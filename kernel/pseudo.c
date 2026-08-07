// 基础伪设备：/dev/zero 返回零字节，/dev/null 丢弃写入并返回 EOF。
#include "types.h"
#include "param.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "time.h"

#define RTC_EPOCH_SEC 1767225600UL // 2026-01-01 00:00:00 UTC

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

static void
clock_now(struct timespec *ts, int realtime)
{
  uint t;

  acquire(&tickslock);
  t = ticks;
  release(&tickslock);
  ts->tv_sec = t / TICKS_PER_SEC;
  ts->tv_nsec = (t % TICKS_PER_SEC) * (1000000000L / TICKS_PER_SEC);
  if(realtime)
    ts->tv_sec += RTC_EPOCH_SEC;
}

static int
clockread(struct file *f, int user_dst, uint64 dst, int n)
{
  struct timespec ts;
  int nn = n;

  clock_now(&ts, 0);
  if(nn > (int)sizeof(ts))
    nn = sizeof(ts);
  if(nn <= 0)
    return 0;
  if(either_copyout(user_dst, dst, (char*)&ts, nn) < 0)
    return -1;
  return nn;
}

static int
rtcread(struct file *f, int user_dst, uint64 dst, int n)
{
  struct timespec ts;
  int nn = n;

  clock_now(&ts, 1);
  if(nn > (int)sizeof(ts))
    nn = sizeof(ts);
  if(nn <= 0)
    return 0;
  if(either_copyout(user_dst, dst, (char*)&ts, nn) < 0)
    return -1;
  return nn;
}

void
pseudoinit(void)
{
  devsw[ZERO_DEV].read = zeroread;
  devsw[ZERO_DEV].write = zerowrite;
  devsw[NULL_DEV].read = nullread;
  devsw[NULL_DEV].write = nullwrite;
  devsw[CLOCK_DEV].read = clockread;
  devsw[RTC_DEV].read = rtcread;
}
