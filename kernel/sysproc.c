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
#include "time.h"

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
sys_exit_group(void)
{
  int n;

  argint(0, &n);
  kexit_group(n);
  return 0;  // not reached
}

uint64
sys_getpid(void)
{
  // getpid 返回线程组 ID；普通进程等于 pid。
  return myproc()->tgid;
}

uint64
sys_gettid(void)
{
  return myproc()->pid;
}

uint64
sys_set_tls(void)
{
  uint64 tls;

  argaddr(0, &tls);
  myproc()->trapframe->tp = tls;
  return 0;
}

uint64
sys_get_tls(void)
{
  return myproc()->trapframe->tp;
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
  uint64 flags, fn, arg, stack, stub;

  argaddr(0, &flags);
  argaddr(1, &fn);
  argaddr(2, &arg);
  argaddr(3, &stack);
  argaddr(4, &stub);
  return kclone(flags, fn, arg, stack, stub);
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
sys_waitpid(void)
{
  int pid;
  uint64 p;

  argint(0, &pid);
  argaddr(1, &p);
  return kwaitpid(pid, p);
}

uint64
sys_waitpid_flags(void)
{
  int pid, options;
  uint64 p;

  argint(0, &pid);
  argaddr(1, &p);
  argint(2, &options);
  return kwaitpid_flags(pid, p, options);
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
    if(myproc()->stop_pending){
      release(&tickslock);
      return -1;
    }
  }
  release(&tickslock);
  return 0;
}

uint64
sys_nanosleep(void)
{
  struct proc *p = myproc();
  struct timespec req;
  uint64 reqaddr, remaddr;
  uint64 tick_ns = 1000000000UL / TICKS_PER_SEC;
  uint64 ns, need, ticks0;

  argaddr(0, &reqaddr);
  argaddr(1, &remaddr);
  if(reqaddr == 0 ||
     copyin(p->pagetable, (char*)&req, reqaddr, sizeof(req)) < 0)
    return -1;
  if(req.tv_sec < 0 || req.tv_nsec < 0 || req.tv_nsec >= 1000000000L)
    return -1;

  ns = (uint64)req.tv_sec * 1000000000UL + (uint64)req.tv_nsec;
  need = (ns + tick_ns - 1) / tick_ns;
  if(need == 0)
    need = 1;

  acquire(&tickslock);
  ticks0 = ticks;
  while(ticks - ticks0 < need){
    if(killed(p)){
      release(&tickslock);
      return -1;
    }
    if(p->stop_pending){
      release(&tickslock);
      return -1;
    }
    sleep(&ticks, &tickslock);
  }
  release(&tickslock);
  return 0;
}

uint64
sys_clock_gettime(void)
{
  struct proc *p = myproc();
  struct timespec ts;
  uint64 tp;
  int clockid;
  uint t;

  argint(0, &clockid);
  argaddr(1, &tp);
  if(clockid != CLOCK_REALTIME && clockid != CLOCK_MONOTONIC)
    return -1;

  acquire(&tickslock);
  t = ticks;
  release(&tickslock);
  ts.tv_sec = t / TICKS_PER_SEC;
  ts.tv_nsec = (t % TICKS_PER_SEC) * (1000000000L / TICKS_PER_SEC);
  if(copyout(p->pagetable, tp, (char*)&ts, sizeof(ts)) < 0)
    return -1;
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

uint64
sys_tgkill(void)
{
  int tgid, tid, sig;

  argint(0, &tgid);
  argint(1, &tid);
  argint(2, &sig);
  return ktgkill(tgid, tid, sig);
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
