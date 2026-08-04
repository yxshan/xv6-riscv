// 示例模块：trace。
//
// 通过事件钩子统计系统调用、进程 fork/exit 和时钟 tick，
// 用 module_call(2, cmd, ...) 读取统计结果。
// 这是事件钩子机制的最小参考实现。

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "fs.h"
#include "file.h"
#include "module.h"
#include "module_ids.h"

static struct spinlock trace_lock;
static int total_syscalls;
static int syscall_counts[KMOD_MAX_SYSCOUNTS];
static int fork_count;
static int exit_count;
static int tick_count;

static int
fmt_uint(char *buf, int max, uint64 x)
{
  char tmp[24];
  int i = 0;
  int n = 0;

  if(max <= 0)
    return 0;
  if(x == 0)
    tmp[i++] = '0';
  while(x > 0){
    tmp[i++] = '0' + x % 10;
    x /= 10;
  }
  while(i > 0 && n < max - 1)
    buf[n++] = tmp[--i];
  return n;
}

static int
append_str(char *buf, int max, int off, const char *s)
{
  for(; *s && off < max - 1; off++)
    buf[off] = *s++;
  return off;
}

static int
trace_dev_open(struct file *f)
{
  f->off = 0;
  return 0;
}

static int
trace_dev_read(struct file *f, int user_dst, uint64 dst, int n)
{
  char buf[256];
  int off = 0;
  int len;

  acquire(&trace_lock);
  off = append_str(buf, sizeof(buf), off, "syscalls ");
  off += fmt_uint(buf + off, sizeof(buf) - off, total_syscalls);
  off = append_str(buf, sizeof(buf), off, "\nforks ");
  off += fmt_uint(buf + off, sizeof(buf) - off, fork_count);
  off = append_str(buf, sizeof(buf), off, "\nexits ");
  off += fmt_uint(buf + off, sizeof(buf) - off, exit_count);
  off = append_str(buf, sizeof(buf), off, "\nticks ");
  off += fmt_uint(buf + off, sizeof(buf) - off, tick_count);
  off = append_str(buf, sizeof(buf), off, "\n");
  len = off;
  release(&trace_lock);

  if(f->off >= (uint)len)
    return 0;
  if(n > len - (int)f->off)
    n = len - (int)f->off;
  if(either_copyout(user_dst, dst, buf + f->off, n) < 0)
    return -1;
  f->off += n;
  return n;
}

static int
trace_dev_write(struct file *f, int user_src, uint64 src, int n)
{
  (void)f;
  (void)user_src;
  (void)src;
  (void)n;
  return -1;
}

static int
trace_dev_close(struct file *f)
{
  (void)f;
  return 0;
}

static struct kmod_device trace_device = {
  KMOD_TRACE_MAJOR,
  "stats",
  trace_dev_open,
  trace_dev_read,
  trace_dev_write,
  trace_dev_close,
};

static void
trace_tick(void)
{
  acquire(&trace_lock);
  tick_count++;
  release(&trace_lock);
}

static void
trace_syscall_enter(int num)
{
  acquire(&trace_lock);
  total_syscalls++;
  if(num >= 0 && num < KMOD_MAX_SYSCOUNTS)
    syscall_counts[num]++;
  release(&trace_lock);
}

static void
trace_syscall_exit(int num, uint64 ret)
{
  (void)num;
  (void)ret;
}

static void
trace_proc_fork(struct proc *child)
{
  (void)child;
  acquire(&trace_lock);
  fork_count++;
  release(&trace_lock);
}

static void
trace_proc_exit(struct proc *p)
{
  (void)p;
  acquire(&trace_lock);
  exit_count++;
  release(&trace_lock);
}

static int
trace_init(void)
{
  initlock(&trace_lock, "trace");
  return module_device_register(&trace_device);
}

static uint64
trace_handler(int cmd, uint64 arg0, uint64 arg1)
{
  int r;
  (void)arg1;

  switch(cmd){
  case TRACE_CMD_TOTAL:
    acquire(&trace_lock);
    r = total_syscalls;
    release(&trace_lock);
    return r;
  case TRACE_CMD_SYS:
    if(arg0 >= KMOD_MAX_SYSCOUNTS)
      return -1;
    acquire(&trace_lock);
    r = syscall_counts[arg0];
    release(&trace_lock);
    return r;
  case TRACE_CMD_FORKS:
    acquire(&trace_lock);
    r = fork_count;
    release(&trace_lock);
    return r;
  case TRACE_CMD_EXITS:
    acquire(&trace_lock);
    r = exit_count;
    release(&trace_lock);
    return r;
  case TRACE_CMD_TICKS:
    acquire(&trace_lock);
    r = tick_count;
    release(&trace_lock);
    return r;
  case TRACE_CMD_RESET:
    acquire(&trace_lock);
    total_syscalls = 0;
    fork_count = 0;
    exit_count = 0;
    tick_count = 0;
    for(int i = 0; i < KMOD_MAX_SYSCOUNTS; i++)
      syscall_counts[i] = 0;
    release(&trace_lock);
    return 0;
  default:
    return -1;
  }
}

static const struct kmod_hooks trace_hooks = {
  .tick = trace_tick,
  .syscall_enter = trace_syscall_enter,
  .syscall_exit = trace_syscall_exit,
  .proc_fork = trace_proc_fork,
  .proc_exit = trace_proc_exit,
};

KMOD_REGISTER(trace, trace_init, 0, KMOD_PRIORITY_DEFAULT);
KMOD_SYSREG(KMOD_TRACE, trace, trace_handler);
KMOD_HOOKREG(trace, trace_hooks);
