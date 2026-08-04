// 自旋锁。
//
// 自旋锁用于保护只会被短暂持有的临界区：获取不到锁时，
// CPU 原地循环等待，而不是睡眠。它适用于内核数据结构的互斥，
// 但持有期间必须关闭中断，否则可能出现“持有锁的进程被时钟中断
// 抢占、其他进程又等待同一把锁”的死锁。
// 释放锁时通过原子操作和内存屏障保证临界区内的读写对其他 CPU 可见。

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "riscv.h"
#include "proc.h"
#include "defs.h"

void
initlock(struct spinlock *lk, char *name)
{
  lk->name = name;
  lk->locked = 0;
  lk->cpu = 0;
}

// 获取锁：原地自旋直到成功。
void
acquire(struct spinlock *lk)
{
  push_off(); // 关闭中断，避免与上下文切换死锁。
  if(holding(lk))
    panic("acquire");

  // RISC-V 上 __sync_lock_test_and_set 会编译为原子交换：
  //   a5 = 1
  //   amoswap.w.aq a5, a5, (lk->locked)
  while(__sync_lock_test_and_set(&lk->locked, 1) != 0)
    ;

  // 内存屏障：保证临界区内的读写严格发生在获取锁之后。
  // RISC-V 上会生成 fence 指令。
  __sync_synchronize();

  // 记录持锁 CPU，供 holding() 检查与调试使用。
  lk->cpu = mycpu();
}

// 释放锁。
void
release(struct spinlock *lk)
{
  if(!holding(lk))
    panic("release");

  lk->cpu = 0;

  // 释放前先加内存屏障，确保临界区内的写对其他 CPU 可见。
  // RISC-V 上会生成 fence 指令。
  __sync_synchronize();

  // 原子地清空 locked，等价于 lk->locked = 0，但不使用普通赋值，
  // 因为普通赋值可能被编译为多条存储指令。
  // RISC-V 上会生成原子交换 amoswap.w zero, zero, (lk->locked)。
  __sync_lock_release(&lk->locked);

  pop_off();
}

// 检查当前 CPU 是否持有该锁。调用前必须关闭中断。
int
holding(struct spinlock *lk)
{
  int r;
  r = (lk->locked && lk->cpu == mycpu());
  return r;
}

// push_off/pop_off 与 intr_off()/intr_on() 类似，但支持嵌套计数：
// 两次 push_off 需要两次 pop_off 才能恢复中断。
// 若进入前中断本来就是关闭的，push_off/pop_off 组合会保持关闭。

void
push_off(void)
{
  int old = intr_get();

  // 关闭中断，避免使用 mycpu() 时发生意外的上下文切换。
  intr_off();

  if(mycpu()->noff == 0)
    // 记录最外层 push_off 之前的中断使能状态。
    mycpu()->intena = old;
  mycpu()->noff += 1;
}

void
pop_off(void)
{
  struct cpu *c = mycpu();
  if(intr_get())
    panic("pop_off - interruptible");
  if(c->noff < 1)
    panic("pop_off");
  c->noff -= 1;
  if(c->noff == 0 && c->intena)
    intr_on();
}
