#define NPROC        64  // maximum number of processes
#define NCPU          8  // maximum number of CPUs
#define NOFILE       16  // open files per process
#define NFILE       100  // open files per system
#define NINODE       50  // maximum number of active i-nodes
#define NDEV         10  // maximum major device number
#define ROOTDEV       1  // device number of file system root disk
#define DISK1DEV      2  // device number of second disk
#define SWAPDEV       3  // device number of raw swap disk
#define NDISK         3  // number of virtio block devices
#define MAXARG       32  // max exec arguments
#define MAXOPBLOCKS  10  // max # of blocks any FS op writes
#define LOGBLOCKS    (MAXOPBLOCKS*3)  // max data blocks in on-disk log
#define NBUF         (MAXOPBLOCKS*3)  // size of disk block cache
#define FSSIZE       4096  // size of file system in blocks
#define MAXPATH      128   // maximum file path name
#define USERSTACK    1     // user stack pages
#define TICKS_PER_SEC 10   // QEMU 时钟中断频率

// clone 系统调用 flags，取值与 Linux 常用位保持一致。
#define CLONE_VM       0x00000100  // 共享地址空间
#define CLONE_FS       0x00000200  // 共享文件系统上下文（cwd）
#define CLONE_FILES    0x00000400  // 共享文件描述符表
#define CLONE_THREAD   0x00010000  // 加入调用者的线程组
