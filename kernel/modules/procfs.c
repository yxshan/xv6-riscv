// /proc 伪文件系统。
//
// xv6 没有完整 VFS，因此以伪设备形式提供动态生成内容：
// 每次 read 时输出进程列表、空闲内存和系统 ticks。
// 每个打开的文件对象持有独立缓冲，支持小缓冲区多次 read。

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
#include "proc_common.h"

#define PROCFS_BUFSZ PGSIZE

struct procdev_state {
  char *buf;
  int len;
  int pos;
};

static int
proc_dev_open(struct file *f)
{
  struct procdev_state *st;

  f->devstate = 0;
  st = (struct procdev_state*)kalloc();
  if(st == 0)
    return -1;
  st->buf = kalloc();
  if(st->buf == 0){
    kfree((void*)st);
    return -1;
  }
  st->len = 0;
  st->pos = 0;
  f->devstate = st;
  return 0;
}

static int
proc_dev_read(struct file *f, int user_dst, uint64 dst, int n)
{
  struct procdev_state *st = (struct procdev_state*)f->devstate;

  if(st == 0)
    return -1;
  st->len = kbuild_proc_all(st->buf, PROCFS_BUFSZ);
  if(st->pos >= st->len)
    return 0;
  if(n > st->len - st->pos)
    n = st->len - st->pos;
  if(either_copyout(user_dst, dst, st->buf + st->pos, n) < 0)
    return -1;
  st->pos += n;
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
  struct procdev_state *st = (struct procdev_state*)f->devstate;

  if(st){
    if(st->buf)
      kfree(st->buf);
    kfree((void*)st);
  }
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
