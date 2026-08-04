// 陷阱与中断处理。
//
// RISC-V 把“CPU 主动或被动改变控制流”统称为陷阱(trap)，这里主要分三类：
// 1. 系统调用：用户程序执行 ecall 进入内核；
// 2. 异常：如缺页、非法指令等；
// 3. 中断：定时器中断和外部设备中断。
//
// 用户态陷阱的入口在 trampoline.S（uservec），内核态陷阱的入口
// 在 kernelvec.S（kernelvec）。trap.c 里的 usertrap() 和 kerneltrap()
// 分别是这两条路径的 C 语言处理函数。
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

struct spinlock tickslock;
uint ticks;

extern char trampoline[], uservec[];

// in kernelvec.S, calls kerneltrap().
void kernelvec();

extern int devintr();

void
trapinit(void)
{
  // ticks 是全局时钟计数，用自旋锁保护。
  initlock(&tickslock, "time");
}

// 让当前 CPU 的内核态陷阱跳转到 kernelvec。
// 内核运行期间发生异常/中断时，硬件会读取 stvec 作为处理入口。
void
trapinithart(void)
{
  w_stvec((uint64)kernelvec);
}

// 在返回用户态前交付一个待处理信号。
static void
deliver_signal(struct proc *p)
{
  for(int sig = 1; sig < NSIG; sig++){
    uint64 h = p->sighandlers[sig];
    if((p->sigpending & (1UL << sig)) == 0 || h == 0)
      continue;
    // handler + 16 存储：2 表示 SIG_DFL，3 表示 SIG_IGN。
    if(h == 3){
      p->sigpending &= ~(1UL << sig);
      continue;
    }
    if(h == 2){
      p->sigpending &= ~(1UL << sig);
      p->killed = 1;
      continue;
    }
    // 信号处理函数执行期间不再重入，新信号留到 sigreturn 后交付。
    if(p->sigactive)
      continue;
    {
      p->sigpending &= ~(1UL << sig);
      p->sigactive = 1;
      p->sigframe = *p->trapframe;
      p->trapframe->epc = h - 16;
      p->trapframe->a0 = sig;
      return;
    }
  }
}

//
// 处理来自用户态的陷阱：中断、异常或系统调用。
// 它由 trampoline.S 调用，返回时会把用户页表的 satp 交给 trampoline.S，
// 用于切换回用户地址空间。
//
uint64
usertrap(void)
{
  int which_dev = 0;

  // 检查 SPP：陷阱必须确实来自用户态，否则说明陷阱向量配置出错。
  if((r_sstatus() & SSTATUS_SPP) != 0)
    panic("usertrap: not from user mode");

  // 现在已经在内核中，后续陷阱交给 kerneltrap() 处理。
  w_stvec((uint64)kernelvec);  //DOC: kernelvec

  struct proc *p = myproc();
  
  // 保存用户程序计数器，供返回用户态时恢复。
  p->trapframe->epc = r_sepc();
  
  if(r_scause() == 8){
    // 系统调用：scause == 8 表示用户执行了 ecall。

    if(killed(p))
      kexit(-1);

    // sepc 指向 ecall 指令本身，返回用户态时要执行下一条指令。
    p->trapframe->epc += 4;

    // sepc/scause/sstatus 已经保存完毕，此时才允许开中断，
    // 避免后续中断覆盖这些寄存器。
    intr_on();

    syscall();
  } else if((which_dev = devintr()) != 0){
    // 设备中断或定时器中断，devintr() 已处理。
  } else if(r_scause() == 15 && cow_handle(p->pagetable, r_stval()) == 0){
    // COW 写缺页：复制物理页并转为可写。
  } else if((r_scause() == 15 || r_scause() == 13) &&
            vmfault(p->pagetable, r_stval(), (r_scause() == 13)? 1 : 0) != 0) {
    // 缺页异常：如果访问的是惰性分配的地址，则在此补上物理页。
  } else {
    // 无法识别的陷阱：打印现场并杀死进程。
    printf("usertrap(): unexpected scause 0x%lx pid=%d\n", r_scause(), p->pid);
    printf("            sepc=0x%lx stval=0x%lx\n", r_sepc(), r_stval());
    setkilled(p);
  }

  if(killed(p))
    kexit(-1);

  // 定时器中断时让出 CPU，实现抢占式调度。
  if(which_dev == 2)
    yield();

  deliver_signal(p);

  if(killed(p))
    kexit(-1);

  prepare_return();

  // the user page table to switch to, for trampoline.S
  uint64 satp = MAKE_SATP(p->pagetable);

  // return to trampoline.S; satp value in a0.
  return satp;
}

//
// 准备返回用户态：设置 stvec、sepc、sstatus 和 trapframe 中的
// 内核恢复信息，使下一次用户陷阱能够再次进入 usertrap()。
//
void
prepare_return(void)
{
  struct proc *p = myproc();

  // 即将把陷阱入口改回 uservec；此时若发生内核陷阱，
  // 会错误地进入 usertrap()，所以先关闭中断。
  intr_off();

  // 把下次用户陷阱入口设置到 trampoline.S 中的 uservec。
  uint64 trampoline_uservec = TRAMPOLINE + (uservec - trampoline);
  w_stvec(trampoline_uservec);

  // 预填 trapframe：下次 uservec 启动时需要这些内核恢复信息。
  p->trapframe->kernel_satp = r_satp();         // kernel page table
  p->trapframe->kernel_sp = p->kstack + PGSIZE; // process's kernel stack
  p->trapframe->kernel_trap = (uint64)usertrap;
  p->trapframe->kernel_hartid = r_tp();         // hartid for cpuid()

  // 设置 sret 返回用户态需要的寄存器。
  
  // SPP 置 0：sret 后进入用户态；SPIE 置 1：进入用户态后允许中断。
  unsigned long x = r_sstatus();
  x &= ~SSTATUS_SPP; // SPP 清 0，目标特权级为用户态
  x |= SSTATUS_SPIE; // 允许用户态中断
  w_sstatus(x);

  // sepc 指向保存的用户 PC，sret 从这里继续执行用户代码。
  w_sepc(p->trapframe->epc);
}

// 内核态代码运行期间发生的陷阱，通过 kernelvec 进入这里。
// 它运行在当前进程的内核栈上，因此可以直接使用当前 CPU/进程信息。
void 
kerneltrap()
{
  int which_dev = 0;
  uint64 sepc = r_sepc();
  uint64 sstatus = r_sstatus();
  uint64 scause = r_scause();
  
  // 确认陷阱确实来自 S-mode 内核态。
  if((sstatus & SSTATUS_SPP) == 0)
    panic("kerneltrap: not from supervisor mode");
  // 内核代码通常应在关闭中断的状态下运行，避免嵌套中断破坏现场。
  if(intr_get() != 0)
    panic("kerneltrap: interrupts enabled");

  if((which_dev = devintr()) == 0){
    // 未知的内核态陷阱，直接 panic。
    printf("scause=0x%lx sepc=0x%lx stval=0x%lx\n", scause, r_sepc(), r_stval());
    panic("kerneltrap");
  }

  // 定时器中断时让出 CPU；无当前进程（如调度器中）则只处理中断。
  if(which_dev == 2 && myproc() != 0)
    yield();

  // yield() 期间可能又发生陷阱、覆盖了这些寄存器，
  // 返回 kernelvec.S 前必须恢复原来的 sepc/sstatus。
  w_sepc(sepc);
  w_sstatus(sstatus);
}

void
clockintr()
{
  struct proc *p;
  int lim;

  // 时钟中断处理：CPU 0 维护全局 ticks 并唤醒等待者，
  // 然后安排下一次时钟中断。
  if(cpuid() == 0){
    acquire(&tickslock);
    ticks++;
    wakeup(&ticks);
    release(&tickslock);
    module_notify_tick();
    if(ticks % 100 == 0)
      mlfq_boost();
  }

  // MLFQ 时间片耗尽后，把当前进程降级到下一队列。
  p = myproc();
  if(p){
    acquire(&p->lock);
    if(p->state == RUNNING){
      p->qticks++;
      switch(p->qlevel){
      case 0:
        lim = MLFQ_SLICE0;
        break;
      case 1:
        lim = MLFQ_SLICE1;
        break;
      default:
        lim = MLFQ_SLICE2;
        break;
      }
      if(p->qticks >= lim && p->qlevel < MLFQ_NQUEUES - 1){
        p->qticks = 0;
        p->qlevel++;
      }
    }
    release(&p->lock);
  }

  // 写 stimecmp 安排下一次中断，同时清除当前中断请求。
  // 1000000 个时钟周期约为 0.1 秒。
  w_stimecmp(r_time() + 1000000);
}

// 判断并处理外部中断或定时器中断。
// 返回 2 表示定时器中断，1 表示其他设备中断，0 表示无法识别。
int
devintr()
{
  uint64 scause = r_scause();

  if(scause == 0x8000000000000009L){
    // 监管者外部中断，通过 PLIC 分发到具体设备。

    // plic_claim() 返回发起中断的设备编号。
    int irq = plic_claim();

    if(irq == UART0_IRQ){
      uartintr();
    } else if(irq == VIRTIO0_IRQ){
      virtio_disk_intr();
    } else if(irq){
      printf("unexpected interrupt irq=%d\n", irq);
    }

    // 通知 PLIC 处理完毕，允许该设备再次发起中断。
    if(irq)
      plic_complete(irq);

    return 1;
  } else if(scause == 0x8000000000000005L){
    // 监管者定时器中断。
    clockintr();
    return 2;
  } else {
    return 0;
  }
}
