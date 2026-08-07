// 基础伪设备：/dev/zero 返回零字节，/dev/null 丢弃写入并返回 EOF。
#include "types.h"
#include "param.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"

static int
zeroread(struct file *f, int user_dst, uint64 dst, int n)
{
  char zeros[32] = { 0 };
  int i = 0;

  while(i < n){
    int nn = n - i;
    if(nn > (int)sizeof(zeros))
      nn = sizeof(zeros);
    if(either_copyout(user_dst, dst + i, zeros, nn) < 0)
      break;
    i += nn;
  }
  return i;
}

static int
zerowrite(struct file *f, int user_src, uint64 src, int n)
{
  return n;
}

static int
nullread(struct file *f, int user_dst, uint64 dst, int n)
{
  return 0;
}

static int
nullwrite(struct file *f, int user_src, uint64 src, int n)
{
  return n;
}

void
pseudoinit(void)
{
  devsw[ZERO_DEV].read = zeroread;
  devsw[ZERO_DEV].write = zerowrite;
  devsw[NULL_DEV].read = nullread;
  devsw[NULL_DEV].write = nullwrite;
}
