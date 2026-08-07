//
// 文件描述符层的支持函数。
//
// 文件描述符是用户可见的整数，指向进程打开文件表（ofile）；
// 进程打开文件表又指向全局 ftable 中的 struct file。
// struct file 是“打开的文件”抽象，通过 ref 引用计数共享：
// fork 和 dup 会让多个描述符指向同一个 file，计数相应增加；
// 计数归零时，file 才真正关闭底层对象（管道或 inode）。
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "fs.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "file.h"
#include "stat.h"
#include "proc.h"

struct devsw devsw[NDEV];
struct {
  struct spinlock lock;
  struct file file[NFILE];
} ftable;

void
fileinit(void)
{
  initlock(&ftable.lock, "ftable");
}

struct proc_files*
proc_files_alloc(void)
{
  struct proc_files *pf = kalloc();

  if(pf == 0)
    return 0;
  memset(pf, 0, PGSIZE);
  initlock(&pf->lock, "proc_files");
  pf->ref = 1;
  return pf;
}

void
proc_files_share(struct proc_files *pf)
{
  acquire(&pf->lock);
  pf->ref++;
  release(&pf->lock);
}

// fork 时复制文件描述符表：底层 file 对象共享，但表本身独立。
void
proc_files_copy(struct proc_files *dst, struct proc_files *src)
{
  acquire(&src->lock);
  for(int i = 0; i < NOFILE; i++){
    if(src->ofile[i])
      dst->ofile[i] = filedup(src->ofile[i]);
    dst->cloexec[i] = src->cloexec[i];
  }
  release(&src->lock);
}

// exec 成功提交前关闭所有 O_CLOEXEC 描述符。
void
proc_files_close_cloexec(struct proc_files *pf)
{
  if(pf == 0)
    return;
  acquire(&pf->lock);
  for(int i = 0; i < NOFILE; i++){
    if(pf->cloexec[i] && pf->ofile[i]){
      struct file *f = pf->ofile[i];
      pf->ofile[i] = 0;
      pf->cloexec[i] = 0;
      release(&pf->lock);
      fileclose(f);
      acquire(&pf->lock);
    }
  }
  release(&pf->lock);
}

// 释放一个文件描述符表引用；最后一个引用关闭所有 fd。
void
proc_files_release(struct proc_files *pf)
{
  int last = 0;

  if(pf == 0)
    return;
  acquire(&pf->lock);
  if(--pf->ref == 0)
    last = 1;
  release(&pf->lock);
  if(!last)
    return;

  for(int i = 0; i < NOFILE; i++){
    if(pf->ofile[i])
      fileclose(pf->ofile[i]);
  }
  kfree(pf);
}

struct proc_fs*
proc_fs_alloc(void)
{
  struct proc_fs *pfs = kalloc();

  if(pfs == 0)
    return 0;
  memset(pfs, 0, PGSIZE);
  initlock(&pfs->lock, "proc_fs");
  pfs->ref = 1;
  return pfs;
}

void
proc_fs_share(struct proc_fs *pfs)
{
  acquire(&pfs->lock);
  pfs->ref++;
  release(&pfs->lock);
}

// fork 时复制 cwd；inode 引用单独持有。
void
proc_fs_copy(struct proc_fs *dst, struct proc_fs *src)
{
  acquire(&src->lock);
  dst->root = idup(src->root);
  dst->cwd = idup(src->cwd);
  release(&src->lock);
}

// 释放一个文件系统上下文引用；最后一个引用释放 cwd。
void
proc_fs_release(struct proc_fs *pfs)
{
  int last = 0;

  if(pfs == 0)
    return;
  acquire(&pfs->lock);
  if(--pfs->ref == 0)
    last = 1;
  release(&pfs->lock);
  if(!last)
    return;

  if(pfs->root){
    begin_op();
    iput(pfs->root);
    end_op();
  }
  if(pfs->cwd){
    begin_op();
    iput(pfs->cwd);
    end_op();
  }
  kfree(pfs);
}

// 强制把 inode 元数据写回磁盘，供 fsync 使用。
int
filefsync(struct file *f)
{
  if(f == 0 || (f->type != FD_INODE && f->type != FD_DEVICE &&
                f->type != FD_FIFO))
    return 0;

  begin_op();
  ilock(f->ip);
  iupdate(f->ip);
  iunlock(f->ip);
  end_op();
  return 0;
}

// 从全局文件表分配一个 struct file，并把引用计数置为 1。
struct file*
filealloc(void)
{
  struct file *f;

  acquire(&ftable.lock);
  for(f = ftable.file; f < ftable.file + NFILE; f++){
    if(f->ref == 0){
      f->ref = 1;
      release(&ftable.lock);
      return f;
    }
  }
  release(&ftable.lock);
  return 0;
}

// 增加文件引用计数。每次新的“引用”产生（fork/dup）时调用。
struct file*
filedup(struct file *f)
{
  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("filedup");
  f->ref++;
  release(&ftable.lock);
  return f;
}

// 关闭文件引用：递减引用计数，归零时真正释放底层资源。
void
fileclose(struct file *f)
{
  struct file ff;

  acquire(&ftable.lock);
  if(f->ref < 1)
    panic("fileclose");
  if(--f->ref > 0){
    release(&ftable.lock);
    return;
  }
  ff = *f;
  f->ref = 0;
  f->type = FD_NONE;
  release(&ftable.lock);

  // 根据文件类型释放对应底层资源。
  if(ff.type == FD_PIPE){
    pipeclose(ff.pipe, ff.writable);
  } else if(ff.type == FD_FIFO){
    fifo_close(ff.pipe, ff.writable);
    if(ff.readable && ff.writable)
      fifo_close(ff.pipe, 0);
  } else if(ff.type == FD_DEVICE){
    if(ff.major >= 0 && ff.major < NDEV && devsw[ff.major].close)
      devsw[ff.major].close(&ff);
  }
  if(ff.type == FD_INODE || ff.type == FD_DEVICE || ff.type == FD_FIFO){
    begin_op();
    iput(ff.ip);
    end_op();
  }
}

// 获取文件元数据，写入用户提供的 struct stat。
int
filestat(struct file *f, uint64 addr)
{
  struct proc *p = myproc();
  struct stat st;
  
  if(f->type == FD_INODE || f->type == FD_DEVICE || f->type == FD_FIFO){
    ilock(f->ip);
    stati(f->ip, &st);
    iunlock(f->ip);
    if(copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0)
      return -1;
    return 0;
  }
  return -1;
}

// 从文件读取数据。根据文件类型分派到管道、设备或普通 inode。
int
fileread(struct file *f, uint64 addr, int n)
{
  int r = 0;

  if(f->readable == 0)
    return -1;

  if(f->type == FD_PIPE || f->type == FD_FIFO){
    if(f->nonblock && !pipe_ready_read(f->pipe))
      return 0;
    r = piperead(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].read)
      return -1;
    r = devsw[f->major].read(f, 1, addr, n);
  } else if(f->type == FD_INODE){
    ilock(f->ip);
    if((r = readi(f->ip, 1, addr, f->off, n)) > 0)
      f->off += r;
    iunlock(f->ip);
  } else {
    panic("fileread");
  }

  return r;
}

// 向文件写入数据。根据文件类型分派到管道、设备或普通 inode。
int
filewrite(struct file *f, uint64 addr, int n)
{
  int r, ret = 0;

  if(f->writable == 0)
    return -1;

  if(f->type == FD_PIPE || f->type == FD_FIFO){
    if(f->nonblock && !pipe_ready_write(f->pipe))
      return -1;
    ret = pipewrite(f->pipe, addr, n);
  } else if(f->type == FD_DEVICE){
    if(f->major < 0 || f->major >= NDEV || !devsw[f->major].write)
      return -1;
    ret = devsw[f->major].write(f, 1, addr, n);
  } else if(f->type == FD_INODE){
    // 每次只写少量块，避免超过日志事务的最大大小；
    // 预算中要计入 inode、间接块、分配位图以及非对齐写带来的额外块。
    int max = ((MAXOPBLOCKS-1-1-2) / 2) * BSIZE;
    int i = 0;
    while(i < n){
      int n1 = n - i;
      if(n1 > max)
        n1 = max;

      begin_op();
      ilock(f->ip);
      if ((r = writei(f->ip, 1, addr + i, f->off, n1)) > 0)
        f->off += r;
      iunlock(f->ip);
      end_op();

      if(r != n1){
        // writei 返回错误，停止写入。
        break;
      }
      i += r;
    }
    ret = (i == n ? n : -1);
  } else {
    panic("filewrite");
  }

  return ret;
}
