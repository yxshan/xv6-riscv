// 交换空间。
//
// 第三块 virtio 磁盘作为原始交换盘，不承载文件系统。
// 内存不足时 swap_evict() 把当前进程的一页写回交换盘，
// 并把 PTE 标记为 PTE_SWAP，PPN 字段记录交换槽号；
// 再次访问时 vmfault() 从交换盘读回并恢复普通 PTE。
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "fs.h"
#include "buf.h"
#include "swap.h"

static unsigned char swapmap[(SWAP_PAGES + 7) / 8];
static uint64 swapflags[SWAP_PAGES];
static struct spinlock swaplock;
static uint64 swapouts;
static uint64 swapins;

void
swapinit(void)
{
  initlock(&swaplock, "swap");
}

static int
swap_alloc(void)
{
  int slot = -1;

  acquire(&swaplock);
  for(int i = 0; i < SWAP_PAGES; i++){
    if((swapmap[i / 8] & (1 << (i % 8))) == 0){
      swapmap[i / 8] |= 1 << (i % 8);
      slot = i;
      break;
    }
  }
  release(&swaplock);
  return slot;
}

void
swap_free(int slot)
{
  if(slot < 0 || slot >= SWAP_PAGES)
    panic("swap_free");
  acquire(&swaplock);
  if((swapmap[slot / 8] & (1 << (slot % 8))) == 0)
    panic("swap_free: not allocated");
  swapmap[slot / 8] &= ~(1 << (slot % 8));
  swapflags[slot] = 0;
  release(&swaplock);
}

uint64
swap_flags(int slot)
{
  uint64 flags = 0;

  if(slot < 0 || slot >= SWAP_PAGES)
    return 0;
  acquire(&swaplock);
  flags = swapflags[slot];
  release(&swaplock);
  return flags;
}

// 把物理页写入交换槽；4 个 1KB 磁盘块组成一页。
int
swap_write(uint64 pa, int slot)
{
  if(pa == 0 || slot < 0 || slot >= SWAP_PAGES)
    return -1;
  for(int i = 0; i < SWAP_BLOCKS_PER_PAGE; i++){
    struct buf *b = bread(SWAPDEV, slot * SWAP_BLOCKS_PER_PAGE + i);
    memmove(b->data, (char*)pa + i * BSIZE, BSIZE);
    bwrite(b);
    brelse(b);
  }
  acquire(&swaplock);
  swapouts++;
  release(&swaplock);
  return 0;
}

int
swap_read(uint64 pa, int slot)
{
  if(pa == 0 || slot < 0 || slot >= SWAP_PAGES)
    return -1;
  for(int i = 0; i < SWAP_BLOCKS_PER_PAGE; i++){
    struct buf *b = bread(SWAPDEV, slot * SWAP_BLOCKS_PER_PAGE + i);
    memmove((char*)pa + i * BSIZE, b->data, BSIZE);
    brelse(b);
  }
  acquire(&swaplock);
  swapins++;
  release(&swaplock);
  return 0;
}

// 从当前进程换出一页，优先选择地址最高的用户页。
// COW 页先通过 cow_handle() 私有化，再写入交换盘。
int
swap_evict(void)
{
  struct proc *p = myproc();
  pte_t *pte;
  uint64 va, pa, flags;
  int slot;

  if(p == 0 || p->pagetable == 0 || p->sz == 0)
    return -1;

  for(va = PGROUNDDOWN(p->sz - 1); ; va -= PGSIZE){
    pte = walk(p->pagetable, va, 0);
    if(pte != 0 && (*pte & PTE_V) && (*pte & PTE_U) &&
       (*pte & PTE_SHM) == 0 && (*pte & PTE_SWAP) == 0){
      if((*pte & PTE_COW) && cow_handle(p->pagetable, va) < 0)
        goto next;
      pte = walk(p->pagetable, va, 0);
      if(pte == 0 || (*pte & PTE_V) == 0 || (*pte & PTE_COW) != 0)
        goto next;

      slot = swap_alloc();
      if(slot < 0)
        return -1;
      pa = PTE2PA(*pte);
      if(swap_write(pa, slot) < 0){
        swap_free(slot);
        return -1;
      }
      flags = PTE_FLAGS(*pte) & (PTE_R | PTE_W | PTE_X | PTE_U);
      acquire(&swaplock);
      swapflags[slot] = flags;
      release(&swaplock);
      kfree((void*)pa);
      *pte = ((uint64)slot << 10) | PTE_SWAP | PTE_U;
      sfence_vma();
      return 0;
    }
next:
    if(va == 0)
      break;
  }
  return -1;
}

uint64
sys_swapout(void)
{
  return swap_evict() == 0 ? 0 : -1;
}

uint64
sys_swapinfo(void)
{
  uint64 addr;
  struct swapinfo si;
  int free = 0;

  argaddr(0, &addr);
  acquire(&swaplock);
  for(int i = 0; i < SWAP_PAGES; i++){
    if((swapmap[i / 8] & (1 << (i % 8))) == 0)
      free++;
  }
  si.total_pages = SWAP_PAGES;
  si.free_pages = free;
  si.swapouts = swapouts;
  si.swapins = swapins;
  release(&swaplock);

  if(copyout(myproc()->pagetable, addr, (char*)&si, sizeof(si)) < 0)
    return -1;
  return 0;
}
