// 用户进程 VMA（虚拟内存区域）管理。
//
// mmap 私有映射先登记 VMA，不立即分配物理页；
// 进程访问映射地址触发缺页时，vma_fault() 按需从文件读取或补零页。
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

static struct vma*
vma_find(struct proc *p, uint64 va)
{
  for(int i = 0; i < NVMA; i++){
    struct vma *v = &p->vmas[i];
    if(v->used && va >= v->start && va < v->end)
      return v;
  }
  return 0;
}

// 在 mmap 区域中从高地址向下查找一块空闲地址空间。
uint64
vma_alloc(struct proc *p, uint64 npages)
{
  uint64 size = npages * PGSIZE;

  if(npages == 0 || size > MMAP_SIZE)
    return 0;

  for(uint64 end = SHM_BASE; end - size >= MMAP_BASE; end -= PGSIZE){
    uint64 start = end - size;
    int free = 1;

    for(int i = 0; i < NVMA; i++){
      struct vma *v = &p->vmas[i];
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
int
vma_add(struct proc *p, uint64 start, uint64 end, int prot, int flags,
        struct inode *ip, uint off)
{
  struct vma *empty = 0;

  if(start % PGSIZE != 0 || end % PGSIZE != 0 || end <= start)
    return -1;
  for(int i = 0; i < NVMA; i++){
    struct vma *v = &p->vmas[i];
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

// 把共享映射中已驻留的可写页写回文件。
static void
vma_writeback_range(struct proc *p, struct vma *v, uint64 start, uint64 end)
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
    pte_t *pte = walk(p->pagetable, va, 0);
    if(pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_W) == 0)
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

// 解除 VMA 中的一段映射，支持页对齐的部分 munmap。
int
vma_remove(struct proc *p, uint64 addr, uint64 length)
{
  struct vma *v, *right = 0;
  uint64 end, oldstart;

  if(addr % PGSIZE != 0 || length == 0 || addr >= MAXVA ||
     length > MAXVA - addr)
    return -1;
  end = PGROUNDUP(addr + length);
  v = vma_find(p, addr);
  if(v == 0 || addr < v->start || end > v->end)
    return -1;

  // 中间切除时需要保留右侧部分，先申请 VMA 槽位。
  if(addr > v->start && end < v->end){
    for(int i = 0; i < NVMA; i++){
      if(!p->vmas[i].used){
        right = &p->vmas[i];
        break;
      }
    }
    if(right == 0)
      return -1;
    *right = *v;
    right->start = end;
    right->off = v->off + (end - v->start);
    if(v->ip)
      right->ip = idup(v->ip);
  }

  vma_writeback_range(p, v, addr, end);
  uvmunmap(p->pagetable, addr, (end - addr) / PGSIZE, 1);

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
  return 0;
}

// 清除进程所有 VMA：先写回共享页，再解除映射并释放 inode 引用。
void
vma_clear(struct proc *p)
{
  for(int i = 0; i < NVMA; i++){
    struct vma *v = &p->vmas[i];
    if(!v->used)
      continue;
    vma_writeback_range(p, v, v->start, v->end);
    if(p->pagetable)
      uvmunmap(p->pagetable, v->start, (v->end - v->start) / PGSIZE, 1);
    if(v->ip){
      begin_op();
      iput(v->ip);
      end_op();
    }
    v->used = 0;
    v->ip = 0;
  }
}

// fork 时复制 VMA 描述符；私有页复制，共享页映射同一物理页。
int
vma_copy(struct proc *np, struct proc *p)
{
  for(int i = 0; i < NVMA; i++){
    struct vma *v = &p->vmas[i];
    if(!v->used)
      continue;

    np->vmas[i] = *v;
    np->vmas[i].ip = 0;
    if(v->ip)
      np->vmas[i].ip = idup(v->ip);

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
        continue;
      }

      mem = kalloc();
      if(mem == 0)
        goto err;
      memmove(mem, (char*)pa, PGSIZE);
      if(mappages(np->pagetable, va, PGSIZE, (uint64)mem, perm) != 0){
        kfree(mem);
        goto err;
      }
    }
  }
  return 0;

err:
  vma_clear(np);
  return -1;
}

// mmap 缺页处理：找到 VMA 后按需分配物理页并读取文件内容。
uint64
vma_fault(struct proc *p, uint64 va)
{
  struct vma *v;
  char *mem;
  int perm;

  v = vma_find(p, va);
  if(v == 0 || ismapped(p->pagetable, va))
    return 0;

  mem = kalloc();
  if(mem == 0)
    return 0;
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
        return 0;
      }
    }
    iunlock(v->ip);
  }

  perm = PTE_R | PTE_U;
  if(v->prot & PROT_WRITE)
    perm |= PTE_W;
  if(v->prot & PROT_EXEC)
    perm |= PTE_X;
  if(v->flags & MAP_SHARED)
    perm |= PTE_SHM | PTE_COW;
  if(mappages(p->pagetable, va, PGSIZE, (uint64)mem, perm) != 0){
    kfree(mem);
    return 0;
  }
  if(v->flags & MAP_SHARED)
    cow_add((uint64)mem);
  return (uint64)mem;
}
