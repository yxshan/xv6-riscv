// 写时复制 (COW) fork 支持。
//
// fork 时把可写用户页标记为 PTE_COW 并清掉 PTE_W，
// 父子进程共享同一物理页；首次写访问时缺页处理复制页面。

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"

static int cowref[NPHYS];
static struct spinlock cowlock;
static int cowinit;

static void
cow_init_once(void)
{
  if(!cowinit){
    initlock(&cowlock, "cow");
    cowinit = 1;
  }
}

int
cow_add(uint64 pa)
{
  int idx;

  if(pa < KERNBASE || pa >= PHYSTOP)
    return -1;
  idx = (pa - KERNBASE) / PGSIZE;
  cow_init_once();
  acquire(&cowlock);
  cowref[idx]++;
  release(&cowlock);
  return 0;
}

void
cow_release(uint64 pa)
{
  int idx;

  if(pa < KERNBASE || pa >= PHYSTOP)
    panic("cow_release");
  idx = (pa - KERNBASE) / PGSIZE;
  cow_init_once();
  acquire(&cowlock);
  if(cowref[idx] > 1){
    cowref[idx]--;
    release(&cowlock);
  } else {
    cowref[idx] = 0;
    release(&cowlock);
    kfree((void*)pa);
  }
}

int
cow_handle(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;
  char *mem;

  if(va >= MAXVA)
    return -1;
  pte = walk(pagetable, va, 0);
  if(pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_COW) == 0)
    return -1;

  pa = PTE2PA(*pte);
  mem = kalloc();
  if(mem == 0)
    return -1;

  memmove(mem, (char*)pa, PGSIZE);
  *pte = PA2PTE((uint64)mem) | PTE_FLAGS(*pte) | PTE_W;
  *pte &= ~PTE_COW;
  cow_release(pa);
  return 0;
}

int
cow_selftest(void)
{
  cow_init_once();
  acquire(&cowlock);
  for(int i = 0; i < NPHYS; i++){
    if(cowref[i] < 0){
      release(&cowlock);
      return -1;
    }
  }
  release(&cowlock);
  return 0;
}
