// 内核崩溃转储与栈回溯。
//
// 提供 backtrace() 供 panic() 调用，也提供 dumpstate 系统调用，
// 让用户态可以主动查看当前进程、陷阱寄存器和内核调用栈。

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "proc.h"

void
backtrace(void)
{
  uint64 fp = r_fp();

  printf("backtrace:\n");
  for(int i = 0; i < 16; i++){
    uint64 ra = *(uint64*)(fp - 8);
    uint64 prev = *(uint64*)(fp - 16);
    printf("  %lx\n", ra);
    if(prev <= fp || prev >= PHYSTOP)
      break;
    fp = prev;
  }
}

uint64
sys_dumpstate(void)
{
  struct proc *p = myproc();

  printf("=== kernel crash dump ===\n");
  if(p)
    printf("pid=%d name=%s\n", p->pid, p->name);
  printf("scause=0x%lx sepc=0x%lx stval=0x%lx\n",
         r_scause(), r_sepc(), r_stval());
  backtrace();
  procdump();
  return 0;
}
