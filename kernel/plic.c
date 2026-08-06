// PLIC（Platform-Level Interrupt Controller）驱动。
//
// PLIC 是 RISC-V 平台级中断控制器：外部设备（UART、virtio 磁盘）
// 发起中断后，PLIC 根据优先级把其中一个中断分发给某个 hart。
// 内核通过 MMIO 寄存器配置哪些中断被启用、当前要处理哪个中断，
// 处理完后再写回 claim/complete 寄存器，允许设备发起下一次中断。
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"

//
// the riscv Platform Level Interrupt Controller (PLIC).
//

void
plicinit(void)
{
  // 为 UART 和 virtio 磁盘设置非 0 优先级，否则中断会被禁用。
  *(uint32*)(PLIC + UART0_IRQ*4) = 1;
  *(uint32*)(PLIC + VIRTIO0_IRQ*4) = 1;
  *(uint32*)(PLIC + VIRTIO1_IRQ*4) = 1;
  *(uint32*)(PLIC + VIRTIO2_IRQ*4) = 1;
}

void
plicinithart(void)
{
  int hart = cpuid();
  
  // 允许当前 hart 的 S-mode 接收 UART 和 virtio 磁盘中断。
  *(uint32*)PLIC_SENABLE(hart) =
    (1 << UART0_IRQ) | (1 << VIRTIO0_IRQ) |
    (1 << VIRTIO1_IRQ) | (1 << VIRTIO2_IRQ);

  // 优先级阈值设为 0，即所有非 0 优先级中断都会触发。
  *(uint32*)PLIC_SPRIORITY(hart) = 0;
}

// 从 PLIC 获取当前需要处理的最高优先级中断编号。
int
plic_claim(void)
{
  int hart = cpuid();
  int irq = *(uint32*)PLIC_SCLAIM(hart);
  return irq;
}

// 通知 PLIC 已完成该中断处理，允许设备再次发起中断。
void
plic_complete(int irq)
{
  int hart = cpuid();
  *(uint32*)PLIC_SCLAIM(hart) = irq;
}
