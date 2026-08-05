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

// 解除一个完整 VMA 映射，释放已分配的物理页和 inode 引用。
int
vma_remove(struct proc *p, uint64 addr, uint64 length)
{
  struct vma *v;
  uint64 end;

  if(addr % PGSIZE != 0 || length == 0 || addr >= MAXVA ||
     length > MAXVA - addr)
    return -1;
  end = PGROUNDUP(addr + length);
  v = vma_find(p, addr);
  if(v == 0 || v->start != addr || v->end != end)
    return -1;

  uvmunmap(p->pagetable, addr, (end - addr) / PGSIZE, 1);
  if(v->ip){
    begin_op();
    iput(v->ip);
    end_op();
  }
  v->used = 0;
  v->ip = 0;
  return 0;
}

// 清除进程所有 VMA：先解除映射，再释放 inode 引用。
void
vma_clear(struct proc *p)
{
  for(int i = 0; i < NVMA; i++){
    struct vma *v = &p->vmas[i];
    if(!v->used)
      continue;
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

// fork 时复制 VMA 描述符，并把父进程中已驻留的页面复制给子进程。
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

      if(pte == 0 || (*pte & PTE_V) == 0)
        continue;
      mem = kalloc();
      if(mem == 0)
        goto err;
      memmove(mem, (char*)PTE2PA(*pte), PGSIZE);

      int perm = PTE_R | PTE_U;
      if(v->prot & PROT_WRITE)
        perm |= PTE_W;
      if(v->prot & PROT_EXEC)
        perm |= PTE_X;
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
  if(mappages(p->pagetable, va, PGSIZE, (uint64)mem, perm) != 0){
    kfree(mem);
    return 0;
  }
  return (uint64)mem;
}
