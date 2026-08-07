// 内核上下文切换时保存的寄存器集合。
#include "signal.h"
#include "vma.h"
#include "sleeplock.h"
// ra 保存切换后的返回地址，sp 保存内核栈指针；
// s0-s11 是 callee-saved 寄存器，调用者不负责保留它们，
// 所以上下文切换必须显式保存和恢复。
struct context {
  uint64 ra;
  uint64 sp;

  // callee-saved
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};

// 每个 CPU 一份的状态。
// scheduler() 选择进程后，通过 swtch() 保存当前上下文，
// 把控制权交给进程；进程再次让出 CPU 时又切换回这个 context。
struct cpu {
  struct proc *proc;          // The process running on this cpu, or null.
  struct context context;     // swtch() here to enter scheduler().
  int noff;                   // Depth of push_off() nesting.
  int intena;                 // Were interrupts enabled before push_off()?
};

extern struct cpu cpus[NCPU];

// 每个进程的陷阱帧，供 trampoline.S 中的用户态陷阱入口/出口使用。
// 它独占一页，映射在用户页表 trampoline 页的下方。
//
// 陷入内核时，用户寄存器会被保存到这里；
// uservec 再从 trapframe 中读出 kernel_satp、kernel_sp、
// kernel_hartid 和 kernel_trap，从而切换到内核栈并进入 usertrap()。
// 返回用户态时，userret 用 trapframe 恢复用户寄存器并切回用户页表。
// 之所以保存 s0-s11，是因为从 usertrapret() 返回用户态时
// 不会沿着整个内核调用栈逐层返回。
struct trapframe {
  /*   0 */ uint64 kernel_satp;   // kernel page table
  /*   8 */ uint64 kernel_sp;     // top of process's kernel stack
  /*  16 */ uint64 kernel_trap;   // usertrap()
  /*  24 */ uint64 epc;           // saved user program counter
  /*  32 */ uint64 kernel_hartid; // saved kernel tp
  /*  40 */ uint64 ra;
  /*  48 */ uint64 sp;
  /*  56 */ uint64 gp;
  /*  64 */ uint64 tp;
  /*  72 */ uint64 t0;
  /*  80 */ uint64 t1;
  /*  88 */ uint64 t2;
  /*  96 */ uint64 s0;
  /* 104 */ uint64 s1;
  /* 112 */ uint64 a0;
  /* 120 */ uint64 a1;
  /* 128 */ uint64 a2;
  /* 136 */ uint64 a3;
  /* 144 */ uint64 a4;
  /* 152 */ uint64 a5;
  /* 160 */ uint64 a6;
  /* 168 */ uint64 a7;
  /* 176 */ uint64 s2;
  /* 184 */ uint64 s3;
  /* 192 */ uint64 s4;
  /* 200 */ uint64 s5;
  /* 208 */ uint64 s6;
  /* 216 */ uint64 s7;
  /* 224 */ uint64 s8;
  /* 232 */ uint64 s9;
  /* 240 */ uint64 s10;
  /* 248 */ uint64 s11;
  /* 256 */ uint64 t3;
  /* 264 */ uint64 t4;
  /* 272 */ uint64 t5;
  /* 280 */ uint64 t6;
};

// 共享的文件描述符表。
// 普通进程各自持有一份；clone 线程可共享，并增加 ref。
struct proc_files {
  struct spinlock lock;
  int ref;
  struct file *ofile[NOFILE];
};

// 共享的文件系统上下文：当前工作目录。
// 与文件描述符表分开，使 CLONE_FILES 和 CLONE_FS 可以独立生效。
struct proc_fs {
  struct spinlock lock;
  int ref;
  struct inode *cwd;
};

// 共享的信号状态。clone 线程共享处理函数表和线程组待处理信号，
// 每个线程仍保留自己的阻塞掩码和 per-thread 待处理位图。
struct proc_sig {
  struct spinlock lock;
  int ref;
  int exiting;                 // 线程组退出流程已开始，避免重复等待
  uint64 handlers[NSIG]; // 信号处理函数表
  uint64 masks[NSIG];   // 进入处理器时额外阻塞的信号
  int flags[NSIG];      // SA_NODEFER / SA_RESETHAND 等
  uint64 pending;        // 线程组待处理信号
};

// 共享的 VMA 表。clone 线程共享同一份，并增加 ref。
struct proc_vmas {
  struct sleeplock lock;
  int ref;
  uint64 pages[VMA_PAGE_COUNT]; // 每页缓存物理地址，持有一份 cow 引用
  struct vma vmas[NVMA];
};

// 进程状态机：
// UNUSED（空槽位）-> USED（已分配）-> RUNNABLE -> RUNNING，
// RUNNING 可以因等待资源进入 SLEEPING，或因退出进入 ZOMBIE；
// 收到停止信号后进入 STOPPED，SIGCONT 再恢复为 RUNNABLE。
enum procstate { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE, STOPPED };

// MLFQ 队列数量与各队列时间片（时钟 tick 数）。
#define MLFQ_NQUEUES 3
#define MLFQ_SLICE0 2
#define MLFQ_SLICE1 4
#define MLFQ_SLICE2 8

// 每个进程的完整状态（进程控制块 PCB）。
struct proc {
  struct spinlock lock;

  // 以下字段必须持有 p->lock 时才能访问：
  enum procstate state;        // Process state
  void *chan;                  // 非 0 时表示正在等待的唤醒通道
  int killed;                  // 非 0 表示已被 kill
  int stop_pending;            // 非 0 表示有等待生效的停止信号
  int continued;               // 非 0 表示刚被 SIGCONT 继续
  int xstate;                  // 退出状态，等待父进程 wait() 读取
  int pid;                     // Process ID
  int tgid;                    // Thread group ID，普通进程等于 pid
  int pgid;                    // Process group ID，普通进程默认等于 pid
  int priority;                // 调度优先级，0 最高，255 最低
  int qlevel;                  // MLFQ 当前队列，0 最高
  int qticks;                  // 当前队列已运行 tick 数
  int is_kthread;              // 是否为内核线程
  uint64 kthread_fn;           // 内核线程入口函数
  uint64 kthread_arg;          // 内核线程参数
  ushort uid;                  // 真实用户 ID
  ushort euid;                 // 有效用户 ID，权限检查使用
  ushort gid;                  // 真实组 ID
  ushort egid;                 // 有效组 ID
  uint umask;                  // 新建文件时屏蔽的权限位
  struct proc_sig *sig;        // 共享信号处理表与线程组待处理信号
  uint64 sigpending;           // 当前线程待处理信号位图
  uint64 sigblocked;           // 当前线程信号阻塞掩码
  uint64 sigblocked_saved;     // 进入信号处理前保存的阻塞掩码
  struct trapframe sigframe;   // 进入信号处理前保存的用户现场
  int sigactive;               // 当前是否正在执行信号处理函数

  // 访问 parent 时必须持有 wait_lock：
  struct proc *parent;         // 父进程

  // 以下字段只在进程自身执行时使用，因此不一定需要 p->lock：
  uint64 kstack;               // 内核栈的虚拟地址
  uint64 sz;                   // 用户内存大小（字节）
  pagetable_t pagetable;       // 用户页表
  struct trapframe *trapframe; // 保存用户寄存器现场的页面
  struct context context;      // 内核上下文，swtch() 保存/恢复
  struct proc_files *files;    // 文件描述符表（可被 clone 共享）
  struct proc_fs *fs;          // 文件系统上下文，cwd（可被 clone 共享）
  struct proc_vmas *vmas;      // mmap 虚拟内存区域（可被 clone 共享）
  char name[16];               // 进程名（用于调试输出）
};
