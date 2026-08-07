// 磁盘日志层。
//
// 每个磁盘设备拥有独立的日志区，但文件系统事务由全局计数器统一管理：
// 一个事务内通常只写一个设备，end_op() 在最后一个操作结束时提交所有
// 有脏日志的设备。这样无需在几十处 begin_op/end_op 调用点传递设备号。
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

struct logheader {
  int n;
  int block[LOGBLOCKS];
};

struct log {
  struct spinlock lock;
  int start;
  int dev;
  struct logheader lh;
};

static struct log logs[NDISK+1];

struct {
  struct spinlock lock;
  int outstanding;
  int committing;
} trans;

static int transinit;

static void recover_from_log(struct log *log);
static void commit(struct log *log);

void
initlog(int dev, struct superblock *sb)
{
  if(!transinit){
    initlock(&trans.lock, "logtrans");
    transinit = 1;
  }
  initlock(&logs[dev].lock, "log");
  logs[dev].start = sb->logstart;
  logs[dev].dev = dev;
  recover_from_log(&logs[dev]);
}

// 把日志中记录的块复制回它们在磁盘上的原始位置。
static void
install_trans(int recovering, struct log *log)
{
  int tail;

  for(tail = 0; tail < log->lh.n; tail++){
    if(recovering)
      printf("recovering tail %d dst %d\n", tail, log->lh.block[tail]);
    struct buf *lbuf = bread(log->dev, log->start + tail + 1);
    struct buf *dbuf = bread(log->dev, log->lh.block[tail]);
    memmove(dbuf->data, lbuf->data, BSIZE);
    bwrite(dbuf);
    if(recovering == 0)
      bunpin(dbuf);
    brelse(lbuf);
    brelse(dbuf);
  }
}

// 从磁盘读取日志头到内存。
static void
read_head(struct log *log)
{
  struct buf *buf = bread(log->dev, log->start);
  struct logheader *lh = (struct logheader *)buf->data;
  int i;

  log->lh.n = lh->n;
  for(i = 0; i < log->lh.n; i++)
    log->lh.block[i] = lh->block[i];
  brelse(buf);
}

// 把内存中的日志头写回磁盘。
static void
write_head(struct log *log)
{
  struct buf *buf = bread(log->dev, log->start);
  struct logheader *hb = (struct logheader *)buf->data;
  int i;

  hb->n = log->lh.n;
  for(i = 0; i < log->lh.n; i++)
    hb->block[i] = log->lh.block[i];
  bwrite(buf);
  brelse(buf);
}

static void
recover_from_log(struct log *log)
{
  read_head(log);
  install_trans(1, log);
  log->lh.n = 0;
  write_head(log);
}

void
begin_op(void)
{
  acquire(&trans.lock);
  while(1){
    int wait = 0;

    if(trans.committing){
      wait = 1;
    } else {
      for(int d = ROOTDEV; d < ROOTDEV + NDISK - 1; d++){
        acquire(&logs[d].lock);
        if(logs[d].lh.n + (trans.outstanding + 1) * MAXOPBLOCKS > LOGBLOCKS)
          wait = 1;
        release(&logs[d].lock);
        if(wait)
          break;
      }
    }
    if(wait)
      sleep(&trans, &trans.lock);
    else
      break;
  }
  trans.outstanding++;
  release(&trans.lock);
}

void
end_op(void)
{
  int do_commit = 0;

  acquire(&trans.lock);
  trans.outstanding--;
  if(trans.committing)
    panic("log.committing");
  if(trans.outstanding == 0){
    do_commit = 1;
    trans.committing = 1;
  } else {
    wakeup(&trans);
  }
  release(&trans.lock);

  if(do_commit){
    for(int d = ROOTDEV; d < ROOTDEV + NDISK - 1; d++){
      if(logs[d].lh.n > 0)
        commit(&logs[d]);
    }
    acquire(&trans.lock);
    trans.committing = 0;
    wakeup(&trans);
    release(&trans.lock);
  }
}

// 把缓存中修改过的块复制到磁盘日志区域。
static void
write_log(struct log *log)
{
  int tail;

  for(tail = 0; tail < log->lh.n; tail++){
    struct buf *to = bread(log->dev, log->start + tail + 1);
    struct buf *from = bread(log->dev, log->lh.block[tail]);
    memmove(to->data, from->data, BSIZE);
    bwrite(to);
    brelse(from);
    brelse(to);
  }
}

static void
commit(struct log *log)
{
  if(log->lh.n > 0){
    write_log(log);
    write_head(log);
    install_trans(0, log);
    log->lh.n = 0;
    write_head(log);
  }
}

void
log_write(struct buf *b)
{
  int i, dev;

  dev = b->dev;
  if(dev < ROOTDEV || dev >= ROOTDEV + NDISK)
    panic("log_write: bad dev");

  acquire(&trans.lock);
  if(trans.outstanding < 1)
    panic("log_write outside of trans");
  release(&trans.lock);

  acquire(&logs[dev].lock);
  if(logs[dev].lh.n >= LOGBLOCKS)
    panic("too big a transaction");

  for(i = 0; i < logs[dev].lh.n; i++){
    if(logs[dev].lh.block[i] == b->blockno)
      break;
  }
  logs[dev].lh.block[i] = b->blockno;
  if(i == logs[dev].lh.n){
    bpin(b);
    logs[dev].lh.n++;
  }
  release(&logs[dev].lock);
}
