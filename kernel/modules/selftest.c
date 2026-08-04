// 内核自测模块。
//
// 通过 module_call 触发，检查内核运行时基本不变量：
// 进程状态、MLFQ 队列范围、优先级范围以及内存可用性。

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "module.h"
#include "module_ids.h"

static int
selftest_basic(void)
{
  extern struct proc proc[NPROC];

  for(struct proc *p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->state < UNUSED || p->state > ZOMBIE){
      release(&p->lock);
      return -1;
    }
    if(p->state != UNUSED){
      if(p->qlevel < 0 || p->qlevel >= MLFQ_NQUEUES ||
         p->priority < 0 || p->priority > 255){
        release(&p->lock);
        return -1;
      }
    }
    release(&p->lock);
  }

  if(proccount() < 0 || freemem() == 0)
    return -1;
  return 0;
}

static int
selftest_signal(void)
{
  struct proc *p = myproc();

  acquire(&p->lock);
  int ok = (p->sigactive == 0);
  release(&p->lock);
  return ok ? 0 : -1;
}

static uint64
selftest_handler(int cmd, uint64 arg0, uint64 arg1)
{
  (void)arg0;
  (void)arg1;

  switch(cmd){
  case SELFTEST_CMD_BASIC:
    return selftest_basic();
  case SELFTEST_CMD_REGISTRY:
    return module_registry_check();
  case SELFTEST_CMD_SIGNAL:
    return selftest_signal();
  case SELFTEST_CMD_MEMORY:
    if(cow_selftest() != 0 || shm_selftest() != 0)
      return -1;
    return 0;
  default:
    return -1;
  }
}

KMOD_SYSREG(KMOD_SELFTEST, selftest, selftest_handler);
