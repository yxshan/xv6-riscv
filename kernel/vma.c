// 用户进程 VMA（虚拟内存区域）管理。
//
// mmap 私有映射先登记 VMA，不立即分配物理页；
// 进程访问映射地址触发缺页时，vma_fault() 按需从文件读取或补零页。
//
// VMA 表本身是一个带引用计数的共享对象：fork 复制一份，clone 线程共享同一份。
// 因此 mmap/munmap 描述符对所有线程可见，缺页页也通过 PTE_SHM|PTE_COW
// 引用计数在同一线程组内共享同一物理页。
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "fs.h"
#include "file.h"
#include "fcntl.h"
#include "vma.h"

extern struct proc proc[NPROC];

static struct vma*
vma_find_locked(struct proc_vmas *pv, uint64 va)
{
  for(int i = 0; i < NVMA; i++){
    struct vma *v = &pv->vmas[i];
    if(v->used && va >= v->start && va < v->end)
      return v;
  }
  return 0;
}

// 在 mmap 区域中从高地址向下查找一块空闲地址空间。
// 调用者必须持有 pv->lock。
static uint64
vma_alloc_locked(struct proc_vmas *pv, uint64 npages)
{
  uint64 size = npages * PGSIZE;

  if(npages == 0 || size > MMAP_SIZE)
    return 0;

  for(uint64 end = SHM_BASE; end - size >= MMAP_BASE; end -= PGSIZE){
    uint64 start = end - size;
    int free = 1;

    for(int i = 0; i < NVMA; i++){
      struct vma *v = &pv->vmas[i];
      if(v->used && v->start < end && v->end > start){
        free = 0;
        break;
      }
    }
    if(free)
      return start;
  }
  return 0;
}

// 登记一个 VMA。start/end 必须页对齐，且不与已有 VMA 重叠。
// 调用者必须持有 pv->lock。
static int
vma_add_locked(struct proc_vmas *pv, uint64 start, uint64 end, int prot,
               int flags, struct inode *ip, uint off)
{
  struct vma *empty = 0;

  if(start % PGSIZE != 0 || end % PGSIZE != 0 || end <= start)
    return -1;
  for(int i = 0; i < NVMA; i++){
    struct vma *v = &pv->vmas[i];
    if(v->used && v->start < end && v->end > start)
      return -1;
    if(!v->used && empty == 0)
      empty = v;
  }
  if(empty == 0)
    return -1;

  empty->used = 1;
  empty->start = start;
  empty->end = end;
  empty->prot = prot;
  empty->flags = flags;
  empty->ip = ip;
  empty->off = off;
  return 0;
}

struct proc_vmas*
proc_vmas_alloc(void)
{
  struct proc_vmas *pv = kalloc();

  if(pv == 0)
    return 0;
  memset(pv, 0, PGSIZE);
  initsleeplock(&pv->lock, "proc_vmas");
  pv->ref = 1;
  return pv;
}

void
proc_vmas_share(struct proc_vmas *pv)
{
  acquiresleep(&pv->lock);
  pv->ref++;
  releasesleep(&pv->lock);
}

// 把共享映射中已驻留的可写页写回文件。
// 同一个 MAP_SHARED 页在 clone 线程间共享，因此扫描所有持有该 VMA 表的
// 线程页表，找到可写映射后写回一次即可。
// 调用者必须持有 pv->lock。
static void
vma_writeback_range(struct proc_vmas *pv, struct vma *v,
                    uint64 start, uint64 end)
{
  if((v->flags & MAP_SHARED) == 0 || v->ip == 0)
    return;
  if(start < v->start)
    start = v->start;
  if(end > v->end)
    end = v->end;
  if(start >= end)
    return;

  begin_op();
  for(uint64 va = start; va < end; va += PGSIZE){
    pte_t *pte = 0;

    for(struct proc *q = proc; q < &proc[NPROC]; q++){
      if(q->vmas != pv || q->pagetable == 0)
        continue;
      pte = walk(q->pagetable, va, 0);
      if(pte && (*pte & PTE_V) && (*pte & PTE_W))
        break;
      pte = 0;
    }
    if(pte == 0)
      continue;

    uint64 off = v->off + (va - v->start);
    ilock(v->ip);
    if(off < v->ip->size){
      uint64 n = v->ip->size - off;
      if(n > PGSIZE)
        n = PGSIZE;
      writei(v->ip, 0, PTE2PA(*pte), off, n);
    }
    iunlock(v->ip);
  }
  end_op();
}

// 解除所有共享同一 VMA 表的线程页表中的一段映射。
// 调用者必须持有 pv->lock。
static void
vma_unmap_range(struct proc_vmas *pv, uint64 start, uint64 end)
{
  uint64 npages = (end - start) / PGSIZE;

  for(struct proc *q = proc; q < &proc[NPROC]; q++){
    if(q->vmas == pv && q->pagetable)
      uvmunmap(q->pagetable, start, npages, 1);
  }
}

// 记录 VMA 页缓存并增加一份 cow 引用。
// 调用者必须持有 pv->lock。
static void
vma_cache_add(struct proc_vmas *pv, uint64 va, uint64 pa)
{
  int idx = (va - MMAP_BASE) / PGSIZE;

  if(idx < 0 || idx >= VMA_PAGE_COUNT)
    panic("vma_cache_add");
  if(pv->pages[idx] != 0)
    return;
  pv->pages[idx] = pa;
  cow_add(pa);
}

// 释放一段 VMA 区域的页缓存引用并清空记录。
// 调用者必须持有 pv->lock。
static void
vma_cache_release_range(struct proc_vmas *pv, uint64 start, uint64 end)
{
  for(uint64 va = start; va < end; va += PGSIZE){
    int idx = (va - MMAP_BASE) / PGSIZE;

    if(idx < 0 || idx >= VMA_PAGE_COUNT)
      panic("vma_cache_release_range");
    if(pv->pages[idx] == 0)
      continue;
    uint64 pa = pv->pages[idx];
    pv->pages[idx] = 0;
    cow_release(pa);
  }
}

// 释放当前线程对 VMA 表的引用。
// 只解除当前线程页表中的映射；最后一个引用才释放 inode 并回收表本身。
void
proc_vmas_release(struct proc *p)
{
  struct proc_vmas *pv = p->vmas;
  int last = 0;

  if(pv == 0)
    return;

  acquiresleep(&pv->lock);
  for(int i = 0; i < NVMA; i++){
    struct vma *v = &pv->vmas[i];
    if(!v->used)
      continue;
    vma_writeback_range(pv, v, v->start, v->end);
    if(p->pagetable)
      uvmunmap(p->pagetable, v->start, (v->end - v->start) / PGSIZE, 1);
  }

  if(--pv->ref == 0)
    last = 1;
  if(last){
    for(int i = 0; i < NVMA; i++){
      struct vma *v = &pv->vmas[i];
      if(!v->used)
        continue;
      vma_cache_release_range(pv, v->start, v->end);
      if(v->ip){
        begin_op();
        iput(v->ip);
        end_op();
      }
      v->used = 0;
      v->ip = 0;
    }
  }
  releasesleep(&pv->lock);

  if(last)
    kfree(pv);
  p->vmas = 0;
}

// 在 mmap 区域中从高地址向下查找一块空闲地址空间。
uint64
vma_alloc(struct proc *p, uint64 npages)
{
  struct proc_vmas *pv = p->vmas;
  uint64 start;

  if(pv == 0)
    return 0;
  acquiresleep(&pv->lock);
  start = vma_alloc_locked(pv, npages);
  releasesleep(&pv->lock);
  return start;
}

// 登记一个 VMA。
int
vma_add(struct proc *p, uint64 start, uint64 end, int prot, int flags,
        struct inode *ip, uint off)
{
  struct proc_vmas *pv = p->vmas;
  int r;

  if(pv == 0)
    return -1;
  acquiresleep(&pv->lock);
  r = vma_add_locked(pv, start, end, prot, flags, ip, off);
  releasesleep(&pv->lock);
  return r;
}

// 原子完成地址分配和 VMA 登记，避免并发 mmap 在两个线程间竞争槽位。
uint64
vma_mmap(struct proc *p, uint64 npages, int prot, int flags,
         struct inode *ip, uint off)
{
  struct proc_vmas *pv = p->vmas;
  uint64 start;

  if(pv == 0)
    return 0;

  acquiresleep(&pv->lock);
  start = vma_alloc_locked(pv, npages);
  if(start != 0 &&
     vma_add_locked(pv, start, start + npages * PGSIZE,
                    prot, flags, ip, off) < 0)
    start = 0;
  releasesleep(&pv->lock);
  return start;
}

// 解除 VMA 中的一段映射，支持页对齐的部分 munmap。
// munmap 对线程组是全局操作：描述符删除后，所有共享该表的线程页表
// 都要解除对应映射，避免其他线程继续访问已释放的区域。
int
vma_remove(struct proc *p, uint64 addr, uint64 length)
{
  struct proc_vmas *pv = p->vmas;
  struct vma *v, *right = 0;
  uint64 end, oldstart;

  if(pv == 0 || addr % PGSIZE != 0 || length == 0 || addr >= MAXVA ||
     length > MAXVA - addr)
    return -1;
  end = PGROUNDUP(addr + length);

  acquiresleep(&pv->lock);
  v = vma_find_locked(pv, addr);
  if(v == 0 || addr < v->start || end > v->end){
    releasesleep(&pv->lock);
    return -1;
  }

  // 中间切除时需要保留右侧部分，先申请 VMA 槽位。
  if(addr > v->start && end < v->end){
    for(int i = 0; i < NVMA; i++){
      if(!pv->vmas[i].used){
        right = &pv->vmas[i];
        break;
      }
    }
    if(right == 0){
      releasesleep(&pv->lock);
      return -1;
    }
    *right = *v;
    right->start = end;
    right->off = v->off + (end - v->start);
    if(v->ip)
      right->ip = idup(v->ip);
  }

  vma_writeback_range(pv, v, addr, end);
  vma_unmap_range(pv, addr, end);
  vma_cache_release_range(pv, addr, end);

  oldstart = v->start;
  if(addr == oldstart && end == v->end){
    if(v->ip){
      begin_op();
      iput(v->ip);
      end_op();
    }
    v->used = 0;
    v->ip = 0;
  } else if(addr == oldstart){
    v->start = end;
    v->off += end - oldstart;
  } else if(end == v->end){
    v->end = addr;
  } else {
    v->end = addr;
  }
  releasesleep(&pv->lock);
  return 0;
}

// exec 前清除旧进程的所有 VMA：
// 先写回共享页，再解除所有线程页表中的映射，最后释放 inode 引用。
// 表本身仍由 proc_vmas_release() 在进程回收时释放。
void
vma_clear(struct proc *p)
{
  struct proc_vmas *pv = p->vmas;

  if(pv == 0)
    return;

  acquiresleep(&pv->lock);
  for(int i = 0; i < NVMA; i++){
    struct vma *v = &pv->vmas[i];
    if(!v->used)
      continue;
    vma_writeback_range(pv, v, v->start, v->end);
    vma_unmap_range(pv, v->start, v->end);
    vma_cache_release_range(pv, v->start, v->end);
    if(v->ip){
      begin_op();
      iput(v->ip);
      end_op();
    }
    v->used = 0;
    v->ip = 0;
  }
  releasesleep(&pv->lock);
}

// fork 时复制 VMA 描述符；私有页复制，共享页映射同一物理页。
int
vma_copy(struct proc *np, struct proc *p)
{
  struct proc_vmas *npv = np->vmas;
  struct proc_vmas *pv = p->vmas;

  if(npv == 0 || pv == 0)
    return -1;

  acquiresleep(&pv->lock);
  for(int i = 0; i < NVMA; i++){
    struct vma *v = &pv->vmas[i];
    if(!v->used)
      continue;

    npv->vmas[i] = *v;
    npv->vmas[i].ip = 0;
    if(v->ip)
      npv->vmas[i].ip = idup(v->ip);

    for(uint64 va = v->start; va < v->end; va += PGSIZE){
      pte_t *pte = walk(p->pagetable, va, 0);
      char *mem;
      uint64 pa;
      int perm = PTE_R | PTE_U;

      if(pte == 0 || (*pte & PTE_V) == 0)
        continue;
      pa = PTE2PA(*pte);
      if(v->prot & PROT_WRITE)
        perm |= PTE_W;
      if(v->prot & PROT_EXEC)
        perm |= PTE_X;

      if(v->flags & MAP_SHARED){
        cow_add(pa);
        if(mappages(np->pagetable, va, PGSIZE, pa,
                    perm | PTE_SHM | PTE_COW) != 0){
          cow_release(pa);
          goto err;
        }
        vma_cache_add(npv, va, pa);
        continue;
      }

      mem = kalloc();
      if(mem == 0)
        goto err;
      memmove(mem, (char*)pa, PGSIZE);
      if(mappages(np->pagetable, va, PGSIZE, (uint64)mem,
                  perm | PTE_SHM | PTE_COW) != 0){
        kfree(mem);
        goto err;
      }
      cow_add((uint64)mem);
      vma_cache_add(npv, va, (uint64)mem);
    }
  }
  releasesleep(&pv->lock);
  return 0;

err:
  releasesleep(&pv->lock);
  return -1;
}

// 查找同线程组内已经建立的 VMA 页。
// clone 线程拥有独立页表根，但共享同一 VMA 表；
// 第一线程缺页建立的物理页必须让后续线程复用，才能保持 mmap 语义。
// 调用者必须持有 pv->lock。
static uint64
vma_find_existing_page(struct proc_vmas *pv, uint64 va)
{
  for(struct proc *q = proc; q < &proc[NPROC]; q++){
    pte_t *pte;

    if(q->vmas != pv || q->pagetable == 0)
      continue;
    pte = walk(q->pagetable, va, 0);
    if(pte && ((*pte & (PTE_SHM | PTE_COW)) == (PTE_SHM | PTE_COW)))
      return PTE2PA(*pte);
  }
  return 0;
}

// mmap 缺页处理：找到 VMA 后按需分配物理页并读取文件内容。
uint64
vma_fault(struct proc *p, uint64 va)
{
  struct proc_vmas *pv = p->vmas;
  struct vma *v;
  char *mem;
  uint64 pa;
  int perm;

  if(pv == 0)
    return 0;

  acquiresleep(&pv->lock);
  v = vma_find_locked(pv, va);
  if(v == 0 || ismapped(p->pagetable, va)){
    releasesleep(&pv->lock);
    return 0;
  }

  perm = PTE_R | PTE_U;
  if(v->prot & PROT_WRITE)
    perm |= PTE_W;
  if(v->prot & PROT_EXEC)
    perm |= PTE_X;

  // 优先复用 VMA 页缓存；没有缓存时再扫描同组线程页表。
  int idx = (va - MMAP_BASE) / PGSIZE;
  pa = pv->pages[idx];
  if(pa == 0)
    pa = vma_find_existing_page(pv, va);
  if(pa != 0){
    if(mappages(p->pagetable, va, PGSIZE, pa,
                perm | PTE_SHM | PTE_COW) != 0){
      releasesleep(&pv->lock);
      return 0;
    }
    cow_add(pa);
    vma_cache_add(pv, va, pa);
    releasesleep(&pv->lock);
    return pa;
  }

  mem = kalloc();
  if(mem == 0){
    releasesleep(&pv->lock);
    return 0;
  }
  memset(mem, 0, PGSIZE);

  if(v->ip){
    uint64 off = v->off + (va - v->start);
    ilock(v->ip);
    if(off < v->ip->size){
      uint64 n = v->ip->size - off;
      if(n > PGSIZE)
        n = PGSIZE;
      if(readi(v->ip, 0, (uint64)mem, off, n) != n){
        iunlock(v->ip);
        kfree(mem);
        releasesleep(&pv->lock);
        return 0;
      }
    }
    iunlock(v->ip);
  }

  if(mappages(p->pagetable, va, PGSIZE, (uint64)mem,
              perm | PTE_SHM | PTE_COW) != 0){
    kfree(mem);
    releasesleep(&pv->lock);
    return 0;
  }
  cow_add((uint64)mem);
  vma_cache_add(pv, va, (uint64)mem);
  releasesleep(&pv->lock);
  return (uint64)mem;
}
