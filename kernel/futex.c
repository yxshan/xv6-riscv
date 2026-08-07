// futex：用户态同步原语的内核支持。
//
// futex_wait(addr, expect) 在 *addr == expect 时睡眠；
// futex_wake(addr, n) 唤醒在 addr 上等待的线程。
// 用户态可以用它实现互斥锁、条件变量等高级同步结构。
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"
#include "proc.h"
#include "futex.h"

static struct spinlock futex_lock;

#define FUTEX_OWNERS 64
struct futex_owner {
  int used;
  uint64 addr;
  int tid;
  pagetable_t pt;
};
static struct futex_owner owners[FUTEX_OWNERS];

void
futexinit(void)
{
  initlock(&futex_lock, "futex");
}

uint64
sys_futex_wait(void)
{
  struct proc *p = myproc();
  uint64 addr;
  int expect, val;
  uint64 pa;

  argaddr(0, &addr);
  argint(1, &expect);
  if(addr == 0 || addr >= MAXVA)
    return -1;

  acquire(&futex_lock);
  pa = walkaddr(p->pagetable, addr);
  if(pa == 0){
    release(&futex_lock);
    return -1;
  }
  val = *(int*)pa;
  if(val != expect){
    release(&futex_lock);
    return 0;
  }
  sleep((void*)addr, &futex_lock);
  release(&futex_lock);
  return 0;
}

uint64
sys_futex_wake(void)
{
  uint64 addr;
  int n;

  argaddr(0, &addr);
  argint(1, &n);
  if(addr == 0 || addr >= MAXVA)
    return -1;

  acquire(&futex_lock);
  wakeup((void*)addr);
  release(&futex_lock);
  return 0;
}

static uint
futex_ticks(void)
{
  uint t;

  acquire(&tickslock);
  t = ticks;
  release(&tickslock);
  return t;
}

uint64
sys_futex_wait_timeout(void)
{
  struct proc *p = myproc();
  uint64 addr;
  int expect, timeout_ms, val;
  uint64 pa;
  uint start, need;

  argaddr(0, &addr);
  argint(1, &expect);
  argint(2, &timeout_ms);
  if(addr == 0 || addr >= MAXVA || timeout_ms < 0)
    return -1;

  acquire(&futex_lock);
  pa = walkaddr(p->pagetable, addr);
  if(pa == 0){
    release(&futex_lock);
    return -1;
  }
  val = *(int*)pa;
  if(val != expect){
    release(&futex_lock);
    return 0;
  }
  if(timeout_ms == 0){
    release(&futex_lock);
    return -1;
  }
  release(&futex_lock);

  need = (uint)((timeout_ms + 99) / 100);
  start = futex_ticks();
  while(futex_ticks() - start < need){
    if(killed(p) || p->stop_pending){
      return -1;
    }
    acquire(&tickslock);
    sleep(&ticks, &tickslock);
    release(&tickslock);

    acquire(&futex_lock);
    pa = walkaddr(p->pagetable, addr);
    if(pa == 0){
      release(&futex_lock);
      return -1;
    }
    val = *(int*)pa;
    if(val != expect){
      release(&futex_lock);
      return 0;
    }
    release(&futex_lock);
  }
  return -1;
}

uint64
sys_futex_set_owner(void)
{
  struct proc *p = myproc();
  uint64 addr;
  int tid;

  argaddr(0, &addr);
  argint(1, &tid);
  if(addr == 0 || addr >= MAXVA || tid <= 0)
    return -1;

  acquire(&futex_lock);
  for(int i = 0; i < FUTEX_OWNERS; i++){
    if(owners[i].used && owners[i].addr == addr){
      owners[i].tid = tid;
      owners[i].pt = p->pagetable;
      release(&futex_lock);
      return 0;
    }
  }
  for(int i = 0; i < FUTEX_OWNERS; i++){
    if(!owners[i].used){
      owners[i].used = 1;
      owners[i].addr = addr;
      owners[i].tid = tid;
      owners[i].pt = p->pagetable;
      release(&futex_lock);
      return 0;
    }
  }
  release(&futex_lock);
  return -1;
}

uint64
sys_futex_clear_owner(void)
{
  uint64 addr;

  argaddr(0, &addr);
  acquire(&futex_lock);
  for(int i = 0; i < FUTEX_OWNERS; i++){
    if(owners[i].used && owners[i].addr == addr){
      owners[i].used = 0;
      owners[i].addr = 0;
      owners[i].tid = 0;
      owners[i].pt = 0;
      release(&futex_lock);
      return 0;
    }
  }
  release(&futex_lock);
  return -1;
}

// 线程退出时，把仍登记为持有者的 futex 标记为 owner-died 并唤醒等待者。
void
futex_owner_exited(int tid)
{
  acquire(&futex_lock);
  for(int i = 0; i < FUTEX_OWNERS; i++){
    struct futex_owner *o = &owners[i];
    pte_t *pte;

    if(!o->used || o->tid != tid)
      continue;
    uint64 addr = o->addr;
    if(o->pt){
      pte = walk(o->pt, addr, 0);
      if(pte && (*pte & PTE_V) && (*pte & PTE_U)){
        int *v = (int*)PTE2PA(*pte);
        *v |= FUTEX_OWNER_DIED;
      }
    }
    o->used = 0;
    o->addr = 0;
    o->tid = 0;
    o->pt = 0;
    wakeup((void*)addr);
  }
  release(&futex_lock);
}
