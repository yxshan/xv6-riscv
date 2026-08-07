// VFS 挂载层。
//
// 挂载表把“某个目录 inode（挂载点）”映射到“另一个设备的根 inode”。
// 路径解析经过挂载点时，会透明地切换到被挂载文件系统的根；
// 在被挂载文件系统的根上访问 ".." 时，会跨回挂载点在父文件系统中的父目录。
//
// 设计上保持对 xv6 核心文件系统的最小侵入：
// fs.c 的 namex() 只调用 mount_enter() 和 mount_dotdot() 两个挂载点，
// 其余 inode、目录、块和日志逻辑仍由原有代码完成。
#include "types.h"
#include "param.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "stat.h"

#define NMOUNT 8

struct mount {
  int used;
  int dev;
  char path[MAXPATH];
  struct inode *mountpoint;  // 父文件系统中的挂载点目录
  struct inode *root;        // 被挂载文件系统的根 inode
};

static struct mount mounts[NMOUNT];
static struct sleeplock mounts_lock;

void
mountinit(void)
{
  initsleeplock(&mounts_lock, "mounts");
}

static int
same_inode(struct inode *a, struct inode *b)
{
  return a != 0 && b != 0 && a->dev == b->dev && a->inum == b->inum;
}

// namex() 在整条路径解析期间持有挂载锁，避免挂载/卸载与路径穿越竞争。
void
mount_acquire(void)
{
  // 首个进程创建前（userinit 的 namei("/")）还没有当前进程，
  // 此时挂载表必然为空，跳过睡眠锁以避免 myproc() 解引用空指针。
  if(myproc())
    acquiresleep(&mounts_lock);
}

void
mount_release(void)
{
  if(myproc())
    releasesleep(&mounts_lock);
}

// 如果 *ipp 是挂载点，切换到被挂载文件系统的根。
// 调用者持有挂载锁；传入的 inode 必须未被锁定。
int
mount_enter(struct inode **ipp)
{
  struct inode *ip = *ipp;
  struct inode *root = 0;

  for(int i = 0; i < NMOUNT; i++){
    if(mounts[i].used && same_inode(mounts[i].mountpoint, ip)){
      root = idup(mounts[i].root);
      break;
    }
  }
  if(root == 0)
    return 0;
  iput(ip);
  *ipp = root;
  return 1;
}

// 当 ip 是被挂载文件系统的根、且当前路径元素是 ".." 时，
// 返回挂载点在父文件系统中的父目录。
// 调用者持有挂载锁；ip 必须已锁定。
// 返回 0 表示 ip 不是挂载根；返回 1 表示已跨出挂载边界并释放 ip；
// 返回 -1 表示解析父目录失败。
int
mount_dotdot(struct inode *ip, struct inode **parent)
{
  struct inode *mp;

  *parent = 0;
  for(int i = 0; i < NMOUNT; i++){
    if(mounts[i].used && same_inode(mounts[i].root, ip)){
      mp = idup(mounts[i].mountpoint);
      iunlock(ip);
      iput(ip);
      ilock(mp);
      *parent = dirlookup(mp, "..", 0);
      iunlockput(mp);
      return *parent == 0 ? -1 : 1;
    }
  }
  return 0;
}

// 把 dev 设备的根文件系统挂载到 mountpoint 目录。
// 调用者持有 mountpoint 的 inode 引用；成功后挂载表自己再保留一份引用。
int
mount_add(int dev, struct inode *mountpoint, char *path)
{
  struct inode *root;
  int i;

  if(dev < ROOTDEV || dev >= ROOTDEV + NDISK)
    return -1;
  if(mountpoint == 0 || path == 0 || path[0] == 0 ||
     mountpoint->type != T_DIR || mountpoint->inum == ROOTINO)
    return -1;
  if(fsvalid(dev) == 0)
    return -1;

  root = iget(dev, ROOTINO);
  ilock(root);
  if(root->type != T_DIR){
    iunlockput(root);
    return -1;
  }
  iunlock(root);

  acquiresleep(&mounts_lock);
  for(i = 0; i < NMOUNT; i++){
    if(mounts[i].used &&
       (mounts[i].dev == dev || strncmp(mounts[i].path, path, MAXPATH) == 0))
      break;
  }
  if(i < NMOUNT && mounts[i].used){
    releasesleep(&mounts_lock);
    iput(root);
    return -1;
  }
  for(i = 0; i < NMOUNT; i++){
    if(!mounts[i].used)
      break;
  }
  if(i == NMOUNT){
    releasesleep(&mounts_lock);
    iput(root);
    return -1;
  }

  mounts[i].used = 1;
  mounts[i].dev = dev;
  safestrcpy(mounts[i].path, path, MAXPATH);
  mounts[i].mountpoint = idup(mountpoint);
  mounts[i].root = root;  // 挂载表接管这次 iget() 引用
  releasesleep(&mounts_lock);
  return 0;
}

// 卸载挂载点。ip 可以是挂载点 inode，也可以是当前挂载根 inode。
int
mount_remove(char *path)
{
  struct inode *mp;
  struct inode *root;
  int idx = -1;

  if(path == 0 || path[0] == 0)
    return -1;
  acquiresleep(&mounts_lock);
  for(int i = 0; i < NMOUNT; i++){
    if(mounts[i].used && strncmp(mounts[i].path, path, MAXPATH) == 0){
      idx = i;
      break;
    }
  }
  if(idx < 0){
    releasesleep(&mounts_lock);
    return -1;
  }

  mp = mounts[idx].mountpoint;
  root = mounts[idx].root;
  mounts[idx].used = 0;
  mounts[idx].dev = 0;
  mounts[idx].path[0] = 0;
  mounts[idx].mountpoint = 0;
  mounts[idx].root = 0;
  releasesleep(&mounts_lock);

  iput(mp);
  iput(root);
  return 0;
}

// 系统启动时把第二块磁盘默认挂载到 /disk1。
// fs.img 在 mkfs 阶段创建空的 /disk1 目录，这里只登记挂载关系。
void
mount_default(void)
{
  struct inode *mp;

  begin_op();
  if((mp = namei("/disk1")) != 0){
    if(mp->type == T_DIR)
      mount_add(DISK1DEV, mp, "/disk1");
    iput(mp);
  }
  end_op();
}

uint64
sys_mount(void)
{
  char path[MAXPATH];
  struct inode *mp;
  int dev;
  int r;

  argint(0, &dev);
  if(argstr(1, path, MAXPATH) < 0)
    return -1;
  int len = strlen(path);
  while(len > 1 && path[len-1] == '/')
    path[--len] = 0;

  begin_op();
  if((mp = namei(path)) == 0){
    end_op();
    return -1;
  }
  r = mount_add(dev, mp, path);
  iput(mp);
  end_op();
  return r;
}

uint64
sys_umount(void)
{
  char path[MAXPATH];
  struct inode *ip;
  int r;

  if(argstr(0, path, MAXPATH) < 0)
    return -1;
  int len = strlen(path);
  while(len > 1 && path[len-1] == '/')
    path[--len] = 0;

  begin_op();
  if((ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  r = mount_remove(path);
  iput(ip);
  end_op();
  return r;
}
