// 物理内存布局。
//
// QEMU 的 virt 机器按如下方式布置物理地址空间：
// 00001000 -- 引导 ROM
// 02000000 -- CLINT（计时器/软中断控制器）
// 0C000000 -- PLIC（平台级中断控制器）
// 10000000 -- UART0 串口
// 10001000 -- virtio 磁盘
// 80000000 -- QEMU 将内核加载到这里并跳转执行
//
// 内核使用 0x80000000 之后的内存：
// 80000000 -- 内核文本与数据
// end      -- 内核页分配区域的起点
// PHYSTOP  -- 内核使用的 RAM 终点

// UART0 串口寄存器位于物理地址 0x10000000。
#define UART0 0x10000000L
#define UART0_IRQ 10

// virtio 磁盘的 MMIO 接口，位于 0x10001000。
#define VIRTIO0 0x10001000
#define VIRTIO0_IRQ 1

// 第二个 virtio 块设备位于下一个 MMIO 总线槽位。
#define VIRTIO1 0x10002000
#define VIRTIO1_IRQ 2

// PLIC 负责汇总外部设备中断并分发给各 hart。
#define PLIC 0x0c000000L
#define PLIC_PRIORITY (PLIC + 0x0)
#define PLIC_PENDING (PLIC + 0x1000)
#define PLIC_SENABLE(hart) (PLIC + 0x2080 + (hart)*0x100)
#define PLIC_SPRIORITY(hart) (PLIC + 0x201000 + (hart)*0x2000)
#define PLIC_SCLAIM(hart) (PLIC + 0x201004 + (hart)*0x2000)

// 内核可使用的物理内存范围是 0x80000000 到 PHYSTOP。
// 内核页表使用直接映射：虚拟地址 = 物理地址。
#define KERNBASE 0x80000000L
#define PHYSTOP (KERNBASE + 128*1024*1024)
#define NPHYS ((PHYSTOP-KERNBASE)/4096)

// 动态模块固定加载区域，位于内核 BSS 之后。
// 该区域从物理页分配器中保留，避免与普通内核内存冲突。
#define DYNMOD_BASE 0x80040000L
#define DYNMOD_SIZE (8*4096)
#define DYNMOD_NUM 4
#define DYNMOD_AREA_SIZE (DYNMOD_SIZE * DYNMOD_NUM)

// trampoline 是处理陷阱的跳板代码，被同时映射到内核和用户地址空间的最高页。
// 这样用户态陷入内核时，页表切换前后 PC 仍能落在同一段代码上。
#define TRAMPOLINE (MAXVA - PGSIZE)

// 每个进程的内核栈都映射在 TRAMPOLINE 下方，
// 栈之间用无效保护页隔开，防止栈溢出越界。
#define KSTACK(p) (TRAMPOLINE - ((p)+1)* 2*PGSIZE)

// 用户地址空间从低地址到高地址依次为：
//   代码段(text)、数据段、固定大小栈、可扩展堆，
//   然后是 mmap 区域、共享内存区域、TRAPFRAME 和 TRAMPOLINE。
#define TRAPFRAME (TRAMPOLINE - PGSIZE)

// 共享内存映射区域，位于 TRAPFRAME 下方。
#define SHM_BASE (TRAPFRAME - 64*PGSIZE)
#define SHM_MAX_SIZE (32*PGSIZE)

// mmap 私有映射区域，位于共享内存区域下方，从高地址向下分配。
#define MMAP_BASE (SHM_BASE - 256*PGSIZE)
#define MMAP_SIZE (SHM_BASE - MMAP_BASE)
