// 管道（pipe）。
//
// 管道是内核提供的有界 FIFO 缓冲区，通过文件描述符对暴露给用户：
// 写端向 pipe 写入数据，读端按先进先出顺序读取。
// 数据保存在固定大小的环形数组 data[] 中，由一把自旋锁保护；
// 写满时写端睡眠在 &pi->nwrite，读空且写端仍打开时读端睡眠在 &pi->nread。
// 管道两端都关闭后，内核释放该管道。
#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "sleeplock.h"
#include "file.h"

#define PIPESIZE 512

struct pipe {
  struct spinlock lock;
  char data[PIPESIZE];
  uint nread;     // 已读字节数
  uint nwrite;    // 已写字节数
  int readopen;   // 读端文件描述符是否仍打开
  int writeopen;  // 写端文件描述符是否仍打开
  int fifo;       // 是否命名管道
};

// 分配一个管道：创建 pipe 结构，并把它包装成一对文件对象
// （一个只读、一个只写），返回给 sys_pipe()。
int
pipealloc(struct file **f0, struct file **f1)
{
  struct pipe *pi;

  pi = 0;
  *f0 = *f1 = 0;
  if((*f0 = filealloc()) == 0 || (*f1 = filealloc()) == 0)
    goto bad;
  if((pi = (struct pipe*)kalloc()) == 0)
    goto bad;
  pi->readopen = 1;
  pi->writeopen = 1;
  pi->fifo = 0;
  pi->nwrite = 0;
  pi->nread = 0;
  initlock(&pi->lock, "pipe");
  (*f0)->type = FD_PIPE;
  (*f0)->readable = 1;
  (*f0)->writable = 0;
  (*f0)->nonblock = 0;
  (*f0)->pipe = pi;
  (*f1)->type = FD_PIPE;
  (*f1)->readable = 0;
  (*f1)->writable = 1;
  (*f1)->nonblock = 0;
  (*f1)->pipe = pi;
  return 0;

 bad:
  if(pi)
    kfree((char*)pi);
  if(*f0)
    fileclose(*f0);
  if(*f1)
    fileclose(*f1);
  return -1;
}

// poll/select 使用的管道就绪查询。
int
pipe_ready_read(struct pipe *pi)
{
  int ready;

  acquire(&pi->lock);
  ready = (pi->nread != pi->nwrite) || pi->writeopen == 0;
  release(&pi->lock);
  return ready;
}

int
pipe_ready_write(struct pipe *pi)
{
  int ready;

  acquire(&pi->lock);
  ready = (pi->nwrite < pi->nread + PIPESIZE) || pi->readopen == 0;
  release(&pi->lock);
  return ready;
}

// 为命名管道分配一个独立的 pipe 对象。
// 初始时读写端计数都为 0，由后续 open 递增。
struct pipe*
fifoalloc(void)
{
  struct pipe *pi = (struct pipe*)kalloc();

  if(pi == 0)
    return 0;
  pi->readopen = 0;
  pi->writeopen = 0;
  pi->fifo = 1;
  pi->nread = 0;
  pi->nwrite = 0;
  initlock(&pi->lock, "fifo");
  return pi;
}

// 记录 FIFO 新打开了一个读端和/或写端。
void
fifo_open(struct pipe *pi, int read, int write)
{
  acquire(&pi->lock);
  if(read)
    pi->readopen++;
  if(write)
    pi->writeopen++;
  wakeup(&pi->nread);
  wakeup(&pi->nwrite);
  release(&pi->lock);
}

// 关闭一个命名管道端。与匿名管道不同，FIFO 的 pipe 对象
// 属于 inode，不在这里释放，而是在 inode 销毁时释放。
void
fifo_close(struct pipe *pi, int writable)
{
  acquire(&pi->lock);
  if(writable){
    if(pi->writeopen > 0)
      pi->writeopen--;
    wakeup(&pi->nread);
  } else {
    if(pi->readopen > 0)
      pi->readopen--;
    wakeup(&pi->nwrite);
  }
  release(&pi->lock);
}

void
pipeclose(struct pipe *pi, int writable)
{
  acquire(&pi->lock);
  if(writable){
    pi->writeopen = 0;
    wakeup(&pi->nread);  // 写端关闭，唤醒可能正在等待数据的读端
  } else {
    pi->readopen = 0;
    wakeup(&pi->nwrite); // 读端关闭，唤醒可能正在阻塞的写端
  }
  if(pi->readopen == 0 && pi->writeopen == 0){
    // 两端都关闭，管道失去所有引用，释放内存。
    release(&pi->lock);
    kfree((char*)pi);
  } else
    release(&pi->lock);
}

int
pipewrite(struct pipe *pi, uint64 addr, int n)
{
  int i = 0;
  struct proc *pr = myproc();

  acquire(&pi->lock);
  while(i < n){
    // 读端已关闭：继续写没有意义；进程被 kill 时也要退出。
    if(killed(pr)){
      release(&pi->lock);
      return -1;
    }
    if(pi->readopen == 0){
      if(pi->fifo){
        // FIFO 写端等待读端打开。
        wakeup(&pi->nread);
        sleep(&pi->nwrite, &pi->lock);
        continue;
      }
      release(&pi->lock);
      return -1;
    }
    if(pi->nwrite == pi->nread + PIPESIZE){ //DOC: pipewrite-full
      // 管道已写满：先唤醒读端，再睡眠等待读端腾出空间。
      wakeup(&pi->nread);
      sleep(&pi->nwrite, &pi->lock);
    } else {
      // 从用户空间逐字节拷入管道环形缓冲区。
      char ch;
      if(copyin(pr->pagetable, &ch, addr + i, 1) == -1)
        break;
      pi->data[pi->nwrite++ % PIPESIZE] = ch;
      i++;
    }
  }
  wakeup(&pi->nread);
  release(&pi->lock);

  return i;
}

int
piperead(struct pipe *pi, uint64 addr, int n)
{
  int i;
  struct proc *pr = myproc();
  char ch;

  acquire(&pi->lock);
  while(pi->nread == pi->nwrite){  //DOC: pipe-empty
    // 管道为空且写端仍打开：睡眠等待写端写入或关闭。
    if(killed(pr)){
      release(&pi->lock);
      return -1;
    }
    if(pi->writeopen == 0 && pi->fifo){
      // FIFO 读端等待写端打开。
      wakeup(&pi->nwrite);
      sleep(&pi->nread, &pi->lock);
      continue;
    }
    if(pi->writeopen == 0)
      break;
    sleep(&pi->nread, &pi->lock); //DOC: piperead-sleep
  }
  for(i = 0; i < n; i++){  //DOC: piperead-copy
    // 读到数据为空时结束；否则把字节逐一带到用户空间。
    if(pi->nread == pi->nwrite)
      break;
    ch = pi->data[pi->nread % PIPESIZE];
    if(copyout(pr->pagetable, addr + i, &ch, 1) == -1) {
      if(i == 0)
        i = -1;
      break;
    }
    pi->nread++;
  }
  wakeup(&pi->nwrite);  //DOC: piperead-wakeup
  release(&pi->lock);
  return i;
}
