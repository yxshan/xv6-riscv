// 系统调用分发。
//
// 用户程序通过 ecall 陷入内核，用户寄存器 a7 存放系统调用编号，
// a0-a5 存放参数。usertrap() 调用 syscall() 后，
// 这里根据编号在 syscalls[] 表中找到对应的内核实现，
// 并把返回值写回 trapframe->a0，随用户寄存器一起恢复。
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "syscall.h"
#include "defs.h"

// 从当前进程的用户地址空间读取一个 uint64。
// 地址必须落在进程大小范围内，且通过 copyin 校验页表映射。
int
fetchaddr(uint64 addr, uint64 *ip)
{
  struct proc *p = myproc();
  // 两个判断都保留，防止 addr+8 发生整数溢出绕过检查。
  if(addr >= p->sz || addr+sizeof(uint64) > p->sz) // both tests needed, in case of overflow
    return -1;
  if(copyin(p->pagetable, (char *)ip, addr, sizeof(*ip)) != 0)
    return -1;
  return 0;
}

// 从用户空间读取一个以 '\0' 结尾的字符串。
// 返回字符串长度（不含 '\0'），失败返回 -1。
int
fetchstr(uint64 addr, char *buf, int max)
{
  struct proc *p = myproc();
  if(copyinstr(p->pagetable, buf, addr, max) < 0)
    return -1;
  return strlen(buf);
}

static uint64
argraw(int n)
{
  struct proc *p = myproc();
  // 系统调用参数按 RISC-V 调用约定放在 a0-a5。
  switch (n) {
  case 0:
    return p->trapframe->a0;
  case 1:
    return p->trapframe->a1;
  case 2:
    return p->trapframe->a2;
  case 3:
    return p->trapframe->a3;
  case 4:
    return p->trapframe->a4;
  case 5:
    return p->trapframe->a5;
  }
  panic("argraw");
  return -1;
}

// 取出第 n 个 32 位系统调用参数。
void
argint(int n, int *ip)
{
  *ip = argraw(n);
}

// 取出第 n 个参数作为指针。
// 这里不做合法性检查，真正使用该地址时 copyin/copyout 会校验页表。
void
argaddr(int n, uint64 *ip)
{
  *ip = argraw(n);
}

// 取出第 n 个参数作为字符串，并复制到 buf，最多 max 字节。
// 成功返回字符串长度，失败返回 -1。
int
argstr(int n, char *buf, int max)
{
  uint64 addr;
  argaddr(n, &addr);
  return fetchstr(addr, buf, max);
}

// Prototypes for the functions that handle system calls.
extern uint64 sys_fork(void);
extern uint64 sys_exit(void);
extern uint64 sys_wait(void);
extern uint64 sys_pipe(void);
extern uint64 sys_read(void);
extern uint64 sys_kill(void);
extern uint64 sys_exec(void);
extern uint64 sys_fstat(void);
extern uint64 sys_chdir(void);
extern uint64 sys_dup(void);
extern uint64 sys_getpid(void);
extern uint64 sys_sbrk(void);
extern uint64 sys_pause(void);
extern uint64 sys_uptime(void);
extern uint64 sys_open(void);
extern uint64 sys_write(void);
extern uint64 sys_mknod(void);
extern uint64 sys_unlink(void);
extern uint64 sys_link(void);
extern uint64 sys_mkdir(void);
extern uint64 sys_close(void);
extern uint64 sys_module_call(void);
extern uint64 sys_module_load(void);
extern uint64 sys_module_unload(void);
extern uint64 sys_setpriority(void);
extern uint64 sys_symlink(void);
extern uint64 sys_mkfifo(void);
extern uint64 sys_dumpstate(void);
extern uint64 sys_shmget(void);
extern uint64 sys_shmat(void);
extern uint64 sys_shmdt(void);
extern uint64 sys_shmrm(void);
extern uint64 sys_signal(void);
extern uint64 sys_sigkill(void);
extern uint64 sys_sigreturn(void);
extern uint64 sys_chmod(void);
extern uint64 sys_chown(void);
extern uint64 sys_getuid(void);
extern uint64 sys_geteuid(void);
extern uint64 sys_getgid(void);
extern uint64 sys_getegid(void);
extern uint64 sys_setuid(void);
extern uint64 sys_setgid(void);
extern uint64 sys_umask(void);
extern uint64 sys_mmap(void);
extern uint64 sys_munmap(void);

// 系统调用号到处理函数的映射表，定义在 syscall.h。
// 使用 C99 指定初始化器，下标即系统调用号。
static uint64 (*syscalls[])(void) = {
[SYS_fork]    sys_fork,
[SYS_exit]    sys_exit,
[SYS_wait]    sys_wait,
[SYS_pipe]    sys_pipe,
[SYS_read]    sys_read,
[SYS_kill]    sys_kill,
[SYS_exec]    sys_exec,
[SYS_fstat]   sys_fstat,
[SYS_chdir]   sys_chdir,
[SYS_dup]     sys_dup,
[SYS_getpid]  sys_getpid,
[SYS_sbrk]    sys_sbrk,
[SYS_pause]   sys_pause,
[SYS_uptime]  sys_uptime,
[SYS_open]    sys_open,
[SYS_write]   sys_write,
[SYS_mknod]   sys_mknod,
[SYS_unlink]  sys_unlink,
[SYS_link]    sys_link,
[SYS_mkdir]   sys_mkdir,
[SYS_close]   sys_close,
[SYS_module_call] sys_module_call,
[SYS_module_load] sys_module_load,
[SYS_module_unload] sys_module_unload,
[SYS_setpriority] sys_setpriority,
[SYS_symlink] sys_symlink,
[SYS_mkfifo] sys_mkfifo,
[SYS_dumpstate] sys_dumpstate,
[SYS_shmget] sys_shmget,
[SYS_shmat] sys_shmat,
[SYS_shmdt] sys_shmdt,
[SYS_shmrm] sys_shmrm,
[SYS_signal] sys_signal,
[SYS_sigkill] sys_sigkill,
[SYS_sigreturn] sys_sigreturn,
[SYS_chmod] sys_chmod,
[SYS_chown] sys_chown,
[SYS_getuid] sys_getuid,
[SYS_geteuid] sys_geteuid,
[SYS_getgid] sys_getgid,
[SYS_getegid] sys_getegid,
[SYS_setuid] sys_setuid,
[SYS_setgid] sys_setgid,
[SYS_umask] sys_umask,
[SYS_mmap] sys_mmap,
[SYS_munmap] sys_munmap,
};

void
syscall(void)
{
  int num;
  struct proc *p = myproc();

  // a7 是用户程序传入的系统调用编号。
  num = p->trapframe->a7;
  module_notify_syscall_enter(num);
  if(num > 0 && num < NELEM(syscalls) && syscalls[num]) {
    // 查表调用对应的内核实现，返回值写入 a0，
    // 用户态从 ecall 返回后就能通过寄存器读到结果。
    uint64 ret = syscalls[num]();
    // sigreturn 会恢复整个用户现场，不能覆盖 a0。
    if(num != SYS_sigreturn)
      p->trapframe->a0 = ret;
  } else {
    // 编号非法：打印提示并把返回值置为 -1。
    printf("%d %s: unknown sys call %d\n",
            p->pid, p->name, num);
    p->trapframe->a0 = -1;
  }
  module_notify_syscall_exit(num, p->trapframe->a0);
}
