// 进程相关系统调用的内核实现。
// 这里主要负责从用户寄存器取出参数、做基本合法性检查，
// 然后调用 proc.c 里的 kfork/kexit/kwait 等核心逻辑。
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "vm.h"

uint64
sys_exit(void)
{
  int n;
  argint(0, &n);
  // exit 后进程进入 ZOMBIE，控制权不再返回用户态。
  kexit(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  // 直接从当前进程控制块读取 pid。
  return myproc()->pid;
}

uint64
sys_getuid(void)
{
  return myproc()->uid;
}

uint64
sys_geteuid(void)
{
  return myproc()->euid;
}

uint64
sys_getgid(void)
{
  return myproc()->gid;
}

uint64
sys_getegid(void)
{
  return myproc()->egid;
}

uint64
sys_setuid(void)
{
  int uid;
  struct proc *p = myproc();

  argint(0, &uid);
  if(uid < 0 || uid > 65535)
    return -1;
  if(p->euid != 0 && uid != p->uid && uid != p->euid)
    return -1;
  if(p->euid == 0)
    p->uid = p->euid = uid;
  else
    p->euid = uid;
  return 0;
}

uint64
sys_setgid(void)
{
  int gid;
  struct proc *p = myproc();

  argint(0, &gid);
  if(gid < 0 || gid > 65535)
    return -1;
  if(p->euid != 0 && gid != p->gid && gid != p->egid)
    return -1;
  if(p->euid == 0)
    p->gid = p->egid = gid;
  else
    p->egid = gid;
  return 0;
}

uint64
sys_umask(void)
{
  int mask;
  uint old;
  struct proc *p = myproc();

  argint(0, &mask);
  if(mask < 0 || mask > PERM_MASK)
    return -1;
  old = p->umask;
  p->umask = mask;
  return old;
}

uint64
sys_fork(void)
{
  // fork 在内核中就是复制当前进程。
  return kfork();
}

uint64
sys_clone(void)
{
  uint64 stack;

  argaddr(0, &stack);
  return kclone(stack);
}

uint64
sys_wait(void)
{
  uint64 p;
  // 参数是用户提供的 int*，用于接收子进程退出状态。
  argaddr(0, &p);
  return kwait(p);
}

uint64
sys_sbrk(void)
{
  uint64 addr;
  int t;
  int n;

  argint(0, &n);
  argint(1, &t);
  addr = myproc()->sz;

  // t 参数选择 eager（立即分配）或 lazy（惰性分配）模式。
  // 收缩内存总是立即释放页。
  if(t == SBRK_EAGER || n < 0) {
    if(growproc(n) < 0) {
      return -1;
    }
  } else {
    // 惰性分配：只扩大虚拟地址空间，不分配物理页；
    // 进程真正读写这些地址时，缺页处理 vmfault() 才分配物理页。
    if(addr + n < addr)
      return -1;
    // 与普通 sbrk 保持一致，避免堆侵入 mmap 区域。
    if(addr + n > MMAP_BASE)
      return -1;
    myproc()->sz += n;
  }
  return addr;
}

uint64
sys_pause(void)
{
  int n;
  uint ticks0;

  argint(0, &n);
  if(n < 0)
    n = 0;
  // 睡眠直到 ticks 至少前进 n 个时钟节拍。
  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < n){
    if(killed(myproc())){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_setpriority(void)
{
  int pid, prio;

  argint(0, &pid);
  argint(1, &prio);
  return ksetpriority(pid, prio);
}

uint64
sys_kill(void)
{
  int pid;

  // kill 只设置目标进程的 killed 标志，实际退出延迟到返回用户态时。
  argint(0, &pid);
  return kkill(pid);
}

// 返回系统启动以来的时钟节拍数。
uint64
sys_uptime(void)
{
  uint xticks;

  acquire(&tickslock);
  xticks = ticks;
  release(&tickslock);
  return xticks;
}
