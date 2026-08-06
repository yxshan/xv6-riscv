// 进程管理。
//
// proc 数组是全局进程表，每个槽位对应一个 struct proc；
// 每把 CPU 也有自己的 cpus[] 结构，记录当前正在运行的进程和调度上下文。
//
// 调度采用多级反馈队列 (MLFQ)：高队列优先，时间片用尽后降级；
// 周期性地把所有进程提升回最高队列，避免低队列饥饿。
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct cpu cpus[NCPU];

struct proc proc[NPROC];

struct proc *initproc;

int nextpid = 1;
struct spinlock pid_lock;

extern void forkret(void);
static void freeproc(struct proc *p);

extern char trampoline[]; // trampoline.S

// wait_lock 保护 parent 字段，并确保 wait() 中的父进程不会错过
// 子进程退出时的 wakeup()。访问 p->parent 前必须先获取 wait_lock。
struct spinlock wait_lock;

// 每队列轮转起点，用于在同一队列内实现 round-robin。
static int rr[MLFQ_NQUEUES];

// 为进程表中的每个进程预分配一个内核栈页面，
// 并映射到高地址区域，栈下方留无效保护页。
void
proc_mapstacks(pagetable_t kpgtbl)
{
  struct proc *p;
  
  for(p = proc; p < &proc[NPROC]; p++) {
    char *pa = kalloc();
    if(pa == 0)
      panic("kalloc");
    uint64 va = KSTACK((int) (p - proc));
    kvmmap(kpgtbl, va, (uint64)pa, PGSIZE, PTE_R | PTE_W);
  }
}

// 初始化进程表：给每个进程槽位初始化自旋锁，
// 并记录各自的固定内核栈地址。
void
procinit(void)
{
  struct proc *p;
  
  initlock(&pid_lock, "nextpid");
  initlock(&wait_lock, "wait_lock");
  for(p = proc; p < &proc[NPROC]; p++) {
      initlock(&p->lock, "proc");
      p->state = UNUSED;
      p->kstack = KSTACK((int) (p - proc));
  }
}

// 返回当前非 UNUSED 状态的进程数，供 sysinfo 等模块读取。
int
proccount(void)
{
  int n = 0;
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->state != UNUSED)
      n++;
    release(&p->lock);
  }
  return n;
}

// 返回当前 CPU 的编号。
// 调用前必须关闭中断，防止进程在这期间被调度到其他 CPU。
int
cpuid()
{
  int id = r_tp();
  return id;
}

// 返回当前 CPU 对应的 cpus[] 元素。调用前必须关闭中断。
struct cpu*
mycpu(void)
{
  int id = cpuid();
  struct cpu *c = &cpus[id];
  return c;
}

// 返回当前正在运行进程的 proc 指针；没有进程时返回 0。
struct proc*
myproc(void)
{
  push_off();
  struct cpu *c = mycpu();
  struct proc *p = c->proc;
  pop_off();
  return p;
}

int
allocpid()
{
  int pid;
  
  acquire(&pid_lock);
  pid = nextpid;
  nextpid = nextpid + 1;
  release(&pid_lock);

  return pid;
}

// 在进程表中查找 UNUSED 槽位，并初始化运行所需的资源：
// trapframe 页、用户页表、内核上下文等。
// 成功时持有 p->lock 返回；失败返回 0。
static struct proc*
allocproc(void)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    acquire(&p->lock);
    if(p->state == UNUSED) {
      goto found;
    } else {
      release(&p->lock);
    }
  }
  return 0;

found:
  p->pid = allocpid();
  p->priority = 50;
  p->qlevel = 0;
  p->qticks = 0;
  p->uid = 0;
  p->euid = 0;
  p->gid = 0;
  p->egid = 0;
  p->umask = 022;
  memset(p->vmas, 0, sizeof(p->vmas));
  p->sigpending = 0;
  p->sigactive = 0;
  memset(p->sighandlers, 0, sizeof(p->sighandlers));
  p->state = USED;

  // 分配保存用户寄存器现场的 trapframe 页面。
  if((p->trapframe = (struct trapframe *)kalloc()) == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // 先创建只有 trampoline 和 trapframe 映射的空用户页表。
  p->pagetable = proc_pagetable(p);
  if(p->pagetable == 0){
    freeproc(p);
    release(&p->lock);
    return 0;
  }

  // 设置新进程第一次被调度时的入口为 forkret()，
  // 它最终会切换到用户态执行。
  memset(&p->context, 0, sizeof(p->context));
  p->context.ra = (uint64)forkret;
  p->context.sp = p->kstack + PGSIZE;

  return p;
}

// 释放进程持有的资源，并把槽位恢复为 UNUSED。
// 调用前必须持有 p->lock。
static void
freeproc(struct proc *p)
{
  vma_clear(p);
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->xstate = 0;
  p->sigpending = 0;
  p->sigactive = 0;
  p->state = UNUSED;
}

// 为进程创建用户页表：此时不含用户程序内存，
// 只有最高地址处的 trampoline 页和其下方的 trapframe 页。
pagetable_t
proc_pagetable(struct proc *p)
{
  pagetable_t pagetable;

  // An empty page table.
  pagetable = uvmcreate();
  if(pagetable == 0)
    return 0;

  // 在用户地址空间最高处映射 trampoline 跳板代码。
  // 它只在陷入/返回内核的瞬间被 CPU 执行，
  // 不需要给用户程序访问权限，因此不设置 PTE_U。
  if(mappages(pagetable, TRAMPOLINE, PGSIZE,
              (uint64)trampoline, PTE_R | PTE_X) < 0){
    uvmfree(pagetable, 0);
    return 0;
  }

  // 在 trampoline 页下方映射 trapframe 数据页，供 trampoline.S 读写。
  if(mappages(pagetable, TRAPFRAME, PGSIZE,
              (uint64)(p->trapframe), PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    uvmfree(pagetable, 0);
    return 0;
  }

  return pagetable;
}

// 释放进程页表，以及页表引用的用户物理内存。
void
proc_freepagetable(pagetable_t pagetable, uint64 sz)
{
  // 先解除共享内存映射，避免 uvmfree 释放共享物理页。
  shm_release_pagetable(pagetable);
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
  uvmfree(pagetable, sz);
}

// 创建第一个用户进程。
// 这里只分配进程槽位并置为 RUNNABLE；真正的 /init 程序
// 由第一个进程第一次被调度时的 forkret() 通过 exec 加载。
void
userinit(void)
{
  struct proc *p;

  p = allocproc();
  initproc = p;
  
  p->cwd = namei("/");

  p->state = RUNNABLE;

  release(&p->lock);
}

// 按 n 字节扩大或缩小当前进程的用户内存。
// 成功返回 0，失败返回 -1。
int
growproc(int n)
{
  uint64 sz;
  struct proc *p = myproc();

  sz = p->sz;
  if(n > 0){
    if(sz + n > MMAP_BASE) {
      return -1;
    }
    if((sz = uvmalloc(p->pagetable, sz, sz + n, PTE_W)) == 0) {
      return -1;
    }
  } else if(n < 0){
    sz = uvmdealloc(p->pagetable, sz, sz + n);
  }
  p->sz = sz;
  return 0;
}

// fork 的内核实现：复制父进程创建子进程。
// 子进程从 fork 返回时，trapframe->a0 被设为 0，
// 因此父子进程在用户态看到不同的 fork() 返回值。
int
kfork(void)
{
  int i, pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  // 复制父进程的用户内存（当前是完整复制物理页）。
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  if(vma_copy(np, p) < 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // 复制保存的用户寄存器现场，使子进程从同样的位置继续执行。
  *(np->trapframe) = *(p->trapframe);

  // 把子进程的 a0 设为 0，实现“子进程 fork 返回 0”。
  np->trapframe->a0 = 0;

  // 复制打开文件表，并递增引用计数。
  for(i = 0; i < NOFILE; i++)
    if(p->ofile[i])
      np->ofile[i] = filedup(p->ofile[i]);
  np->cwd = idup(p->cwd);

  safestrcpy(np->name, p->name, sizeof(p->name));
  np->priority = p->priority;
  np->qlevel = p->qlevel;
  np->qticks = 0;
  np->uid = p->uid;
  np->euid = p->euid;
  np->gid = p->gid;
  np->egid = p->egid;
  np->umask = p->umask;

  pid = np->pid;

  release(&np->lock);

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  module_notify_proc_fork(np);

  return pid;
}

// 进程退出时，把它的子进程重新托管给 init。
// 调用者必须持有 wait_lock。
void
reparent(struct proc *p)
{
  struct proc *pp;

  for(pp = proc; pp < &proc[NPROC]; pp++){
    if(pp->parent == p){
      pp->parent = initproc;
      wakeup(initproc);
    }
  }
}

// 当前进程退出，函数不会返回。
// 退出后进程进入 ZOMBIE 状态，保留 pid 和退出状态，
// 直到父进程调用 wait() 回收资源。
void
kexit(int status)
{
  struct proc *p = myproc();

  if(p == initproc)
    panic("init exiting");

  // 关闭进程占用的所有文件描述符。
  for(int fd = 0; fd < NOFILE; fd++){
    if(p->ofile[fd]){
      struct file *f = p->ofile[fd];
      fileclose(f);
      p->ofile[fd] = 0;
    }
  }

  begin_op();
  iput(p->cwd);
  end_op();
  p->cwd = 0;

  vma_clear(p);

  acquire(&wait_lock);

  // 把子进程交给 init 收养。
  reparent(p);

  // 唤醒可能在 wait() 中睡眠的父进程。
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  module_notify_proc_exit(p);

  release(&wait_lock);

  // 切换到调度器，之后不再回到本进程。
  sched();
  panic("zombie exit");
}

// wait 的内核实现：等待一个子进程退出并回收其资源，
// 返回子进程 pid；没有子进程时返回 -1。
int
kwait(uint64 addr)
{
  struct proc *pp;
  int havekids, pid;
  struct proc *p = myproc();

  acquire(&wait_lock);

  for(;;){
    // 扫描进程表，查找自己的 ZOMBIE 子进程。
    havekids = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p){
        // make sure the child isn't still in exit() or swtch().
        acquire(&pp->lock);

        havekids = 1;
        if(pp->state == ZOMBIE){
          // 找到已退出子进程，把退出状态复制给用户，然后回收 PCB。
          pid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0) {
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return pid;
        }
        release(&pp->lock);
      }
    }

    // 没有子进程或本进程已被 kill 时不再等待。
    if(!havekids || killed(p)){
      release(&wait_lock);
      return -1;
    }
    
    // 睡眠在 wait_lock 上，等待子进程退出时唤醒。
    sleep(p, &wait_lock);  //DOC: wait-sleep
  }
}

// 每 CPU 的调度器主循环，永不返回。
// 它反复扫描进程表，选中 RUNNABLE 进程后通过 swtch() 切入；
// 进程让出 CPU 时会再次 swtch() 回到这里，调度器继续选下一个进程。
void
scheduler(void)
{
  struct proc *p;
  struct cpu *c = mycpu();
  int bestq, start, k;

  c->proc = 0;
  for(;;){
    // 刚运行完的进程可能关闭了中断；
    // 这里先开中断再关掉，避免所有进程都在等待时死锁，
    // 同时也避免 wfi 与中断发生竞态。
    intr_on();
    intr_off();

    int found = 0;
    struct proc *best = 0;

    // 先找到非空的最低队列。
    bestq = MLFQ_NQUEUES;
    for(p = proc; p < &proc[NPROC]; p++){
      acquire(&p->lock);
      if(p->state == RUNNABLE && p->qlevel < bestq)
        bestq = p->qlevel;
      release(&p->lock);
    }

    // 在最低队列内从轮转起点开始选择进程。
    if(bestq < MLFQ_NQUEUES){
      start = rr[bestq];
      for(k = 0; k < NPROC; k++){
        p = &proc[(start + k) % NPROC];
        acquire(&p->lock);
        if(p->state == RUNNABLE && p->qlevel == bestq){
          best = p;
          rr[bestq] = (start + k + 1) % NPROC;
          release(&p->lock);
          break;
        }
        release(&p->lock);
      }
    }

    if(best){
      acquire(&best->lock);
      if(best->state == RUNNABLE){
        // 切入选中的进程。被切换出去的进程会负责在回到这里前
        // 释放并重新获取自己的 p->lock。
        best->state = RUNNING;
        c->proc = best;
        swtch(&c->context, &best->context);

        // 进程暂时运行结束，回到调度器；它应已修改自己的状态。
        c->proc = 0;
        found = 1;
      }
      release(&best->lock);
    }

    if(found == 0) {
      // 没有可运行进程时，用 wfi 让 CPU 休眠直到中断到来。
      asm volatile("wfi");
    }
  }
}

// 设置指定进程的调度优先级。优先级范围 0-255。
int
ksetpriority(int pid, int prio)
{
  struct proc *p;

  if(prio < 0 || prio > 255)
    return -1;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->priority = prio;
      p->qlevel = prio / 64;
      if(p->qlevel >= MLFQ_NQUEUES)
        p->qlevel = MLFQ_NQUEUES - 1;
      p->qticks = 0;
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

// 设置当前进程的信号处理函数。
int
ksignal(int sig, uint64 handler)
{
  struct proc *p = myproc();

  if(sig <= 0 || sig >= NSIG)
    return -1;
  acquire(&p->lock);
  if(handler == SIG_DFL)
    p->sighandlers[sig] = 2;
  else if(handler == SIG_IGN)
    p->sighandlers[sig] = 3;
  else
    // 用户程序从地址 0 开始；用 handler+16 编码普通处理函数。
    p->sighandlers[sig] = handler + 16;
  release(&p->lock);
  return 0;
}

// 向指定进程发送信号。
int
ksigkill(int pid, int sig)
{
  struct proc *p;

  if(sig <= 0 || sig >= NSIG)
    return -1;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      if(sig == SIGKILL){
        p->killed = 1;
        if(p->state == SLEEPING)
          p->state = RUNNABLE;
      } else {
        // 未设置处理函数或显式 SIG_DFL 时，默认动作是终止进程。
        if(p->sighandlers[sig] == 0 || p->sighandlers[sig] == 2){
          p->killed = 1;
          if(p->state == SLEEPING)
            p->state = RUNNABLE;
        } else if(p->sighandlers[sig] != 3){
          p->sigpending |= (1UL << sig);
          if(p->state == SLEEPING)
            p->state = RUNNABLE;
        }
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

uint64
sys_signal(void)
{
  int sig;
  uint64 handler;
  argint(0, &sig);
  argaddr(1, &handler);
  return ksignal(sig, handler);
}

uint64
sys_sigkill(void)
{
  int pid, sig;
  argint(0, &pid);
  argint(1, &sig);
  return ksigkill(pid, sig);
}

uint64
sys_sigreturn(void)
{
  struct proc *p = myproc();

  if(!p->sigactive)
    return -1;
  // 恢复进入信号处理前保存的用户现场。
  *p->trapframe = p->sigframe;
  p->sigactive = 0;
  return 0;
}

// 周期性优先级提升：按进程静态优先级重新计算目标队列，
// 只提升因时间片用尽而降级的进程，不把高优先级任务重置到低队列。
void
mlfq_boost(void)
{
  struct proc *p;
  int target;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->state != UNUSED){
      target = p->priority / 64;
      if(target >= MLFQ_NQUEUES)
        target = MLFQ_NQUEUES - 1;
      if(p->qlevel > target)
        p->qlevel = target;
      p->qticks = 0;
    }
    release(&p->lock);
  }
}

// 从当前进程切换回调度器。
// 调用者必须持有 p->lock，且已修改 p->state。
// intena 记录的是“本内核执行流”进入中断屏蔽前是否允许中断，
// 因此要随进程上下文一起保存和恢复，而不是随 CPU。
void
sched(void)
{
  int intena;
  struct proc *p = myproc();

  if(!holding(&p->lock))
    panic("sched p->lock");
  if(mycpu()->noff != 1)
    panic("sched locks");
  if(p->state == RUNNING)
    panic("sched RUNNING");
  if(intr_get())
    panic("sched interruptible");

  intena = mycpu()->intena;
  swtch(&p->context, &mycpu()->context);
  mycpu()->intena = intena;
}

// 当前进程主动让出 CPU，加入下一轮调度。
void
yield(void)
{
  struct proc *p = myproc();
  acquire(&p->lock);
  p->state = RUNNABLE;
  sched();
  release(&p->lock);
}

// fork 出来的子进程第一次被调度时，从 forkret() 开始执行。
// 它负责在普通进程上下文完成文件系统初始化，并 exec /init。
void
forkret(void)
{
  extern char userret[];
  static int first = 1;
  struct proc *p = myproc();

  // 进入这里时仍持有调度器传入的 p->lock，先释放它。
  release(&p->lock);

  if (first) {
    // 文件系统初始化可能睡眠等待锁，必须在普通进程上下文中完成，
    // 所以放在第一个进程第一次调度时执行，而不是 main() 里。
    fsinit(ROOTDEV);

    // 文件系统就绪后初始化所有内核模块。
    fsinit(DISK1DEV);
    mount_default();
    module_init_all();

    first = 0;
    // ensure other cores see first=0.
    __sync_synchronize();

    // 文件系统就绪后加载 /init，返回值放入 a0 作为 main 的 argc。
    p->trapframe->a0 = kexec("/init", (char *[]){ "/init", 0 });
    if (p->trapframe->a0 == -1) {
      panic("exec");
    }
  }

  // 模仿 usertrap() 的返回路径，切回用户态执行用户程序。
  prepare_return();
  uint64 satp = MAKE_SATP(p->pagetable);
  uint64 trampoline_userret = TRAMPOLINE + (userret - trampoline);
  ((void (*)(uint64))trampoline_userret)(satp);
}

// 让当前进程在通道 chan 上睡眠，并释放条件锁 lk。
// 被唤醒后重新获取 lk。
void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // 必须先持有 p->lock 才能修改状态并调用 sched()。
  // wakeup() 也会获取 p->lock，因此持有 p->lock 期间释放 lk 是安全的，
  // 不会出现“错过唤醒”的竞态。

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // 记录等待通道并进入 SLEEPING 状态，然后切换回调度器。
  p->chan = chan;
  p->state = SLEEPING;

  sched();

  // 被唤醒后清空通道，重新获取条件锁。
  p->chan = 0;

  // Reacquire original lock.
  release(&p->lock);
  acquire(lk);
}

// 唤醒所有在通道 chan 上睡眠的进程，把它们置为 RUNNABLE。
// 调用者应持有相应的条件锁。
void
wakeup(void *chan)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++) {
    if(p != myproc()){
      acquire(&p->lock);
      if(p->state == SLEEPING && p->chan == chan) {
        p->state = RUNNABLE;
      }
      release(&p->lock);
    }
  }
}

// 请求终止指定 pid 的进程。
// 只是把 killed 标志置位；进程真正退出发生在它试图返回用户态时。
int
kkill(int pid)
{
  struct proc *p;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->pid == pid){
      p->killed = 1;
      if(p->state == SLEEPING){
        // 被 kill 的睡眠进程也要唤醒，让它有机会检查 killed。
        p->state = RUNNABLE;
      }
      release(&p->lock);
      return 0;
    }
    release(&p->lock);
  }
  return -1;
}

void
setkilled(struct proc *p)
{
  acquire(&p->lock);
  p->killed = 1;
  release(&p->lock);
}

int
killed(struct proc *p)
{
  int k;
  
  acquire(&p->lock);
  k = p->killed;
  release(&p->lock);
  return k;
}

// 根据 usr_dst 选择向用户地址或内核地址拷贝数据。
// 成功返回 0，失败返回 -1。
int
either_copyout(int user_dst, uint64 dst, void *src, uint64 len)
{
  struct proc *p = myproc();
  if(user_dst){
    return copyout(p->pagetable, dst, src, len);
  } else {
    memmove((char *)dst, src, len);
    return 0;
  }
}

// 根据 user_src 选择从用户地址或内核地址读取数据。
// 成功返回 0，失败返回 -1。
int
either_copyin(void *dst, int user_src, uint64 src, uint64 len)
{
  struct proc *p = myproc();
  if(user_src){
    return copyin(p->pagetable, dst, src, len);
  } else {
    memmove(dst, (char*)src, len);
    return 0;
  }
}

// 在控制台打印进程列表，供调试使用；用户按 ^P 触发。
// 这里不取锁，避免系统卡死时反而被锁住。
void
procdump(void)
{
  static char *states[] = {
  [UNUSED]    "unused",
  [USED]      "used",
  [SLEEPING]  "sleep ",
  [RUNNABLE]  "runble",
  [RUNNING]   "run   ",
  [ZOMBIE]    "zombie"
  };
  struct proc *p;
  char *state;

  printf("\n");
  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->state >= 0 && p->state < NELEM(states) && states[p->state])
      state = states[p->state];
    else
      state = "???";
    printf("%d %s %s", p->pid, state, p->name);
    printf("\n");
  }
}
