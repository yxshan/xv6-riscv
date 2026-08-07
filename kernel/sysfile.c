//
// 文件系统相关系统调用的内核实现。
// 用户代码不可信，因此这里负责参数检查和文件描述符校验，
// 再把实际工作交给 file.c（文件对象）和 fs.c（inode/目录/路径）完成。
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "stat.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"
#include "fcntl.h"
#include "uio.h"

static struct inode* create(char *path, short type, short major, short minor, ushort mode);

// 取出第 n 个参数作为文件描述符，并返回对应的 struct file。
// 校验描述符是否在范围内、进程是否真的打开了该文件。
static int
argfd(int n, int *pfd, struct file **pf)
{
  int fd;
  struct file *f;

  argint(n, &fd);
  if(fd < 0 || fd >= NOFILE || (f=myproc()->files->ofile[fd]) == 0)
    return -1;
  if(pfd)
    *pfd = fd;
  if(pf)
    *pf = f;
  return 0;
}

// 为文件分配一个空闲文件描述符。
// 成功后接管调用者持有的文件引用。
static int
fdalloc(struct file *f)
{
  int fd;
  struct proc *p = myproc();

  for(fd = 0; fd < NOFILE; fd++){
    if(p->files->ofile[fd] == 0){
      p->files->ofile[fd] = f;
      return fd;
    }
  }
  return -1;
}

uint64
sys_dup(void)
{
  struct file *f;
  int fd;

  if(argfd(0, 0, &f) < 0)
    return -1;
  if((fd=fdalloc(f)) < 0)
    return -1;
  // dup 让两个描述符共享同一个文件对象，因此增加引用计数。
  filedup(f);
  myproc()->files->cloexec[fd] = 0;
  return fd;
}

uint64
sys_dup2(void)
{
  struct proc *p = myproc();
  struct file *f, *old;
  int oldfd, newfd;

  argint(0, &oldfd);
  argint(1, &newfd);
  if(oldfd < 0 || oldfd >= NOFILE || newfd < 0 || newfd >= NOFILE)
    return -1;
  f = p->files->ofile[oldfd];
  if(f == 0)
    return -1;
  if(oldfd == newfd)
    return newfd;
  old = p->files->ofile[newfd];
  if(old){
    p->files->ofile[newfd] = 0;
    fileclose(old);
  }
  p->files->ofile[newfd] = f;
  p->files->cloexec[newfd] = 0;
  filedup(f);
  return newfd;
}

uint64
sys_read(void)
{
  struct file *f;
  int n;
  uint64 p;

  argaddr(1, &p);
  argint(2, &n);
  if(argfd(0, 0, &f) < 0)
    return -1;
  // 具体读写语义（普通文件、管道、设备）由 fileread 根据 f->type 分派。
  return fileread(f, p, n);
}

uint64
sys_write(void)
{
  struct file *f;
  int n;
  uint64 p;
  
  argaddr(1, &p);
  argint(2, &n);
  if(argfd(0, 0, &f) < 0)
    return -1;

  return filewrite(f, p, n);
}

uint64
sys_close(void)
{
  int fd;
  struct file *f;

  if(argfd(0, &fd, &f) < 0)
    return -1;
  myproc()->files->ofile[fd] = 0;
  myproc()->files->cloexec[fd] = 0;
  fileclose(f);
  return 0;
}

uint64
sys_getcwd(void)
{
  uint64 addr;
  int size;
  char buf[MAXPATH];

  argaddr(0, &addr);
  argint(1, &size);
  if(size <= 0 || size > MAXPATH)
    return -1;
  if(kgetcwd(buf, size) < 0)
    return -1;
  if(copyout(myproc()->pagetable, addr, buf, strlen(buf) + 1) < 0)
    return -1;
  return 0;
}

uint64
sys_chroot(void)
{
  char path[MAXPATH];
  struct inode *ip, *old;
  struct proc *p = myproc();

  if(p->fs == 0 || argstr(0, path, MAXPATH) < 0)
    return -1;
  begin_op();
  if((ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR || iaccess(ip, 1) < 0){
    iunlockput(ip);
    end_op();
    return -1;
  }
  iunlock(ip);
  old = p->fs->root;
  p->fs->root = ip;
  if(old)
    iput(old);
  end_op();
  return 0;
}

uint64
sys_fsync(void)
{
  struct file *f;

  if(argfd(0, 0, &f) < 0)
    return -1;
  return filefsync(f);
}

uint64
sys_readv(void)
{
  struct file *f;
  struct iovec uv;
  uint64 iovaddr;
  int fd, iovcnt, total = 0;

  argint(0, &fd);
  argaddr(1, &iovaddr);
  argint(2, &iovcnt);
  if(iovcnt <= 0 || iovcnt > 16 || argfd(0, 0, &f) < 0)
    return -1;

  for(int i = 0; i < iovcnt; i++){
    if(copyin(myproc()->pagetable, (char*)&uv, iovaddr + i * sizeof(uv),
              sizeof(uv)) < 0)
      return -1;
    if(uv.iov_len == 0)
      continue;
    if(uv.iov_len > 0x7fffffff)
      return -1;
    int n = fileread(f, (uint64)uv.iov_base, (int)uv.iov_len);
    if(n < 0){
      if(total == 0)
        return -1;
      break;
    }
    total += n;
    if(n < (int)uv.iov_len)
      break;
  }
  return total;
}

uint64
sys_writev(void)
{
  struct file *f;
  struct iovec uv;
  uint64 iovaddr;
  int fd, iovcnt, total = 0;

  argint(0, &fd);
  argaddr(1, &iovaddr);
  argint(2, &iovcnt);
  if(iovcnt <= 0 || iovcnt > 16 || argfd(0, 0, &f) < 0)
    return -1;

  for(int i = 0; i < iovcnt; i++){
    if(copyin(myproc()->pagetable, (char*)&uv, iovaddr + i * sizeof(uv),
              sizeof(uv)) < 0)
      return -1;
    if(uv.iov_len == 0)
      continue;
    if(uv.iov_len > 0x7fffffff)
      return -1;
    int n = filewrite(f, (uint64)uv.iov_base, (int)uv.iov_len);
    if(n < 0){
      if(total == 0)
        return -1;
      break;
    }
    total += n;
    if(n < (int)uv.iov_len){
      if(total == 0)
        return -1;
      break;
    }
  }
  return total;
}

uint64
sys_fstat(void)
{
  struct file *f;
  uint64 st; // user pointer to struct stat

  argaddr(1, &st);
  if(argfd(0, 0, &f) < 0)
    return -1;
  return filestat(f, st);
}

// 修改文件权限位。只有文件所有者或 root 可以执行。
uint64
sys_chmod(void)
{
  char path[MAXPATH];
  int mode;
  struct inode *ip;
  struct proc *p = myproc();

  if(argstr(0, path, MAXPATH) < 0)
    return -1;
  argint(1, &mode);
  if(mode < 0 || mode > PERM_MASK)
    return -1;

  begin_op();
  if((ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(p->euid != 0 && p->euid != ip->uid){
    iunlockput(ip);
    end_op();
    return -1;
  }
  ip->mode = mode & PERM_MASK;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return 0;
}

// 修改文件所有者/所属组。uid/gid 为 -1 时保持原值。
uint64
sys_chown(void)
{
  char path[MAXPATH];
  int uid, gid;
  struct inode *ip;
  struct proc *p = myproc();

  if(argstr(0, path, MAXPATH) < 0)
    return -1;
  argint(1, &uid);
  argint(2, &gid);
  if((uid < -1 || uid > 65535) || (gid < -1 || gid > 65535))
    return -1;

  begin_op();
  if((ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(p->euid != 0 && p->euid != ip->uid)
    goto bad;
  // 非 root 只能修改所属组为自己当前有效组，不能修改文件所有者。
  if(p->euid != 0){
    if(uid != -1 && uid != ip->uid)
      goto bad;
    if(gid != -1 && gid != p->egid)
      goto bad;
  }
  if(uid != -1)
    ip->uid = uid;
  if(gid != -1)
    ip->gid = gid;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return 0;

bad:
  iunlockput(ip);
  end_op();
  return -1;
}

// 创建硬链接：让 new 指向 old 的同一个 inode，并递增 nlink。
uint64
sys_link(void)
{
  char name[DIRSIZ], new[MAXPATH], old[MAXPATH];
  struct inode *dp, *ip;

  if(argstr(0, old, MAXPATH) < 0 || argstr(1, new, MAXPATH) < 0)
    return -1;

  begin_op();
  if((ip = namei(old)) == 0){
    end_op();
    return -1;
  }

  ilock(ip);
  if(ip->type == T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }

  ip->nlink++;
  iupdate(ip);
  iunlock(ip);

  if((dp = nameiparent(new, name)) == 0)
    goto bad;
  ilock(dp);
  if(iaccess(dp, 2) < 0 || dp->dev != ip->dev ||
     dirlink(dp, name, ip->inum) < 0){
    iunlockput(dp);
    goto bad;
  }
  iunlockput(dp);
  iput(ip);

  end_op();

  return 0;

bad:
  ilock(ip);
  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);
  end_op();
  return -1;
}

// 创建 path 指向 target 的符号链接。
uint64
sys_symlink(void)
{
  char target[MAXPATH], path[MAXPATH];
  struct inode *ip;
  int n;

  if(argstr(0, target, MAXPATH) < 0 || argstr(1, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if((ip = create(path, T_SYMLINK, 0, 0, 0)) == 0){
    end_op();
    return -1;
  }

  n = strlen(target) + 1;
  if(writei(ip, 0, (uint64)target, 0, n) != n){
    iunlockput(ip);
    end_op();
    return -1;
  }

  iunlockput(ip);
  end_op();
  return 0;
}

// 判断目录是否为空（除 "." 和 ".." 外没有其他目录项）。
static int
isdirempty(struct inode *dp)
{
  int off;
  struct dirent de;

  for(off=2*sizeof(de); off<dp->size; off+=sizeof(de)){
    if(readi(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
      panic("isdirempty: readi");
    if(de.inum != 0)
      return 0;
  }
  return 1;
}

uint64
sys_unlink(void)
{
  struct inode *ip, *dp;
  struct dirent de;
  char name[DIRSIZ], path[MAXPATH];
  uint off;

  if(argstr(0, path, MAXPATH) < 0)
    return -1;

  begin_op();
  if((dp = nameiparent(path, name)) == 0){
    end_op();
    return -1;
  }

  ilock(dp);

  if(iaccess(dp, 2) < 0)
    goto bad;

  // 不允许删除 "." 和 ".."。
  if(namecmp(name, ".") == 0 || namecmp(name, "..") == 0)
    goto bad;

  if((ip = dirlookup(dp, name, &off)) == 0)
    goto bad;
  ilock(ip);

  if(ip->nlink < 1)
    panic("unlink: nlink < 1");
  if(ip->type == T_DIR && !isdirempty(ip)){
    iunlockput(ip);
    goto bad;
  }

  memset(&de, 0, sizeof(de));
  if(writei(dp, 0, (uint64)&de, off, sizeof(de)) != sizeof(de))
    panic("unlink: writei");
  if(ip->type == T_DIR){
    dp->nlink--;
    iupdate(dp);
  }
  iunlockput(dp);

  ip->nlink--;
  iupdate(ip);
  iunlockput(ip);

  end_op();

  return 0;

bad:
  iunlockput(dp);
  end_op();
  return -1;
}

static struct inode*
create(char *path, short type, short major, short minor, ushort mode)
{
  struct inode *ip, *dp;
  char name[DIRSIZ];

  if((dp = nameiparent(path, name)) == 0)
    return 0;

  ilock(dp);

  if((ip = dirlookup(dp, name, 0)) != 0){
    iunlockput(dp);
    ilock(ip);
    if(type == T_FILE &&
       (ip->type == T_FILE || ip->type == T_DEVICE || ip->type == T_FIFO))
      return ip;
    iunlockput(ip);
    return 0;
  }

  // 只有真正新建目录项时才要求父目录写权限；
  // O_CREATE 打开已存在文件不应受父目录权限影响。
  if(iaccess(dp, 2) < 0){
    iunlockput(dp);
    return 0;
  }

  if((ip = ialloc(dp->dev, type)) == 0){
    iunlockput(dp);
    return 0;
  }

  ilock(ip);
  ip->major = major;
  ip->minor = minor;
  if(mode == 0){
    ushort base = (type == T_DIR) ? 0777 : 0666;
    mode = (base & ~myproc()->umask) & PERM_MASK;
  } else {
    mode &= PERM_MASK;
  }
  ip->mode = mode;
  ip->uid = myproc()->euid;
  ip->gid = myproc()->egid;
  ip->nlink = 1;
  iupdate(ip);

  if(type == T_DIR){  // 目录需要创建 "." 和 ".." 目录项。
    // "." 指向自身，但故意不递增 nlink，避免循环引用导致永远释放不掉。
    if(dirlink(ip, ".", ip->inum) < 0 || dirlink(ip, "..", dp->inum) < 0)
      goto fail;
  }

  if(dirlink(dp, name, ip->inum) < 0)
    goto fail;

  if(type == T_DIR){
    // 父目录因为多了 ".." 指向它，所以成功时递增 nlink。
    dp->nlink++;  // 为 ".." 目录项
    iupdate(dp);
  }

  iunlockput(dp);

  return ip;

 fail:
  // something went wrong. de-allocate ip.
  ip->nlink = 0;
  iupdate(ip);
  iunlockput(ip);
  iunlockput(dp);
  return 0;
}

uint64
sys_mkfifo(void)
{
  char path[MAXPATH];
  int mode;
  struct inode *ip;

  argstr(0, path, MAXPATH);
  argint(1, &mode);

  begin_op();
  if((ip = create(path, T_FIFO, 0, 0, (ushort)mode)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_open(void)
{
  char path[MAXPATH];
  int fd, omode, fifo_read, fifo_write;
  struct file *f;
  struct inode *ip;
  struct pipe *pi = 0;
  int n;

  argint(1, &omode);
  if((n = argstr(0, path, MAXPATH)) < 0)
    return -1;

  begin_op();

  // O_CREATE 时若文件不存在则创建，否则查找已有 inode。
  if(omode & O_CREATE){
    ip = create(path, T_FILE, 0, 0, 0);
    if(ip == 0){
      end_op();
      return -1;
    }
  } else {
    if((ip = namei(path)) == 0){
      end_op();
      return -1;
    }
    ilock(ip);
    if(ip->type == T_DIR && omode != O_RDONLY){
      iunlockput(ip);
      end_op();
      return -1;
    }
  }

  if(ip->type == T_DEVICE && (ip->major < 0 || ip->major >= NDEV)){
    iunlockput(ip);
    end_op();
    return -1;
  }

  // 打开时必须满足请求的读/写权限；O_TRUNC 也要求写权限。
  int want = 0;
  if(!(omode & O_WRONLY))
    want |= 4;
  if((omode & O_WRONLY) || (omode & O_RDWR) || (omode & O_TRUNC))
    want |= 2;
  if(iaccess(ip, want) < 0){
    iunlockput(ip);
    end_op();
    return -1;
  }

  // FIFO 第一次打开时创建内存管道，后续打开复用同一个管道对象。
  if(ip->type == T_FIFO){
    if(ip->fifo == 0){
      pi = fifoalloc();
      if(pi == 0){
        iunlockput(ip);
        end_op();
        return -1;
      }
      ip->fifo = pi;
    } else {
      pi = ip->fifo;
    }
  }

  // 分配文件对象和文件描述符，并填写读写权限。
  if((f = filealloc()) == 0 || (fd = fdalloc(f)) < 0){
    if(f)
      fileclose(f);
    iunlockput(ip);
    end_op();
    return -1;
  }

  if(ip->type == T_FIFO){
    f->type = FD_FIFO;
    f->pipe = pi;
    // O_RDWR 同时拥有读端和写端，不能只按 O_WRONLY 判断。
    fifo_read = (omode & O_RDWR) || !(omode & O_WRONLY);
    fifo_write = (omode & O_RDWR) || (omode & O_WRONLY);
    fifo_open(pi, fifo_read, fifo_write);
  } else if(ip->type == T_DEVICE){
    f->type = FD_DEVICE;
    f->major = ip->major;
  } else {
    f->type = FD_INODE;
    f->off = 0;
  }
  f->ip = ip;
  f->readable = !(omode & O_WRONLY);
  f->writable = (omode & O_WRONLY) || (omode & O_RDWR);
  f->nonblock = (omode & O_NONBLOCK) != 0;
  myproc()->files->cloexec[fd] = (omode & O_CLOEXEC) != 0;

  if(ip->type == T_DEVICE && devsw[f->major].open && devsw[f->major].open(f) < 0){
    myproc()->files->ofile[fd] = 0;
    myproc()->files->cloexec[fd] = 0;
    iunlock(ip);
    fileclose(f);
    end_op();
    return -1;
  }

  if((omode & O_TRUNC) && ip->type == T_FILE){
    itrunc(ip);
  }

  iunlock(ip);
  end_op();

  return fd;
}

uint64
sys_mkdir(void)
{
  char path[MAXPATH];
  struct inode *ip;

  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = create(path, T_DIR, 0, 0, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_mknod(void)
{
  struct inode *ip;
  char path[MAXPATH];
  int major, minor;

  begin_op();
  argint(1, &major);
  argint(2, &minor);
  if((argstr(0, path, MAXPATH)) < 0 ||
     (ip = create(path, T_DEVICE, major, minor, 0)) == 0){
    end_op();
    return -1;
  }
  iunlockput(ip);
  end_op();
  return 0;
}

uint64
sys_chdir(void)
{
  char path[MAXPATH];
  struct inode *ip;
  struct proc *p = myproc();
  
  begin_op();
  if(argstr(0, path, MAXPATH) < 0 || (ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);
  if(ip->type != T_DIR){
    iunlockput(ip);
    end_op();
    return -1;
  }
  if(iaccess(ip, 1) < 0){
    iunlockput(ip);
    end_op();
    return -1;
  }
  // 释放旧 cwd 的引用，换用新目录的引用。
  iunlock(ip);
  iput(p->fs->cwd);
  end_op();
  p->fs->cwd = ip;
  return 0;
}

uint64
sys_exec(void)
{
  char path[MAXPATH], *argv[MAXARG];
  int i;
  uint64 uargv, uarg;

  argaddr(1, &uargv);
  if(argstr(0, path, MAXPATH) < 0) {
    return -1;
  }
  // 把用户空间的 argv 数组逐个复制到内核，再进行 exec。
  memset(argv, 0, sizeof(argv));
  for(i=0;; i++){
    if(i >= NELEM(argv)){
      goto bad;
    }
    if(fetchaddr(uargv+sizeof(uint64)*i, (uint64*)&uarg) < 0){
      goto bad;
    }
    if(uarg == 0){
      argv[i] = 0;
      break;
    }
    argv[i] = kalloc();
    if(argv[i] == 0)
      goto bad;
    if(fetchstr(uarg, argv[i], PGSIZE) < 0)
      goto bad;
  }

  int ret = kexec(path, argv);

  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);

  return ret;

 bad:
  for(i = 0; i < NELEM(argv) && argv[i] != 0; i++)
    kfree(argv[i]);
  return -1;
}

static int
pipe_common(uint64 fdarray, int flags)
{
  struct file *rf, *wf;
  int fd0, fd1;
  struct proc *p = myproc();

  if((flags & ~(O_CLOEXEC | O_NONBLOCK)) != 0)
    return -1;
  // 创建一对文件对象：rf 负责读，wf 负责写。
  if(pipealloc(&rf, &wf) < 0)
    return -1;
  rf->nonblock = (flags & O_NONBLOCK) != 0;
  wf->nonblock = (flags & O_NONBLOCK) != 0;
  fd0 = -1;
  if((fd0 = fdalloc(rf)) < 0 || (fd1 = fdalloc(wf)) < 0){
    if(fd0 >= 0)
      p->files->ofile[fd0] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  if(flags & O_CLOEXEC){
    p->files->cloexec[fd0] = 1;
    p->files->cloexec[fd1] = 1;
  }
  // 把两个描述符写回用户提供的 int[2] 数组。
  if(copyout(p->pagetable, fdarray, (char*)&fd0, sizeof(fd0)) < 0 ||
     copyout(p->pagetable, fdarray+sizeof(fd0), (char *)&fd1, sizeof(fd1)) < 0){
    p->files->ofile[fd0] = 0;
    p->files->ofile[fd1] = 0;
    p->files->cloexec[fd0] = 0;
    p->files->cloexec[fd1] = 0;
    fileclose(rf);
    fileclose(wf);
    return -1;
  }
  return 0;
}

uint64
sys_pipe(void)
{
  uint64 fdarray;

  argaddr(0, &fdarray);
  return pipe_common(fdarray, 0);
}

uint64
sys_pipe2(void)
{
  uint64 fdarray;
  int flags;

  argaddr(0, &fdarray);
  argint(1, &flags);
  return pipe_common(fdarray, flags);
}

// mmap：创建文件或匿名映射，地址由内核在 mmap 区域分配。
uint64
sys_mmap(void)
{
  uint64 addr, length, start;
  int prot, flags, fd, off;
  struct file *f = 0;
  struct inode *ip = 0;
  struct proc *p = myproc();
  uint64 npages;

  argaddr(0, &addr);
  argaddr(1, &length);
  argint(2, &prot);
  argint(3, &flags);
  argint(4, &fd);
  argint(5, &off);

  if(addr != 0 || length == 0 || length > MMAP_SIZE || off < 0)
    return -1;
  if((flags & (MAP_PRIVATE|MAP_SHARED)) == 0 ||
     (flags & (MAP_PRIVATE|MAP_SHARED)) == (MAP_PRIVATE|MAP_SHARED) ||
     (flags & MAP_FIXED))
    return -1;

  if(fd < 0){
    if((flags & MAP_ANONYMOUS) == 0)
      return -1;
  } else {
    if((flags & MAP_ANONYMOUS) || argfd(4, &fd, &f) < 0 ||
       f->type != FD_INODE)
      return -1;
    if((prot & PROT_READ) && !f->readable)
      return -1;
    if((flags & MAP_SHARED) && (prot & PROT_WRITE) && !f->writable)
      return -1;

    ip = idup(f->ip);
    ilock(ip);
    if(ip->type != T_FILE || (uint64)off + length > (uint64)ip->size){
      iunlockput(ip);
      return -1;
    }
    iunlock(ip);
  }

  npages = PGROUNDUP(length) / PGSIZE;
  start = vma_mmap(p, npages, prot, flags, ip, (uint)off);
  if(start == 0){
    if(ip){
      begin_op();
      iput(ip);
      end_op();
    }
    return -1;
  }
  return start;
}

// munmap：解除 mmap 建立的完整映射。
uint64
sys_munmap(void)
{
  uint64 addr, length;
  struct proc *p = myproc();

  argaddr(0, &addr);
  argaddr(1, &length);
  return vma_remove(p, addr, length);
}
