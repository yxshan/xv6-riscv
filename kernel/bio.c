// 块缓存。
//
// 块缓存是 buf 结构组成的双向链表，保存磁盘块内容的缓存副本。
// 把磁盘块缓存在内存中有两个作用：
// 1. 减少磁盘读写次数；
// 2. 让多个进程对同一个磁盘块的访问通过缓存进行同步。
//
// 使用方式：
// * 读取某个磁盘块：bread()，返回已加锁的缓冲区；
// * 修改缓冲区内容后：bwrite() 写回磁盘；
// * 使用完毕：brelse() 释放；
// * brelse() 之后不得再使用该缓冲区。
// * 同一时刻只允许一个进程使用某个缓冲区，因此不要长时间持有。


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head;
} bcache;

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.lock, "bcache");

  // 把所有缓冲区串成双向链表，初始时全部空闲。
  bcache.head.prev = &bcache.head;
  bcache.head.next = &bcache.head;
  for(b = bcache.buf; b < bcache.buf+NBUF; b++){
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    initsleeplock(&b->lock, "buffer");
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
}

// 在缓存中查找设备 dev 的 blockno 块。
// 未命中时回收一个最久未使用的空闲缓冲区。
// 两种情况下都返回持有睡眠锁的缓冲区。
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;

  acquire(&bcache.lock);

  // 缓存命中：增加引用计数并等待该缓冲区可独占使用。
  for(b = bcache.head.next; b != &bcache.head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // 缓存未命中：从链表尾部（最久未使用）开始回收 refcnt == 0 的缓冲区。
  for(b = bcache.head.prev; b != &bcache.head; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&bcache.lock);
      acquiresleep(&b->lock);
      return b;
    }
  }
  panic("bget: no buffers");
}

// 返回已加锁且内容有效的磁盘块缓冲区；
// 若块尚未读入缓存，则发起真正的磁盘读。
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// 把缓冲区内容写回磁盘。调用者必须持有 b 的睡眠锁。
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// 释放缓冲区，并把它移到最近使用列表的头部。
// 当引用计数归零时，它成为后续回收的候选者。
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  acquire(&bcache.lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    b->next = bcache.head.next;
    b->prev = &bcache.head;
    bcache.head.next->prev = b;
    bcache.head.next = b;
  }
  
  release(&bcache.lock);
}

// 固定缓冲区：引用计数 +1，防止它在操作期间被回收。
void
bpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt++;
  release(&bcache.lock);
}

// 解除固定：引用计数 -1。
void
bunpin(struct buf *b) {
  acquire(&bcache.lock);
  b->refcnt--;
  release(&bcache.lock);
}

