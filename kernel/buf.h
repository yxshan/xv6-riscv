// 磁盘块缓冲区：缓存一个磁盘块的内容。
// valid 表示是否已从磁盘读入；refcnt 表示正在使用的引用数；
// prev/next 用于 LRU 链表；sleeplock 保证同一时刻只有一个进程使用。
struct buf {
  int valid;   // 是否已从磁盘读入数据？
  int disk;    // 磁盘是否“拥有”该缓冲区？
  uint dev;    // 所属设备
  uint blockno; // 磁盘块号
  struct sleeplock lock; // 缓冲区独占锁
  uint refcnt;  // 引用计数
  struct buf *prev; // LRU 链表前驱
  struct buf *next; // LRU 链表后继
  uchar data[BSIZE]; // 块内容
};
