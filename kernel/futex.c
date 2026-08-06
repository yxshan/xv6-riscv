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

static struct spinlock futex_lock;

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
