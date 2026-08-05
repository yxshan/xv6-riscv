// 文件系统实现。整体分为五层：
//   + 块层：原始磁盘块的分配与释放
//   + 日志层：保证多步更新的崩溃一致性
//   + 文件层：inode 的分配、读写与元数据
//   + 目录层：把目录实现为“内容是指向其他 inode 列表”的特殊 inode
//   + 名字层：把 /usr/rtm/xv6/fs.c 这类路径解析为 inode
//
// 本文件包含低层文件系统操作；更上层的系统调用实现在 sysfile.c。
//
// File system implementation.  Five layers:
//   + Blocks: allocator for raw disk blocks.
//   + Log: crash recovery for multi-step updates.
//   + Files: inode allocator, reading, writing, metadata.
//   + Directories: inode with special contents (list of other inodes!)
//   + Names: paths like /usr/rtm/xv6/fs.c for convenient naming.
//
// This file contains the low-level file system manipulation
// routines.  The (higher-level) system call implementations
// are in sysfile.c.

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "file.h"

#define min(a, b) ((a) < (b) ? (a) : (b))
// 每个磁盘设备一份超级块，dev 直接作为下标；第二磁盘当前按只读挂载使用。
struct superblock sb[NDISK+1];

// 读取超级块：它描述文件系统的总体布局（块数、inode 数、日志位置等）。
static void
readsb(int dev, struct superblock *sb)
{
  struct buf *bp;

  bp = bread(dev, 1);
  memmove(sb, bp->data, sizeof(*sb));
  brelse(bp);
}

// 初始化文件系统：校验超级块魔数，恢复日志并回收孤立 inode。
void
fsinit(int dev) {
  readsb(dev, &sb[dev]);
  if(sb[dev].magic != FSMAGIC)
    panic("invalid file system");
  initlog(dev, &sb[dev]);
  ireclaim(dev);
}

// 把一个磁盘块清零。
static void
bzero(int dev, int bno)
{
  struct buf *bp;

  bp = bread(dev, bno);
  memset(bp->data, 0, BSIZE);
  log_write(bp);
  brelse(bp);
}

// Blocks.

// 在块位图中查找一个空闲块并分配，同时把块内容清零。
// 磁盘空间耗尽时返回 0。
static uint
balloc(uint dev)
{
  int b, bi, m;
  struct buf *bp;

  bp = 0;
  for(b = 0; b < sb[dev].size; b += BPB){
    bp = bread(dev, BBLOCK(b, sb[dev]));
    for(bi = 0; bi < BPB && b + bi < sb[dev].size; bi++){
      m = 1 << (bi % 8);
      if((bp->data[bi/8] & m) == 0){  // 该位为 0 表示块空闲？
        bp->data[bi/8] |= m;  // 标记为已使用
        log_write(bp);
        brelse(bp);
        bzero(dev, b + bi);
        return b + bi;
      }
    }
    brelse(bp);
  }
  printf("balloc: out of blocks\n");
  return 0;
}

// 释放磁盘块：把位图中对应位清 0。
static void
bfree(int dev, uint b)
{
  struct buf *bp;
  int bi, m;

  bp = bread(dev, BBLOCK(b, sb[dev]));
  bi = b % BPB;
  m = 1 << (bi % 8);
  if((bp->data[bi/8] & m) == 0)
    panic("freeing free block");
  bp->data[bi/8] &= ~m;
  log_write(bp);
  brelse(bp);
}

// Inodes。
//
// inode 描述一个文件（不包含文件名）：类型、大小、链接数、
// 以及存放文件内容的块号列表。内核维护 inode 表，
// 通过 ref 计数跟踪内存引用，通过睡眠锁串行化对 inode 的访问。
// 典型使用序列：iget() 取引用，ilock() 加锁，
// 读写后 iunlock()，最后 iput() 释放引用。
//
//
// An inode describes a single unnamed file.
// The inode disk structure holds metadata: the file's type,
// its size, the number of links referring to it, and the
// list of blocks holding the file's content.
//
// The inodes are laid out sequentially on disk at block
// sb.inodestart. Each inode has a number, indicating its
// position on the disk.
//
// The kernel keeps a table of in-use inodes in memory
// to provide a place for synchronizing access
// to inodes used by multiple processes. The in-memory
// inodes include book-keeping information that is
// not stored on disk: ip->ref and ip->valid.
//
// An inode and its in-memory representation go through a
// sequence of states before they can be used by the
// rest of the file system code.
//
// * Allocation: an inode is allocated if its type (on disk)
//   is non-zero. ialloc() allocates, and iput() frees if
//   the reference and link counts have fallen to zero.
//
// * Referencing in table: an entry in the inode table
//   is free if ip->ref is zero. Otherwise ip->ref tracks
//   the number of in-memory pointers to the entry (open
//   files and current directories). iget() finds or
//   creates a table entry and increments its ref; iput()
//   decrements ref.
//
// * Valid: the information (type, size, &c) in an inode
//   table entry is only correct when ip->valid is 1.
//   ilock() reads the inode from
//   the disk and sets ip->valid, while iput() clears
//   ip->valid if ip->ref has fallen to zero.
//
// * Locked: file system code may only examine and modify
//   the information in an inode and its content if it
//   has first locked the inode.
//
// Thus a typical sequence is:
//   ip = iget(dev, inum)
//   ilock(ip)
//   ... examine and modify ip->xxx ...
//   iunlock(ip)
//   iput(ip)
//
// ilock() is separate from iget() so that system calls can
// get a long-term reference to an inode (as for an open file)
// and only lock it for short periods (e.g., in read()).
// The separation also helps avoid deadlock and races during
// pathname lookup. iget() increments ip->ref so that the inode
// stays in the table and pointers to it remain valid.
//
// Many internal file system functions expect the caller to
// have locked the inodes involved; this lets callers create
// multi-step atomic operations.
//
// The itable.lock spin-lock protects the allocation of itable
// entries. Since ip->ref indicates whether an entry is free,
// and ip->dev and ip->inum indicate which i-node an entry
// holds, one must hold itable.lock while using any of those fields.
//
// An ip->lock sleep-lock protects all ip-> fields other than ref,
// dev, and inum.  One must hold ip->lock in order to
// read or write that inode's ip->valid, ip->size, ip->type, &c.

struct {
  struct spinlock lock;
  struct inode inode[NINODE];
} itable;

void
iinit()
{
  int i = 0;
  
  initlock(&itable.lock, "itable");
  for(i = 0; i < NINODE; i++) {
    initsleeplock(&itable.inode[i].lock, "inode");
  }
}

static struct inode* iget(uint dev, uint inum);

// 在磁盘上分配一个 inode：把磁盘 inode 的 type 设为非 0，
// 并返回一个已引用但未加锁的内存 inode。
// 没有空闲 inode 时返回 0。
struct inode*
ialloc(uint dev, short type)
{
  int inum;
  struct buf *bp;
  struct dinode *dip;

  for(inum = 1; inum < sb[dev].ninodes; inum++){
    bp = bread(dev, IBLOCK(inum, sb[dev]));
    dip = (struct dinode*)bp->data + inum%IPB;
    if(dip->type == 0){  // 找到一个空闲 inode
      memset(dip, 0, sizeof(*dip));
      dip->type = type;
      log_write(bp);   // 通过日志在磁盘上标记分配
      brelse(bp);
      return iget(dev, inum);
    }
    brelse(bp);
  }
  printf("ialloc: no inodes\n");
  return 0;
}

// 把修改后的内存 inode 写回磁盘。
// 每次修改了需要落盘的 ip->xxx 字段后都必须调用。
// 调用者必须持有 ip->lock。
void
iupdate(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  bp = bread(ip->dev, IBLOCK(ip->inum, sb[ip->dev]));
  dip = (struct dinode*)bp->data + ip->inum%IPB;
  dip->type = ip->type;
  dip->major = ip->major;
  dip->minor = ip->minor;
  dip->mode = ip->mode;
  dip->uid = ip->uid;
  dip->gid = ip->gid;
  dip->nlink = ip->nlink;
  dip->size = ip->size;
  memmove(dip->addrs, ip->addrs, sizeof(ip->addrs));
  log_write(bp);
  brelse(bp);
}

// 在 inode 表中查找或创建 inum 对应的内存 inode，并增加引用计数。
// 不加锁、不读磁盘；返回的 inode 需要 ilock() 后使用。
static struct inode*
iget(uint dev, uint inum)
{
  struct inode *ip, *empty;

  acquire(&itable.lock);

  // 已在表中：直接增加引用计数。
  empty = 0;
  for(ip = &itable.inode[0]; ip < &itable.inode[NINODE]; ip++){
    if(ip->ref > 0 && ip->dev == dev && ip->inum == inum){
      ip->ref++;
      release(&itable.lock);
      return ip;
    }
    if(empty == 0 && ip->ref == 0)    // Remember empty slot.
      empty = ip;
  }

  // 未在表中：回收一个空槽位。
  if(empty == 0)
    panic("iget: no inodes");

  ip = empty;
  ip->dev = dev;
  ip->inum = inum;
  ip->ref = 1;
  ip->valid = 0;
  release(&itable.lock);

  return ip;
}

// 增加 inode 引用计数，并返回 ip，支持 ip = idup(ip1) 的写法。
struct inode*
idup(struct inode *ip)
{
  acquire(&itable.lock);
  ip->ref++;
  release(&itable.lock);
  return ip;
}

// 锁定 inode；若磁盘内容尚未读入（valid == 0），先读入。
// 锁与“读磁盘”分离，使系统调用可以长期持有引用、短时间加锁。
void
ilock(struct inode *ip)
{
  struct buf *bp;
  struct dinode *dip;

  if(ip == 0 || ip->ref < 1)
    panic("ilock");

  acquiresleep(&ip->lock);

  if(ip->valid == 0){
    bp = bread(ip->dev, IBLOCK(ip->inum, sb[ip->dev]));
    dip = (struct dinode*)bp->data + ip->inum%IPB;
    ip->type = dip->type;
    ip->major = dip->major;
    ip->minor = dip->minor;
    ip->mode = dip->mode;
    ip->uid = dip->uid;
    ip->gid = dip->gid;
    ip->nlink = dip->nlink;
    ip->size = dip->size;
    memmove(ip->addrs, dip->addrs, sizeof(ip->addrs));
    brelse(bp);
    ip->valid = 1;
    if(ip->type == 0)
      panic("ilock: no type");
  }
}

// 解锁 inode。
void
iunlock(struct inode *ip)
{
  if(ip == 0 || !holdingsleep(&ip->lock) || ip->ref < 1)
    panic("iunlock");

  releasesleep(&ip->lock);
}

// 释放一个 inode 内存引用。
// 若这是最后一个引用且链接数已为 0（文件已被 unlink），
// 同时截断内容并释放磁盘 inode。调用 iput() 必须在日志事务内，
// 因为它可能触发磁盘释放。
void
iput(struct inode *ip)
{
  acquire(&itable.lock);

  if(ip->ref == 1 && ip->valid && ip->nlink == 0){
    // inode 无链接且无其他引用：释放所有数据块并回收 inode。

    // ref == 1 意味着没有其他进程持锁，acquiresleep() 不会阻塞。
    acquiresleep(&ip->lock);

    release(&itable.lock);

    if(ip->fifo){
      kfree(ip->fifo);
      ip->fifo = 0;
    }
    itrunc(ip);
    ip->type = 0;
    iupdate(ip);
    ip->valid = 0;

    releasesleep(&ip->lock);

    acquire(&itable.lock);
  }

  ip->ref--;
  release(&itable.lock);
}

// 常见组合：先解锁，再释放引用。
void
iunlockput(struct inode *ip)
{
  iunlock(ip);
  iput(ip);
}

void
ireclaim(int dev)
{
  for (int inum = 1; inum < sb[dev].ninodes; inum++) {
    struct inode *ip = 0;
    struct buf *bp = bread(dev, IBLOCK(inum, sb[dev]));
    struct dinode *dip = (struct dinode *)bp->data + inum % IPB;
    if (dip->type != 0 && dip->nlink == 0) {  // is an orphaned inode
      printf("ireclaim: orphaned inode %d\n", inum);
      ip = iget(dev, inum);
    }
    brelse(bp);
    if (ip) {
      begin_op();
      ilock(ip);
      iunlock(ip);
      iput(ip);
      end_op();
    }
  }
}

// Inode content
//
// The content (data) associated with each inode is stored
// in blocks on the disk. The first NDIRECT block numbers
// are listed in ip->addrs[].  The next NINDIRECT blocks are
// listed in block ip->addrs[NDIRECT].

// 返回 inode 中第 bn 个数据块的磁盘块号；不存在时按需分配。
// 支持直接块和一级间接块两种方式。磁盘空间不足返回 0。
static uint
bmap(struct inode *ip, uint bn)
{
  uint addr, *a;
  struct buf *bp;

  if(bn < NDIRECT){
    // 前 NDIRECT 个块号直接记录在 inode 中。
    if((addr = ip->addrs[bn]) == 0){
      addr = balloc(ip->dev);
      if(addr == 0)
        return 0;
      ip->addrs[bn] = addr;
    }
    return addr;
  }
  bn -= NDIRECT;

  if(bn < NINDIRECT){
    // 超过直接块后，通过间接块数组索引，必要时先分配间接块。
    if((addr = ip->addrs[NDIRECT]) == 0){
      addr = balloc(ip->dev);
      if(addr == 0)
        return 0;
      ip->addrs[NDIRECT] = addr;
    }
    bp = bread(ip->dev, addr);
    a = (uint*)bp->data;
    if((addr = a[bn]) == 0){
      addr = balloc(ip->dev);
      if(addr){
        a[bn] = addr;
        log_write(bp);
      }
    }
    brelse(bp);
    return addr;
  }

  panic("bmap: out of range");
}

// 截断 inode：释放所有数据块并清零大小。调用者必须持有 ip->lock。
void
itrunc(struct inode *ip)
{
  int i, j;
  struct buf *bp;
  uint *a;

  for(i = 0; i < NDIRECT; i++){
    if(ip->addrs[i]){
      bfree(ip->dev, ip->addrs[i]);
      ip->addrs[i] = 0;
    }
  }

  if(ip->addrs[NDIRECT]){
    bp = bread(ip->dev, ip->addrs[NDIRECT]);
    a = (uint*)bp->data;
    for(j = 0; j < NINDIRECT; j++){
      if(a[j])
        bfree(ip->dev, a[j]);
    }
    brelse(bp);
    bfree(ip->dev, ip->addrs[NDIRECT]);
    ip->addrs[NDIRECT] = 0;
  }

  ip->size = 0;
  iupdate(ip);
}

// Copy stat information from inode.
// Caller must hold ip->lock.
void
stati(struct inode *ip, struct stat *st)
{
  st->dev = ip->dev;
  st->ino = ip->inum;
  st->type = ip->type;
  st->nlink = ip->nlink;
  st->mode = ip->mode;
  st->uid = ip->uid;
  st->gid = ip->gid;
  st->size = ip->size;
}

// 检查当前进程是否对 inode 拥有 want 指定的权限。
// want 使用与权限位相同的位布局：4=读，2=写，1=执行。
// root（euid 0）可以绕过读/写/执行检查，简化教学实现。
int
iaccess(struct inode *ip, int want)
{
  struct proc *p = myproc();
  ushort perm;

  if(p->euid == 0)
    return 0;
  if(p->euid == ip->uid)
    perm = (ip->mode >> 6) & 7;
  else if(p->egid == ip->gid)
    perm = (ip->mode >> 3) & 7;
  else
    perm = ip->mode & 7;
  return (perm & want) == want ? 0 : -1;
}

// 从 inode 读取数据。调用者必须持有 ip->lock。
// user_dst 为 1 时 dst 是用户地址，否则是内核地址。
int
readi(struct inode *ip, int user_dst, uint64 dst, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  // 越界或整数溢出的偏移直接拒绝。
  if(off > ip->size || off + n < off)
    return 0;
  if(off + n > ip->size)
    n = ip->size - off;

  for(tot=0; tot<n; tot+=m, off+=m, dst+=m){
    uint addr = bmap(ip, off/BSIZE);
    if(addr == 0)
      break;
    bp = bread(ip->dev, addr);
    m = min(n - tot, BSIZE - off%BSIZE);
    if(either_copyout(user_dst, dst, bp->data + (off % BSIZE), m) == -1) {
      brelse(bp);
      tot = -1;
      break;
    }
    brelse(bp);
  }
  return tot;
}

// 向 inode 写入数据。调用者必须持有 ip->lock。
// user_src 为 1 时 src 是用户地址，否则是内核地址。
// 返回实际写入字节数；小于请求值说明出现错误。
int
writei(struct inode *ip, int user_src, uint64 src, uint off, uint n)
{
  uint tot, m;
  struct buf *bp;

  if(off > ip->size || off + n < off)
    return -1;
  if(off + n > MAXFILE*BSIZE)
    return -1;

  for(tot=0; tot<n; tot+=m, off+=m, src+=m){
    uint addr = bmap(ip, off/BSIZE);
    if(addr == 0)
      break;
    bp = bread(ip->dev, addr);
    m = min(n - tot, BSIZE - off%BSIZE);
    if(either_copyin(bp->data + (off % BSIZE), user_src, src, m) == -1) {
      brelse(bp);
      break;
    }
    log_write(bp);
    brelse(bp);
  }

  if(off > ip->size)
    ip->size = off;

  // write the i-node back to disk even if the size didn't change
  // because the loop above might have called bmap() and added a new
  // block to ip->addrs[].
  iupdate(ip);

  return tot;
}

// Directories

int
namecmp(const char *s, const char *t)
{
  return strncmp(s, t, DIRSIZ);
}

// 在目录中查找名为 name 的目录项；
// 找到时返回对应 inode，并通过 *poff 返回目录项偏移。
struct inode*
dirlookup(struct inode *dp, char *name, uint *poff)
{
  uint off, inum;
  struct dirent de;

  if(dp->type != T_DIR)
    panic("dirlookup not DIR");

  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlookup read");
    if(de.inum == 0)
      continue;
    if(namecmp(name, de.name) == 0){
      // entry matches path element
      if(poff)
        *poff = off;
      inum = de.inum;
      return iget(dp->dev, inum);
    }
  }

  return 0;
}

// 向目录 dp 写入目录项 (name, inum)。
// 成功返回 0，失败（如磁盘块不足）返回 -1。
int
dirlink(struct inode *dp, char *name, uint inum)
{
  int off;
  struct dirent de;
  struct inode *ip;

  // 目录中不允许出现重名。
  if((ip = dirlookup(dp, name, 0)) != 0){
    iput(ip);
    return -1;
  }

  // 查找空闲目录项位置。
  for(off = 0; off < dp->size; off += sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("dirlink read");
    if(de.inum == 0)
      break;
  }

  strncpy(de.name, name, DIRSIZ);
  de.inum = inum;
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    return -1;

  return 0;
}

// 路径解析

// Copy the next path element from path into name.
// Return a pointer to the element following the copied one.
// The returned path has no leading slashes,
// so the caller can check *path=='\0' to see if the name is the last one.
// If no name to remove, return 0.
//
// Examples:
//   skipelem("a/bb/c", name) = "bb/c", setting name = "a"
//   skipelem("///a//bb", name) = "bb", setting name = "a"
//   skipelem("a", name) = "", setting name = "a"
//   skipelem("", name) = skipelem("////", name) = 0
//
static char*
skipelem(char *path, char *name)
{
  char *s;
  int len;

  while(*path == '/')
    path++;
  if(*path == 0)
    return 0;
  s = path;
  while(*path != '/' && *path != 0)
    path++;
  len = path - s;
  if(len >= DIRSIZ)
    memmove(name, s, DIRSIZ);
  else {
    memmove(name, s, len);
    name[len] = 0;
  }
  while(*path == '/')
    path++;
  return path;
}

// 简单的只读挂载：/disk1 前缀映射到第二块磁盘的根 inode。
// 返回设备号，并把 *rest 调整为挂载点之后的路径。
static int
mount_root(char *path, char **rest)
{
  if(strncmp(path, "/disk1", 6) == 0 && (path[6] == 0 || path[6] == '/')){
    *rest = path + 6;
    return DISK1DEV;
  }
  *rest = path;
  return ROOTDEV;
}

// 解析路径并返回对应 inode。
// nameiparent 非 0 时返回父目录 inode，并把最后一段路径元素
// 复制到 name（至少 DIRSIZ 字节）。
// 必须位于日志事务内，因为解析过程中会调用 iput()。
static struct inode*
namex(char *path, int nameiparent, char *name)
{
  struct inode *ip, *next;
  int depth = 0;
  int dev;
  char *mntpath;

  dev = mount_root(path, &mntpath);
  path = mntpath;

  // 绝对路径从根 inode 开始，相对路径从当前目录开始。
  if(*path == '/' || dev != ROOTDEV)
    ip = iget(dev, ROOTINO);
  else
    ip = idup(myproc()->cwd);

  while((path = skipelem(path, name)) != 0){
    ilock(ip);
    // 路径中间元素必须是目录，否则解析失败。
    if(ip->type != T_DIR){
      iunlockput(ip);
      return 0;
    }
    // 遍历目录必须拥有执行权限，否则拒绝继续解析路径。
    if(iaccess(ip, 1) < 0){
      iunlockput(ip);
      return 0;
    }
    if(nameiparent && *path == '\0'){
      // nameiparent 模式：在最后一级之前停下，返回父目录。
      iunlock(ip);
      return ip;
    }
    if((next = dirlookup(ip, name, 0)) == 0){
      iunlockput(ip);
      return 0;
    }
    // 先释放父目录，再锁子目录，避免 "." / ".." 等自引用目录死锁。
    iunlock(ip);
    ilock(next);
    if(next->type == T_SYMLINK){
      char target[MAXPATH];
      int n = next->size;

      if(n >= MAXPATH)
        n = MAXPATH - 1;
      if(readi(next, 0, (uint64)target, 0, n) != n){
        iunlockput(next);
        iput(ip);
        return 0;
      }
      target[n] = 0;
      iunlockput(next);

      if(++depth > 8){
        iput(ip);
        return 0;
      }

      if(target[0] == '/'){
        // 绝对符号链接：从对应磁盘根目录重新解析。
        iput(ip);
        dev = mount_root(target, &mntpath);
        ip = iget(dev, ROOTINO);
        path = mntpath;
      } else {
        // 相对符号链接：继续从当前目录解析，ip 已解锁。
        path = target;
      }
      continue;
    }
    iunlock(next);
    iput(ip);
    ip = next;
  }
  if(nameiparent){
    iput(ip);
    return 0;
  }
  return ip;
}

struct inode*
namei(char *path)
{
  char name[DIRSIZ];
  return namex(path, 0, name);
}

struct inode*
nameiparent(char *path, char *name)
{
  return namex(path, 1, name);
}
