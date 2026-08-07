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
#include "stat.h"

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

struct proc_sig*
proc_sig_alloc(void)
{
  struct proc_sig *ps = kalloc();

  if(ps == 0)
    return 0;
  memset(ps, 0, PGSIZE);
  initlock(&ps->lock, "proc_sig");
  ps->ref = 1;
  return ps;
}

void
proc_sig_share(struct proc_sig *ps)
{
  acquire(&ps->lock);
  ps->ref++;
  release(&ps->lock);
}

// fork 时复制信号处理表；线程组待处理信号不继承。
void
proc_sig_copy(struct proc_sig *dst, struct proc_sig *src)
{
  acquire(&src->lock);
  for(int i = 0; i < NSIG; i++){
    dst->handlers[i] = src->handlers[i];
    dst->masks[i] = src->masks[i];
    dst->flags[i] = src->flags[i];
  }
  release(&src->lock);
}

void
proc_sig_release(struct proc_sig *ps)
{
  int last = 0;

  if(ps == 0)
    return;
  acquire(&ps->lock);
  if(--ps->ref == 0)
    last = 1;
  release(&ps->lock);
  if(last)
    kfree(ps);
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
  p->tgid = 0;
  p->pgid = 0;
  p->priority = 50;
  p->qlevel = 0;
  p->qticks = 0;
  p->is_kthread = 0;
  p->kthread_fn = 0;
  p->kthread_arg = 0;
  p->files = 0;
  p->fs = 0;
  p->uid = 0;
  p->euid = 0;
  p->gid = 0;
  p->egid = 0;
  p->umask = 022;
  p->vmas = 0;
  p->sig = 0;
  p->sigpending = 0;
  p->sigblocked = 0;
  p->sigblocked_saved = 0;
  p->sigactive = 0;
  p->stop_pending = 0;
  p->continued = 0;
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
  proc_vmas_release(p);
  proc_sig_release(p->sig);
  if(p->trapframe)
    kfree((void*)p->trapframe);
  p->trapframe = 0;
  if(p->pagetable)
    proc_freepagetable(p->pagetable, p->sz);
  p->pagetable = 0;
  p->sz = 0;
  p->pid = 0;
  p->tgid = 0;
  p->pgid = 0;
  p->is_kthread = 0;
  p->kthread_fn = 0;
  p->kthread_arg = 0;
  proc_files_release(p->files);
  p->files = 0;
  proc_fs_release(p->fs);
  p->fs = 0;
  p->vmas = 0;
  p->sig = 0;
  p->parent = 0;
  p->name[0] = 0;
  p->chan = 0;
  p->killed = 0;
  p->stop_pending = 0;
  p->continued = 0;
  p->xstate = 0;
  p->sigpending = 0;
  p->sigblocked = 0;
  p->sigblocked_saved = 0;
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
  p->tgid = p->pid;
  p->pgid = p->pid;
  
  p->files = proc_files_alloc();
  if(p->files == 0)
    panic("proc_files_alloc");
  p->fs = proc_fs_alloc();
  if(p->fs == 0)
    panic("proc_fs_alloc");
  p->fs->root = namei("/");
  if(p->fs->root == 0)
    panic("root namei");
  p->fs->cwd = idup(p->fs->root);
  p->vmas = proc_vmas_alloc();
  if(p->vmas == 0)
    panic("proc_vmas_alloc");
  p->sig = proc_sig_alloc();
  if(p->sig == 0)
    panic("proc_sig_alloc");

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
  int pid;
  struct proc *np;
  struct proc *p = myproc();

  // Allocate process.
  if((np = allocproc()) == 0){
    return -1;
  }

  if(p->files == 0 || p->fs == 0 || p->vmas == 0 || p->sig == 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }

  np->vmas = proc_vmas_alloc();
  if(np->vmas == 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  // VMA 表使用 sleeplock，初始化阶段不能继续持有 np->lock：
  // 否则 releasesleep() 里的 wakeup() 会递归获取同一把 PCB 锁。
  release(&np->lock);

  // 复制父进程的用户内存（当前是完整复制物理页）。
  if(uvmcopy(p->pagetable, np->pagetable, p->sz) < 0){
    proc_vmas_release(np);
    acquire(&np->lock);
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  if(vma_copy(np, p) < 0){
    proc_vmas_release(np);
    acquire(&np->lock);
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // 复制保存的用户寄存器现场，使子进程从同样的位置继续执行。
  *(np->trapframe) = *(p->trapframe);

  // 把子进程的 a0 设为 0，实现“子进程 fork 返回 0”。
  np->trapframe->a0 = 0;

  // fork 得到独立文件描述符表，但底层 file 对象共享。
  np->files = proc_files_alloc();
  if(np->files == 0){
    proc_vmas_release(np);
    acquire(&np->lock);
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  proc_files_copy(np->files, p->files);

  // fork 得到独立文件系统上下文，但 cwd 指向同一目录 inode。
  np->fs = proc_fs_alloc();
  if(np->fs == 0){
    acquire(&np->lock);
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  proc_fs_copy(np->fs, p->fs);

  np->sig = proc_sig_alloc();
  if(np->sig == 0){
    acquire(&np->lock);
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  proc_sig_copy(np->sig, p->sig);
  np->sigblocked = p->sigblocked;

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
  np->tgid = pid;
  np->pgid = p->pgid;

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  module_notify_proc_fork(np);

  return pid;
}

// clone：创建共享父进程地址空间的轻量线程。
// 新线程拥有独立 trapframe、内核栈和页表根，但叶页映射同一物理页。
// 子线程从用户库 clone_stub 开始执行 fn(arg)，不依赖父进程栈。
int
kclone(uint64 flags, uint64 fn, uint64 arg, uint64 stack, uint64 stub)
{
  struct proc *np;
  struct proc *p = myproc();
  int pid;

  if(stack == 0 || stack >= MAXVA || stack % 16 != 0 ||
     fn == 0 || fn >= MAXVA || stub == 0 || stub >= MAXVA ||
     (flags & ~(CLONE_VM | CLONE_FS | CLONE_FILES | CLONE_THREAD)) != 0)
    return -1;
  if((flags & CLONE_THREAD) && !(flags & CLONE_VM))
    return -1;

  if((np = allocproc()) == 0)
    return -1;

  if(p->vmas == 0 || p->sig == 0 || p->files == 0 || p->fs == 0){
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  release(&np->lock);

  int r = 0;
  if(flags & CLONE_VM)
    r = uvmshare(p->pagetable, np->pagetable, p->sz);
  else
    r = uvmcopy(p->pagetable, np->pagetable, p->sz);
  if(r < 0){
    acquire(&np->lock);
    freeproc(np);
    release(&np->lock);
    return -1;
  }
  np->sz = p->sz;

  // 子线程直接进入 clone_stub，寄存器传递 fn/arg，并使用新栈。
  *(np->trapframe) = *(p->trapframe);
  np->trapframe->epc = stub;
  np->trapframe->a0 = fn;
  np->trapframe->a1 = arg;
  np->trapframe->sp = stack;

  // VMA 表与地址空间语义保持一致：CLONE_VM 共享，否则复制。
  if(flags & CLONE_VM){
    np->vmas = p->vmas;
    proc_vmas_share(p->vmas);
  } else {
    np->vmas = proc_vmas_alloc();
    if(np->vmas == 0 || vma_copy(np, p) < 0){
      acquire(&np->lock);
      freeproc(np);
      release(&np->lock);
      return -1;
    }
  }

  // CLONE_FILES 共享文件描述符表，否则复制一份。
  if(flags & CLONE_FILES){
    np->files = p->files;
    proc_files_share(p->files);
  } else {
    np->files = proc_files_alloc();
    if(np->files == 0){
      acquire(&np->lock);
      freeproc(np);
      release(&np->lock);
      return -1;
    }
    proc_files_copy(np->files, p->files);
  }

  // CLONE_FS 共享 cwd，否则复制一份文件系统上下文。
  if(flags & CLONE_FS){
    np->fs = p->fs;
    proc_fs_share(p->fs);
  } else {
    np->fs = proc_fs_alloc();
    if(np->fs == 0){
      acquire(&np->lock);
      freeproc(np);
      release(&np->lock);
      return -1;
    }
    proc_fs_copy(np->fs, p->fs);
  }

  // CLONE_THREAD 加入调用者线程组，并共享信号处理状态。
  if(flags & CLONE_THREAD){
    np->sig = p->sig;
    proc_sig_share(p->sig);
  } else {
    np->sig = proc_sig_alloc();
    if(np->sig == 0){
      acquire(&np->lock);
      freeproc(np);
      release(&np->lock);
      return -1;
    }
    proc_sig_copy(np->sig, p->sig);
  }
  np->sigblocked = p->sigblocked;

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
  if(flags & CLONE_THREAD)
    np->tgid = p->tgid;
  else
    np->tgid = pid;
  np->pgid = p->pgid;

  acquire(&wait_lock);
  np->parent = p;
  release(&wait_lock);

  acquire(&np->lock);
  np->state = RUNNABLE;
  release(&np->lock);

  module_notify_proc_fork(np);
  return pid;
}

static void
kthreadret(void)
{
  struct proc *p = myproc();
  void (*fn)(void*) = (void (*)(void*))p->kthread_fn;
  void *arg = (void*)p->kthread_arg;

  // 与 forkret() 一样，首次调度进入时仍持有 p->lock。
  release(&p->lock);
  fn(arg);
  kexit(0);
}

// 创建内核线程：复用 proc 表和调度器，但不分配用户页表/trapframe。
// 内核线程从 kthreadret 开始执行 fn(arg)，返回后自动退出。
int
kthread_create(void (*fn)(void*), void *arg)
{
  struct proc *p;
  int pid;

  if(fn == 0)
    return -1;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->state != UNUSED){
      release(&p->lock);
      continue;
    }
    p->pid = allocpid();
    p->tgid = 0;
    p->pgid = 0;
    p->priority = 50;
    p->qlevel = 0;
    p->qticks = 0;
    p->is_kthread = 1;
    p->tgid = p->pid;
    p->pgid = p->pid;
    p->kthread_fn = (uint64)fn;
    p->kthread_arg = (uint64)arg;
    p->uid = 0;
    p->euid = 0;
    p->gid = 0;
    p->egid = 0;
    p->umask = 022;
    p->trapframe = 0;
    p->pagetable = 0;
    p->files = 0;
    p->fs = 0;
    p->vmas = 0;
    p->sig = 0;
    p->sz = 0;
    p->sigpending = 0;
    p->sigblocked = 0;
    p->sigblocked_saved = 0;
    p->sigactive = 0;
    p->stop_pending = 0;
    p->continued = 0;
    p->state = USED;

    memset(&p->context, 0, sizeof(p->context));
    p->context.ra = (uint64)kthreadret;
    p->context.sp = p->kstack + PGSIZE;
    pid = p->pid;
    release(&p->lock);

    acquire(&wait_lock);
    p->parent = myproc();
    release(&wait_lock);

    acquire(&p->lock);
    p->state = RUNNABLE;
    release(&p->lock);
    return pid;
  }
  return -1;
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

// 终止并回收同 tgid 的其他线程。
// 组内任意线程都可以调用；exiting 标志保证并发退出时只有一个
// 线程执行等待回收，其余线程直接进入 ZOMBIE。
static void
thread_group_exit(struct proc *p)
{
  struct proc *q;
  int already = 0;

  acquire(&p->sig->lock);
  if(p->sig->exiting)
    already = 1;
  else
    p->sig->exiting = 1;
  release(&p->sig->lock);
  if(already)
    return;

  // 先标记所有兄弟线程 killed，并唤醒睡眠中的线程。
  for(q = proc; q < &proc[NPROC]; q++){
    if(q == p || q->tgid != p->tgid || q->state == UNUSED)
      continue;
    acquire(&q->lock);
    if(q->state != UNUSED){
      q->killed = 1;
      if(q->state == SLEEPING || q->state == STOPPED)
        q->state = RUNNABLE;
    }
    release(&q->lock);
  }

  // 逐个等待兄弟线程进入 ZOMBIE 并回收，避免共享页表提前释放。
  for(q = proc; q < &proc[NPROC]; q++){
    if(q == p || q->tgid != p->tgid || q->state == UNUSED)
      continue;
    acquire(&wait_lock);
    for(;;){
      acquire(&q->lock);
      if(q->state == ZOMBIE){
        freeproc(q);
        release(&q->lock);
        break;
      }
      release(&q->lock);
      sleep(p->sig, &wait_lock);
    }
    release(&wait_lock);
  }
}

// 当前进程退出，函数不会返回。
// 退出后进程进入 ZOMBIE 状态，保留 pid 和退出状态，
// 直到父进程调用 wait() 回收资源。
void
kexit(int status)
{
  struct proc *p = myproc();
  struct proc_sig *sig = p->sig;

  if(p == initproc)
    panic("init exiting");

  // 组长退出即整个线程组退出；内核线程没有共享信号状态，直接退出。
  if(p->tgid == p->pid && p->sig != 0)
    thread_group_exit(p);

  // 最后一个线程/进程退出时关闭共享文件表、cwd 与地址空间资源。
  proc_files_release(p->files);
  p->files = 0;
  proc_fs_release(p->fs);
  p->fs = 0;

  proc_vmas_release(p);

  acquire(&wait_lock);

  // 把子进程交给 init 收养。
  reparent(p);

  // 唤醒可能在 wait() 中睡眠的父进程。
  wakeup(p->parent);
  
  acquire(&p->lock);

  p->xstate = status;
  p->state = ZOMBIE;

  // 唤醒正在等待同组线程回收的线程。
  if(sig)
    wakeup(sig);
  proc_sig_release(sig);
  p->sig = 0;
  module_notify_proc_exit(p);

  release(&wait_lock);

  // 切换到调度器，之后不再回到本进程。
  sched();
  panic("zombie exit");
}

// exit_group：终止整个线程组。
// 由组内任意线程调用，成功后当前线程成为新组长并以 status 退出。
void
kexit_group(int status)
{
  struct proc *p = myproc();
  struct proc *q;

  if(p->sig == 0){
    kexit(status);
    return;
  }

  // 非组长调用 exit_group 时，把当前线程托管给原组长所在的父进程，
  // 这样父进程仍能 wait 到整个线程组的退出状态。
  if(p->tgid != p->pid){
    acquire(&wait_lock);
    for(q = proc; q < &proc[NPROC]; q++){
      if(q->state != UNUSED && q->pid == p->tgid){
        p->parent = q->parent;
        break;
      }
    }
    release(&wait_lock);
  }

  thread_group_exit(p);
  p->tgid = p->pid;
  kexit(status);
}

// exec 前的线程组清理：终止其他线程，让当前线程成为新的组长。
// 只有 exec 已确认成功后才会调用，失败路径不会破坏线程组。
void
kexec_thread_cleanup(void)
{
  struct proc *p = myproc();
  struct proc *q;

  if(p->sig == 0)
    return;

  // 非组长线程 exec 后成为新组长，父进程也改为原组长所在父进程。
  if(p->tgid != p->pid){
    acquire(&wait_lock);
    for(q = proc; q < &proc[NPROC]; q++){
      if(q->state != UNUSED && q->pid == p->tgid){
        p->parent = q->parent;
        break;
      }
    }
    release(&wait_lock);
  }

  thread_group_exit(p);
  p->tgid = p->pid;

  // 线程组已变为单线程，后续 clone 应重新获得正常组退出语义。
  acquire(&p->sig->lock);
  p->sig->exiting = 0;
  release(&p->sig->lock);
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

// 等待指定 pid 的子进程/线程退出，用于 thread join。
int
kwaitpid(int pid, uint64 addr)
{
  return kwaitpid_flags(pid, addr, 0);
}

// 带选项等待指定 pid：WUNTRACED 报告停止，WCONTINUED 报告继续。
int
kwaitpid_flags(int pid, uint64 addr, int options)
{
  struct proc *pp;
  struct proc *p = myproc();
  int found;

  if(pid <= 0 || (options & ~(WUNTRACED | WCONTINUED | WNOHANG)))
    return -1;

  acquire(&wait_lock);
  for(;;){
    found = 0;
    for(pp = proc; pp < &proc[NPROC]; pp++){
      if(pp->parent == p && pp->pid == pid){
        acquire(&pp->lock);
        found = 1;
        if(pp->state == ZOMBIE){
          int xpid = pp->pid;
          if(addr != 0 && copyout(p->pagetable, addr, (char *)&pp->xstate,
                                  sizeof(pp->xstate)) < 0){
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          freeproc(pp);
          release(&pp->lock);
          release(&wait_lock);
          return xpid;
        }
        if(pp->continued && (options & WCONTINUED)){
          int st = XV6_WCONTINUED;
          pp->continued = 0;
          if(addr != 0 &&
             copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0){
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          release(&pp->lock);
          release(&wait_lock);
          return pp->pid;
        }
        if(pp->state == STOPPED && (options & WUNTRACED)){
          int st = XV6_WSTOPPED;
          if(addr != 0 &&
             copyout(p->pagetable, addr, (char *)&st, sizeof(st)) < 0){
            release(&pp->lock);
            release(&wait_lock);
            return -1;
          }
          release(&pp->lock);
          release(&wait_lock);
          return pp->pid;
        }
        release(&pp->lock);
      }
    }
    if(options & WNOHANG){
      release(&wait_lock);
      return -1;
    }
    if(!found || killed(p)){
      release(&wait_lock);
      return -1;
    }
    sleep(p, &wait_lock);
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

static uint64
sig_encode_handler(uint64 h)
{
  if(h == SIG_DFL)
    return 2;
  if(h == SIG_IGN)
    return 3;
  return h + 16;
}

static uint64
sig_decode_handler(uint64 h)
{
  if(h == 0 || h == 2)
    return SIG_DFL;
  if(h == 3)
    return SIG_IGN;
  return h - 16;
}

// 标准信号的默认动作。
int
sig_default_kind(int sig)
{
  switch(sig){
  case SIGCONT:
    return SIGACT_CONT;
  case SIGCHLD:
  case SIGURG:
    return SIGACT_IGN;
  case SIGSTOP:
  case SIGTSTP:
  case SIGTTIN:
  case SIGTTOU:
    return SIGACT_STOP;
  default:
    return SIGACT_TERM;
  }
}

// 设置当前进程的信号处理函数。
int
ksignal(int sig, uint64 handler)
{
  struct proc *p = myproc();

  if(sig <= 0 || sig >= NSIG || p->sig == 0)
    return -1;
  if(sig == SIGKILL || sig == SIGSTOP)
    return -1;
  acquire(&p->sig->lock);
  p->sig->handlers[sig] = sig_encode_handler(handler);
  p->sig->masks[sig] = 0;
  p->sig->flags[sig] = 0;
  release(&p->sig->lock);
  return 0;
}

uint64
sys_sigaction(void)
{
  struct proc *p = myproc();
  struct sigaction act, old;
  int sig;
  uint64 actaddr, oldaddr;

  argint(0, &sig);
  argaddr(1, &actaddr);
  argaddr(2, &oldaddr);
  if(sig <= 0 || sig >= NSIG || p->sig == 0)
    return -1;
  if(sig == SIGKILL || sig == SIGSTOP)
    return -1;
  if(actaddr != 0 &&
     copyin(p->pagetable, (char*)&act, actaddr, sizeof(act)) < 0)
    return -1;

  acquire(&p->sig->lock);
  old.sa_handler = sig_decode_handler(p->sig->handlers[sig]);
  old.sa_mask = p->sig->masks[sig];
  old.sa_flags = p->sig->flags[sig];
  if(oldaddr != 0 &&
     copyout(p->pagetable, oldaddr, (char*)&old, sizeof(old)) < 0){
    release(&p->sig->lock);
    return -1;
  }
  if(actaddr != 0){
    p->sig->handlers[sig] = sig_encode_handler(act.sa_handler);
    p->sig->masks[sig] = act.sa_mask & ~(1UL << SIGKILL) & ~(1UL << SIGSTOP);
    p->sig->flags[sig] = act.sa_flags;
  }
  release(&p->sig->lock);
  return 0;
}

uint64
sys_sigpending(void)
{
  struct proc *p = myproc();
  uint64 setaddr, pending;

  argaddr(0, &setaddr);
  pending = p->sigpending;
  if(p->sig){
    acquire(&p->sig->lock);
    pending |= p->sig->pending;
    release(&p->sig->lock);
  }
  if(copyout(p->pagetable, setaddr, (char*)&pending, sizeof(pending)) < 0)
    return -1;
  return 0;
}

// 向单个线程递送信号：默认动作按标准语义处理，自定义处理函数进入待处理位图。
static int
signal_deliver_thread(struct proc *p, int sig)
{
  uint64 h;
  int d;

  if(p == 0 || p->sig == 0 || sig <= 0 || sig >= NSIG)
    return -1;

  if(sig == SIGKILL){
    acquire(&p->lock);
    p->killed = 1;
    if(p->state == SLEEPING || p->state == STOPPED)
      p->state = RUNNABLE;
    release(&p->lock);
    return 0;
  }

  if(sig == SIGSTOP){
    acquire(&p->lock);
    p->stop_pending = 1;
    if(p->state == SLEEPING)
      p->state = RUNNABLE;
    release(&p->lock);
    return 0;
  }
  if(sig == SIGCONT){
    acquire(&p->lock);
    p->stop_pending = 0;
    if(p->state == STOPPED){
      p->state = RUNNABLE;
      p->continued = 1;
    }
    release(&p->lock);
    wakeup(p->parent);
    return 0;
  }

  acquire(&p->sig->lock);
  h = p->sig->handlers[sig];
  release(&p->sig->lock);

  if(h == 3) // SIG_IGN
    return 0;

  if(h == 0 || h == 2){ // SIG_DFL
    d = sig_default_kind(sig);
    if(d == SIGACT_IGN)
      return 0;
    acquire(&p->lock);
    if(d == SIGACT_STOP){
      p->stop_pending = 1;
    } else {
      p->killed = 1;
    }
    if(p->state == SLEEPING)
      p->state = RUNNABLE;
    release(&p->lock);
    return 0;
  }

  acquire(&p->lock);
  p->sigpending |= (1UL << sig);
  if(p->state == SLEEPING)
    p->state = RUNNABLE;
  release(&p->lock);
  return 0;
}

// 向整个线程组递送信号：优先投递给未阻塞该信号的线程；
// 全部阻塞时放入共享线程组待处理位图。
static int
signal_group(struct proc *leader, int sig)
{
  struct proc *p;

  if(leader == 0 || leader->sig == 0 || sig <= 0 || sig >= NSIG)
    return -1;

  if(sig == SIGKILL){
    for(p = proc; p < &proc[NPROC]; p++){
      if(p->state != UNUSED && p->tgid == leader->tgid)
        signal_deliver_thread(p, SIGKILL);
    }
    return 0;
  }
  if(sig == SIGSTOP || sig == SIGCONT || sig == SIGTSTP ||
     sig == SIGTTIN || sig == SIGTTOU){
    for(p = proc; p < &proc[NPROC]; p++){
      if(p->state != UNUSED && p->tgid == leader->tgid)
        signal_deliver_thread(p, sig);
    }
    return 0;
  }

  for(p = proc; p < &proc[NPROC]; p++){
    int blocked;

    if(p->state == UNUSED || p->tgid != leader->tgid)
      continue;
    acquire(&p->lock);
    blocked = (p->sigblocked & (1UL << sig)) != 0;
    release(&p->lock);
    if(!blocked)
      return signal_deliver_thread(p, sig);
  }

  acquire(&leader->sig->lock);
  leader->sig->pending |= (1UL << sig);
  release(&leader->sig->lock);
  return 0;
}

// 向指定进程/线程组发送信号。
int
ksigkill(int pid, int sig)
{
  struct proc *p, *leader = 0;

  if(pid < 0)
    return kkillpg(-pid, sig);
  if(sig <= 0 || sig >= NSIG)
    return -1;

  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED)
      continue;
    if(p->pid == pid && p->tgid != pid)
      return signal_deliver_thread(p, sig);
    if(p->pid == pid && p->tgid == pid)
      leader = p;
    if(leader == 0 && p->tgid == pid)
      leader = p;
  }
  if(leader == 0)
    return -1;
  return signal_group(leader, sig);
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
  p->sigblocked = p->sigblocked_saved;
  *p->trapframe = p->sigframe;
  p->sigactive = 0;
  return 0;
}

uint64
sys_sigprocmask(void)
{
  struct proc *p = myproc();
  uint64 setaddr, oldsetaddr, set = 0, old, nset;
  int how;

  argint(0, &how);
  argaddr(1, &setaddr);
  argaddr(2, &oldsetaddr);
  if(how < SIG_BLOCK || how > SIG_SETMASK)
    return -1;
  if(setaddr != 0 &&
     copyin(p->pagetable, (char*)&set, setaddr, sizeof(set)) < 0)
    return -1;
  if(set & ((1UL << SIGKILL) | (1UL << SIGSTOP)))
    return -1;

  old = p->sigblocked;
  if(oldsetaddr != 0 &&
     copyout(p->pagetable, oldsetaddr, (char*)&old, sizeof(old)) < 0)
    return -1;

  switch(how){
  case SIG_BLOCK:
    nset = old | set;
    break;
  case SIG_UNBLOCK:
    nset = old & ~set;
    break;
  default:
    nset = set;
    break;
  }
  p->sigblocked = nset & ~(1UL << SIGKILL);
  return 0;
}

// 设置进程组：pid 为 0 时表示当前进程；pgid 为 0 时新建以 pid 为组长的新组。
uint64
sys_setpgid(void)
{
  struct proc *p = myproc();
  struct proc *target = p;
  int pid, pgid, found = 0;

  argint(0, &pid);
  argint(1, &pgid);
  if(pgid < 0)
    return -1;
  if(pid == 0)
    pid = p->pid;

  if(pid != p->pid){
    acquire(&wait_lock);
    for(struct proc *q = proc; q < &proc[NPROC]; q++){
      if(q->state != UNUSED && q->pid == pid && q->parent == p){
        target = q;
        found = 1;
        break;
      }
    }
    release(&wait_lock);
    if(!found)
      return -1;
  }

  if(pgid == 0)
    pgid = pid;
  if(pgid != pid){
    for(struct proc *q = proc; q < &proc[NPROC]; q++){
      if(q->state != UNUSED && q->pid == pgid){
        found = 1;
        break;
      }
    }
    if(!found)
      return -1;
  }

  acquire(&target->lock);
  target->pgid = pgid;
  release(&target->lock);
  return 0;
}

uint64
sys_getpgid(void)
{
  struct proc *p = myproc();
  int pid;

  argint(0, &pid);
  if(pid == 0)
    pid = p->pid;
  for(struct proc *q = proc; q < &proc[NPROC]; q++){
    if(q->pid != pid)
      continue;
    acquire(&q->lock);
    if(q->state != UNUSED){
      int pgid = q->pgid;
      release(&q->lock);
      return pgid;
    }
    release(&q->lock);
  }
  return -1;
}

// 向整个进程组投递信号；sig == 0 时只检查进程组是否存在。
int
kkillpg(int pgrp, int sig)
{
  struct proc *p;
  int found = 0;

  if(pgrp <= 0 || sig < 0 || sig >= NSIG)
    return -1;

  for(p = proc; p < &proc[NPROC]; p++){
    if(p->state == UNUSED || p->pid != p->tgid || p->pgid != pgrp)
      continue;
    found = 1;
    if(sig == 0)
      return 0;
    if(ksigkill(p->pid, sig) < 0)
      return -1;
  }
  return found ? 0 : -1;
}

uint64
sys_killpg(void)
{
  int pgrp, sig;

  argint(0, &pgrp);
  argint(1, &sig);
  return kkillpg(pgrp, sig);
}

uint64
sys_tcsetpgrp(void)
{
  int fd, pgid;

  argint(0, &fd);
  argint(1, &pgid);
  if(fd < 0)
    return -1;
  return console_set_fg(pgid);
}

uint64
sys_tcgetpgrp(void)
{
  int fd;

  argint(0, &fd);
  if(fd < 0)
    return -1;
  return console_get_fg();
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
static int
sig_pending_unblocked(struct proc *p)
{
  uint64 pending = p->sigpending;

  if(p->sig){
    acquire(&p->sig->lock);
    pending |= p->sig->pending;
    release(&p->sig->lock);
  }
  return (pending & ~p->sigblocked) != 0;
}

void
sleep(void *chan, struct spinlock *lk)
{
  struct proc *p = myproc();
  
  // 必须先持有 p->lock 才能修改状态并调用 sched()。
  // wakeup() 也会获取 p->lock，因此持有 p->lock 期间释放 lk 是安全的，
  // 不会出现“错过唤醒”的竞态。

  acquire(&p->lock);  //DOC: sleeplock1
  release(lk);

  // 已有可投递信号时不进入睡眠，让调用者返回用户态后先处理信号。
  if(p->stop_pending || sig_pending_unblocked(p)){
    release(&p->lock);
    acquire(lk);
    return;
  }

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
  int found = 0;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->state != UNUSED && (p->pid == pid || p->tgid == pid)){
      p->killed = 1;
      if(p->state == SLEEPING || p->state == STOPPED){
        // 被 kill 的睡眠进程也要唤醒，让它有机会检查 killed。
        p->state = RUNNABLE;
      }
      found = 1;
    }
    release(&p->lock);
  }
  return found ? 0 : -1;
}

// 精确向线程组中指定 tid 递送信号；sig==0 只做存在性检查。
int
ktgkill(int tgid, int tid, int sig)
{
  struct proc *p;

  if(sig < 0 || sig >= NSIG)
    return -1;

  for(p = proc; p < &proc[NPROC]; p++){
    acquire(&p->lock);
    if(p->state != UNUSED && p->tgid == tgid && p->pid == tid){
      release(&p->lock);
      if(sig == 0)
        return 0;
      return signal_deliver_thread(p, sig);
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
  [ZOMBIE]    "zombie",
  [STOPPED]   "stop  "
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
