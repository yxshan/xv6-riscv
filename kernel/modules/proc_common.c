// 系统信息与进程列表的公共格式化实现。
//
// sysinfo、procfs 和 SYSINFO_CMD_DUMP 共用这里的函数，
// 避免三处各自维护 fmt_uint / append_str / proclist。

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "proc_common.h"

int
kfmt_uint(char *buf, int max, uint64 x)
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

int
kappend_str(char *buf, int max, int off, const char *s)
{
  for(; *s && off < max - 1; off++)
    buf[off] = *s++;
  return off;
}

static char *state_names[] = {
  "unused", "used", "sleep", "runble", "run", "zombie"
};

int
kbuild_sysinfo(char *buf, int max)
{
  int off = 0;

  off = kappend_str(buf, max, off, "processes ");
  off += kfmt_uint(buf + off, max - off, proccount());
  off = kappend_str(buf, max, off, "\nfree_pages ");
  off += kfmt_uint(buf + off, max - off, freemem() / PGSIZE);
  off = kappend_str(buf, max, off, "\nticks ");
  off += kfmt_uint(buf + off, max - off, ticks);
  off = kappend_str(buf, max, off, "\n");
  return off;
}

int
kbuild_proclist(char *buf, int max)
{
  extern struct proc proc[NPROC];
  int off = 0;

  for(struct proc *p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->state != UNUSED){
      off = kappend_str(buf, max, off, "pid ");
      off += kfmt_uint(buf + off, max - off, p->pid);
      off = kappend_str(buf, max, off, " ");
      if(p->state >= 0 && p->state < (int)(sizeof(state_names)/sizeof(state_names[0])))
        off = kappend_str(buf, max, off, state_names[p->state]);
      off = kappend_str(buf, max, off, " prio ");
      off += kfmt_uint(buf + off, max - off, p->priority);
      off = kappend_str(buf, max, off, " q ");
      off += kfmt_uint(buf + off, max - off, p->qlevel);
      off = kappend_str(buf, max, off, " ");
      off = kappend_str(buf, max, off, p->name);
      off = kappend_str(buf, max, off, "\n");
    }
    release(&p->lock);
  }
  return off;
}

int
kbuild_proc_all(char *buf, int max)
{
  int off = kbuild_sysinfo(buf, max);
  off += kbuild_proclist(buf + off, max - off);
  return off;
}
