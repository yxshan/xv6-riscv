// 示例模块：sysinfo。
//
// 通过 module_call(1, cmd, 0, 0) 暴露系统基本信息：
//   SYSINFO_CMD_PROC  -> 当前非 UNUSED 进程数
//   SYSINFO_CMD_MEM   -> 空闲物理页数
//   SYSINFO_CMD_TICKS -> 系统启动以来的时钟节拍数

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

static int
sysinfo_dev_open(struct file *f)
{
  f->off = 0;
  return 0;
}

static int
sysinfo_dev_read(struct file *f, int user_dst, uint64 dst, int n)
{
  char buf[128];
  int len = kbuild_sysinfo(buf, sizeof(buf));

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
sysinfo_dev_write(struct file *f, int user_src, uint64 src, int n)
{
  (void)f;
  (void)user_src;
  (void)src;
  (void)n;
  return -1;
}

static int
sysinfo_dev_close(struct file *f)
{
  (void)f;
  return 0;
}

static struct kmod_device sysinfo_device = {
  KMOD_SYSINFO_MAJOR,
  "sysinfo",
  sysinfo_dev_open,
  sysinfo_dev_read,
  sysinfo_dev_write,
  sysinfo_dev_close,
};

static int
sysinfo_init(void)
{
  return module_device_register(&sysinfo_device);
}

static uint64
sysinfo_handler(int cmd, uint64 arg0, uint64 arg1)
{
  (void)arg0;
  (void)arg1;

  switch(cmd){
  case SYSINFO_CMD_PROC:
    return proccount();
  case SYSINFO_CMD_MEM:
    return freemem() / PGSIZE;
  case SYSINFO_CMD_TICKS:
    return ticks;
  case SYSINFO_CMD_DUMP:
    {
      char *buf = kalloc();
      int len;

      if(buf == 0)
        return -1;
      len = kbuild_proclist(buf, PGSIZE);
      if(arg0 == 0 || arg1 < (uint64)len){
        kfree(buf);
        return -1;
      }
      if(copyout(myproc()->pagetable, arg0, buf, len) < 0){
        kfree(buf);
        return -1;
      }
      kfree(buf);
      return len;
    }
  default:
    return -1;
  }
}

KMOD_REGISTER(sysinfo, sysinfo_init, 0, KMOD_PRIORITY_DEFAULT);
KMOD_SYSREG(KMOD_SYSINFO, sysinfo, sysinfo_handler);
