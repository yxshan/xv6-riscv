// poll/select 系统调用。
//
// 对普通文件和设备直接视为就绪；管道/FIFO 通过 pipe_ready_* 查询。
// 没有就绪且未超时时按时钟节拍轮询，保证内核实现简单且可测试。
#include "types.h"
#include "param.h"
#include "riscv.h"
#include "defs.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"
#include "file.h"
#include "poll.h"
#include "time.h"

static int
file_ready_read(struct file *f)
{
  if(f->type == FD_PIPE || f->type == FD_FIFO)
    return pipe_ready_read(f->pipe);
  return f->type == FD_INODE || f->type == FD_DEVICE;
}

static int
file_ready_write(struct file *f)
{
  if(f->type == FD_PIPE || f->type == FD_FIFO)
    return pipe_ready_write(f->pipe);
  return f->type == FD_INODE || f->type == FD_DEVICE;
}

static int
do_poll(struct pollfd *fds, int nfds, int timeout_ms)
{
  struct proc *p = myproc();
  int elapsed = 0;

  if(timeout_ms < -1)
    return -1;
  if(nfds == 0)
    return 0;

  for(;;){
    int ready = 0;

    for(int i = 0; i < nfds; i++){
      struct file *f;
      int fd = fds[i].fd;

      fds[i].revents = 0;
      if(fd < 0 || fd >= NOFILE || (f = p->files->ofile[fd]) == 0){
        fds[i].revents = POLLNVAL;
        ready++;
        continue;
      }
      if((fds[i].events & POLLIN) && file_ready_read(f))
        fds[i].revents |= POLLIN;
      if((fds[i].events & POLLOUT) && file_ready_write(f))
        fds[i].revents |= POLLOUT;
      if(fds[i].revents)
        ready++;
    }

    if(ready || killed(p))
      return ready;
    if(timeout_ms == 0)
      return 0;
    if(timeout_ms > 0 && elapsed >= timeout_ms)
      return 0;

    acquire(&tickslock);
    uint ticks0 = ticks;
    while(ticks - ticks0 < 1){
      if(killed(p) || p->stop_pending){
        release(&tickslock);
        return ready;
      }
      sleep(&ticks, &tickslock);
    }
    release(&tickslock);

    if(timeout_ms > 0){
      elapsed += 100;
    }
  }
}

uint64
sys_poll(void)
{
  struct proc *p = myproc();
  struct pollfd fds[NOFILE];
  uint64 fdsaddr;
  int nfds, timeout;

  argaddr(0, &fdsaddr);
  argint(1, &nfds);
  argint(2, &timeout);
  if(nfds < 0 || nfds > NOFILE)
    return -1;
  if(nfds > 0 &&
     copyin(p->pagetable, (char*)fds, fdsaddr, nfds * sizeof(struct pollfd)) < 0)
    return -1;

  int r = do_poll(fds, nfds, timeout);
  if(r < 0)
    return -1;
  if(nfds > 0 &&
     copyout(p->pagetable, fdsaddr, (char*)fds, nfds * sizeof(struct pollfd)) < 0)
    return -1;
  return r;
}

uint64
sys_select(void)
{
  struct proc *p = myproc();
  struct pollfd fds[NOFILE];
  struct timespec ts;
  uint64 rf, wf, ef, tv;
  uint64 rmask = 0, wmask = 0;
  uint64 ormask = 0, owmask = 0;
  int nfds, n = 0, timeout_ms = -1;

  argint(0, &nfds);
  argaddr(1, &rf);
  argaddr(2, &wf);
  argaddr(3, &ef);
  argaddr(4, &tv);
  if(nfds < 0 || nfds > NOFILE)
    return -1;
  if(rf && copyin(p->pagetable, (char*)&rmask, rf, sizeof(rmask)) < 0)
    return -1;
  if(wf && copyin(p->pagetable, (char*)&wmask, wf, sizeof(wmask)) < 0)
    return -1;
  if(tv){
    if(copyin(p->pagetable, (char*)&ts, tv, sizeof(ts)) < 0)
      return -1;
    if(ts.tv_sec < 0 || ts.tv_nsec < 0 || ts.tv_nsec >= 1000000000L)
      return -1;
    timeout_ms = (int)(ts.tv_sec * 1000 + ts.tv_nsec / 1000000);
  }

  for(int fd = 0; fd < nfds; fd++){
    int events = 0;

    if(rf && (rmask & (1UL << fd)))
      events |= POLLIN;
    if(wf && (wmask & (1UL << fd)))
      events |= POLLOUT;
    if(events == 0)
      continue;
    fds[n].fd = fd;
    fds[n].events = events;
    fds[n].revents = 0;
    n++;
  }

  int r = do_poll(fds, n, timeout_ms);
  if(r < 0)
    return -1;

  for(int i = 0; i < n; i++){
    if(fds[i].revents & POLLIN)
      ormask |= (1UL << fds[i].fd);
    if(fds[i].revents & POLLOUT)
      owmask |= (1UL << fds[i].fd);
  }
  if(rf && copyout(p->pagetable, rf, (char*)&ormask, sizeof(ormask)) < 0)
    return -1;
  if(wf && copyout(p->pagetable, wf, (char*)&owmask, sizeof(owmask)) < 0)
    return -1;
  if(ef){
    uint64 e = 0;
    if(copyout(p->pagetable, ef, (char*)&e, sizeof(e)) < 0)
      return -1;
  }
  return r;
}
