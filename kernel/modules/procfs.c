// /proc 伪文件系统。
//
// xv6 没有完整 VFS，因此以伪设备形式提供动态生成内容：
// 每次 read 时输出进程列表、空闲内存和系统 ticks。

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "fs.h"
#include "file.h"
#include "module.h"
#include "module_ids.h"

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

static char *proc_state_names[] = {
  "unused", "used", "sleep", "runble", "run", "zombie"
};

static int
build_proc(char *buf, int max)
{
  extern struct proc proc[NPROC];
  int off = 0;

  off = append_str(buf, max, off, "processes ");
  off += fmt_uint(buf + off, max - off, proccount());
  off = append_str(buf, max, off, "\nfree_pages ");
  off += fmt_uint(buf + off, max - off, freemem() / PGSIZE);
  off = append_str(buf, max, off, "\nticks ");
  off += fmt_uint(buf + off, max - off, ticks);
  off = append_str(buf, max, off, "\n");

  for(struct proc *p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->state != UNUSED){
      off = append_str(buf, max, off, "pid ");
      off += fmt_uint(buf + off, max - off, p->pid);
      off = append_str(buf, max, off, " ");
      if(p->state >= 0 && p->state < NELEM(proc_state_names))
        off = append_str(buf, max, off, proc_state_names[p->state]);
      off = append_str(buf, max, off, " prio ");
      off += fmt_uint(buf + off, max - off, p->priority);
      off = append_str(buf, max, off, " q ");
      off += fmt_uint(buf + off, max - off, p->qlevel);
      off = append_str(buf, max, off, " ");
      off = append_str(buf, max, off, p->name);
      off = append_str(buf, max, off, "\n");
    }
    release(&p->lock);
  }
  return off;
}

static int
proc_dev_open(struct file *f)
{
  f->off = 0;
  return 0;
}

static int
proc_dev_read(struct file *f, int user_dst, uint64 dst, int n)
{
  char buf[2048];
  int len = build_proc(buf, sizeof(buf));

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
proc_dev_write(struct file *f, int user_src, uint64 src, int n)
{
  (void)f;
  (void)user_src;
  (void)src;
  (void)n;
  return -1;
}

static int
proc_dev_close(struct file *f)
{
  (void)f;
  return 0;
}

static struct kmod_device proc_device = {
  KMOD_PROC_MAJOR,
  "proc",
  proc_dev_open,
  proc_dev_read,
  proc_dev_write,
  proc_dev_close,
};

static int
procfs_init(void)
{
  return module_device_register(&proc_device);
}

KMOD_REGISTER(procfs, procfs_init, 0, KMOD_PRIORITY_DEFAULT);
