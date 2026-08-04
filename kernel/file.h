// 打开的文件抽象。
// 一个 struct file 可能同时被多个文件描述符引用（fork/dup 共享），
// 因此用 ref 计数；type 决定读写操作具体分派到管道、inode 还是设备。
struct file {
  enum { FD_NONE, FD_PIPE, FD_INODE, FD_DEVICE } type;
  int ref; // 引用计数
  char readable;
  char writable;
  struct pipe *pipe; // FD_PIPE
  struct inode *ip;  // FD_INODE and FD_DEVICE
  uint off;          // FD_INODE
  short major;       // FD_DEVICE
};

#define major(dev)  ((dev) >> 16 & 0xFFFF)
#define minor(dev)  ((dev) & 0xFFFF)
#define	mkdev(m,n)  ((uint)((m)<<16| (n)))

// 内存中的 inode 副本。
// 它包含磁盘 inode 的元数据（type/size/addrs 等），
// 以及只存在于内存中的 ref 计数、valid 标志和睡眠锁。
struct inode {
  uint dev;           // Device number
  uint inum;          // Inode number
  int ref;            // Reference count
  struct sleeplock lock; // 保护下面所有字段
  int valid;          // 是否已从磁盘读入？

  short type;         // copy of disk inode
  short major;
  short minor;
  short nlink;
  uint size;
  uint addrs[NDIRECT+1];
};

// 设备号到设备读写函数的映射表。
// 设备模块通过这些回调接入文件描述符层，类似 Linux 的 file_operations。
struct devsw {
  int (*open)(struct file*);
  int (*read)(struct file*, int, uint64, int);
  int (*write)(struct file*, int, uint64, int);
  int (*close)(struct file*);
};

extern struct devsw devsw[];

#define CONSOLE 1
