#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

void main();
void timerinit();

// entry.S needs one stack per CPU.
__attribute__ ((aligned (16))) char stack0[4096 * NCPU];

// start() 是 entry.S 跳转到的机器模式(M-mode)入口。
// 它的任务是：准备 S-mode 运行环境，然后把 CPU 切换到监管者模式(S-mode)，
// 并从 main() 开始执行内核 C 代码。
void
start()
{
  // 把 mstatus 中的 MPP 字段设为 Supervisor。
  // mret 返回时，CPU 会跳转到 mepc 指向的地址，
  // 并以 MPP 指定的特权级继续运行。
  unsigned long x = r_mstatus();
  x &= ~MSTATUS_MPP_MASK;
  x |= MSTATUS_MPP_S;
  w_mstatus(x);

  // 设置异常返回程序计数器为 main()。
  // mret 将跳到该地址，也就是内核的 C 入口。
  // 这里的寻址依赖编译选项 -mcmodel=medany。
  w_mepc((uint64)main);

  // 暂时关闭分页，让 CPU 先直接使用物理地址访问内存。
  w_satp(0);

  // 把异常和中断委托给 S-mode 处理。
  // 这样陷阱发生时，CPU 直接进入内核的 trap 向量，
  // 而不是落在 M-mode 的 trap handler 里。
  w_medeleg(0xffff);
  w_mideleg(0xffff);
  w_sie(r_sie() | SIE_SEIE | SIE_STIE);

  // 配置 Physical Memory Protection (PMP)，
  // 允许 S-mode 访问全部物理内存。
  w_pmpaddr0(0x3fffffffffffffull);
  w_pmpcfg0(0xf);

  // 初始化时钟，使每个 CPU 都能收到定时器中断，
  // 这是进程抢占式调度的时间来源。
  timerinit();

  // 把当前 CPU 的 hartid 保存在 tp 寄存器中，
  // 内核代码可用 cpuid() 快速获取“我是哪个 CPU”。
  int id = r_mhartid();
  w_tp(id);

  // 切换到 S-mode 并跳入 main()。
  asm volatile("mret");
}

// 让每个 hart 产生 S-mode 时钟中断。
void
timerinit()
{
  // 允许 S-mode 定时器中断。
  w_mie(r_mie() | MIE_STIE);
  
  // 启用 sstc 扩展，即使用 stimecmp 寄存器比较时钟值。
  w_menvcfg(r_menvcfg() | (1L << 63)); 
  
  // 允许 S-mode 读取 time 并写 stimecmp。
  w_mcounteren(r_mcounteren() | 2);
  
  // 设置第一次时钟中断的阈值：当前时间 + 1000000。
  // 之后每次时钟中断处理都会重新设置 stimecmp，形成周期性调度时钟。
  w_stimecmp(r_time() + 1000000);
}
