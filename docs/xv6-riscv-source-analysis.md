# xv6-riscv 源码分析

> **项目**: xv6-riscv — MIT 6.1810 教学操作系统  
> **架构**: RISC-V 64-bit (rv64gc), Sv39 虚拟内存  
> **平台**: QEMU virt 机器, 多核 SMP  
> **分析日期**: 2026-06-12

---

## 目录

1. [项目概述](#1-项目概述)
2. [目录结构与构建系统](#2-目录结构与构建系统)
3. [物理内存布局](#3-物理内存布局)
4. [启动流程](#4-启动流程)
5. [进程管理](#5-进程管理)
6. [虚拟内存管理](#6-虚拟内存管理)
7. [陷阱与中断处理](#7-陷阱与中断处理)
8. [上下文切换](#8-上下文切换)
9. [系统调用](#9-系统调用)
10. [文件系统](#10-文件系统)
11. [锁与同步机制](#11-锁与同步机制)
12. [设备驱动](#12-设备驱动)
13. [用户程序](#13-用户程序)
14. [核心执行流程](#14-核心执行流程)

---

## 1. 项目概述

xv6 是 Dennis Ritchie 和 Ken Thompson 的 Unix Version 6 的重新实现，用于 MIT 操作系统课程 6.1810。本项目是针对 **RISC-V 64 位多处理器** 架构的移植版本，使用 ANSI C 编写。

### 源码行数统计

| 模块 | 文件数 | 核心功能 |
|------|--------|----------|
| 内核核心 | ~22 个 `.c` 文件 + 5 个 `.S` 文件 + 多个 `.h` 文件 | 进程、内存、文件、设备 |
| 用户程序 | ~20 个 `.c` 文件 | shell、工具、测试 |
| 构建脚本 | Makefile + Perl + Python | 编译、链接、镜像制作 |

---

## 2. 目录结构与构建系统

### 2.1 目录组织

```
xv6-riscv/
├── Makefile              # 构建系统
├── README                # 项目说明
├── test-xv6.py           # 自动化测试脚本
├── kernel/               # 内核源码
│   ├── main.c            # 内核入口, 初始化序列
│   ├── start.c           # 机器模式启动代码
│   ├── entry.S           # 内核入口汇编 (跳转到 start)
│   ├── kernel.ld         # 内核链接脚本
│   ├── proc.c / proc.h   # 进程管理
│   ├── vm.c / vm.h       # 虚拟内存管理
│   ├── trap.c            # 陷阱和中断处理
│   ├── syscall.c/.h      # 系统调用分发
│   ├── sysfile.c         # 文件相关系统调用
│   ├── sysproc.c         # 进程相关系统调用
│   ├── spinlock.c/.h     # 自旋锁
│   ├── sleeplock.c/.h    # 睡眠锁
│   ├── fs.c / fs.h       # 文件系统核心
│   ├── bio.c / buf.h     # 块缓存层
│   ├── log.c             # 日志层 (崩溃恢复)
│   ├── file.c / file.h   # 文件描述符管理
│   ├── pipe.c            # 管道实现
│   ├── exec.c            # exec 系统调用
│   ├── kalloc.c          # 物理内存分配器
│   ├── trampoline.S      # 用户态↔内核态跳板
│   ├── kernelvec.S       # 内核态陷阱入口
│   ├── swtch.S           # 上下文切换
│   ├── plic.c            # PLIC 中断控制器
│   ├── virtio_disk.c/.h  # virtio 磁盘驱动
│   ├── uart.c            # UART 串口驱动
│   ├── console.c         # 控制台输入输出
│   ├── printf.c          # 格式化输出
│   ├── string.c          # 字符串/内存操作
│   ├── riscv.h           # RISC-V 特权架构宏定义
│   ├── memlayout.h       # 物理/虚拟内存布局
│   ├── defs.h            # 全内核函数声明
│   └── ...               # 其他头文件
├── user/                 # 用户程序
│   ├── init.c            # 第一个用户进程 (启动 shell)
│   ├── sh.c              # shell
│   ├── usys.pl           # 系统调用入口生成器
│   ├── ulib.c            # 用户库函数
│   ├── umalloc.c         # 用户态内存分配器
│   ├── printf.c          # 用户态 printf
│   └── ...               # 各种工具 (cat, ls, grep, ...)
└── mkfs/
    └── mkfs.c            # 文件系统镜像生成工具
```

### 2.2 Makefile 关键配置

```makefile
# 编译器
CC = $(TOOLPREFIX)gcc          # RISC-V 交叉编译器
LD = $(TOOLPREFIX)ld           # RISC-V 链接器

# 编译器标志
CFLAGS = -Wall -O -ggdb
CFLAGS += -march=rv64gc        # RISC-V 64位, 通用+压缩指令
CFLAGS += -mcmodel=medany      # 中等代码模型
CFLAGS += -ffreestanding       # 独立环境 (无标准库)
CFLAGS += -nostdlib            # 不使用标准库
CFLAGS += -fno-pie             # 禁用位置无关代码

# 链接器
LDFLAGS = -z max-page-size=4096

# QEMU 配置
QEMU = qemu-system-riscv64
CPUS := 3                      # 3 个 CPU 核心
QEMUOPTS = -machine virt -m 128M -smp $(CPUS) -nographic
```

**关键构建目标**:
- `make qemu` — 构建内核 + 文件系统镜像 + 启动 QEMU
- `make qemu-gdb` — 带 GDB 调试的 QEMU
- `kernel/kernel` — 内核 ELF 文件 (链接地址 0x80000000)
- `fs.img` — 文件系统镜像 (包含所有用户程序)

---

## 3. 物理内存布局

### 3.1 QEMU virt 机器物理地址空间

```
物理地址           设备/用途
─────────────────────────────────────
0x00001000         QEMU Boot ROM
0x02000000         CLINT (Core Local Interruptor)
0x0C000000         PLIC (Platform-Level Interrupt Controller)
0x10000000         UART0 串口寄存器
0x10001000         virtio 磁盘 MMIO 接口
0x80000000 ────   内核加载地址 (entry.S)
  │                内核代码段 (.text)
  │                内核只读数据 (.rodata)
  │                内核数据 (.data)
  │                内核 BSS (.bss)
  end ────        内核结束, 物理页分配开始
  │                空闲物理内存
0x88000000 ────   PHYSTOP = KERNBASE + 128MB
```

**关键常量** ([memlayout.h](../kernel/memlayout.h)):

| 宏定义 | 值 | 说明 |
|--------|-----|------|
| `KERNBASE` | `0x80000000` | 内核起始物理地址 |
| `PHYSTOP` | `KERNBASE + 128MB` | 物理内存结束 (128MB) |
| `UART0` | `0x10000000` | UART MMIO 基地址 |
| `VIRTIO0` | `0x10001000` | virtio 磁盘 MMIO 基地址 |
| `PLIC` | `0x0C000000` | PLIC 中断控制器基地址 |

### 3.2 虚拟地址空间布局

```
用户虚拟地址空间 (每个进程独立)       内核虚拟地址空间 (所有 CPU 共享)
─────────────────────────────────    ─────────────────────────────────
MAXVA ────                          MAXVA ────
  TRAMPOLINE (1 page)                 TRAMPOLINE (跳板代码)
  TRAPFRAME  (1 page)                 │
  ...                                │
  │ 堆 (可增长)                       │ 内核栈 (每个进程)
  │ 栈 (固定大小, 带保护页)            │   KSTACK(0)
  │ 数据段                            │   KSTACK(1)  ...
  │ 代码段                            │
0 ────                               │ 内核数据/BSS
                                     │ 内核代码 (.text)
                                     │ UART, PLIC, VIRTIO (直接映射)
                                    0 ────

TRAMPOLINE = MAXVA - PGSIZE          # 同一物理页映射到用户和内核空间
TRAPFRAME  = TRAMPOLINE - PGSIZE     # 用户空间的 trapframe
KSTACK(p)  = TRAMPOLINE - (p+1)*2*PGSIZE  # 内核栈, 周围有保护页
```

**设计要点**:
- `TRAMPOLINE` 页在用户和内核空间都映射到相同虚拟地址 — 这是陷阱处理的关键
- 每个进程的内核栈 (`KSTACK`) 被两页保护页包围, 防止栈溢出
- Sv39 三级页表支持 39 位虚拟地址 (512GB)

---

## 4. 启动流程

### 4.1 启动序列图

```
QEMU 加载内核到 0x80000000
  │
  ▼
[entry.S] _entry
  │ 设置每个 CPU 的栈 (指向 stack0 + CPU_ID * 4096)
  │ 调用 start()
  ▼
[start.c] start()
  │ M 模式 (机器模式):
  │   - 配置 mstatus (禁止 M 模式中断, 设置 mstatus.MPP = S 模式)
  │   - 设置 mepc = main (mret 将跳转到 main)
  │   - 禁用 MMU (satp = 0)
  │   - 设置 mtvec = timervec (M 模式定时器中断)
  │   - 使能定时器中断
  │   - mret → 切换到 S 模式, 跳转到 main()
  ▼
[main.c] main()
  │ CPU 0 (引导核心):
  │   - consoleinit()        # 初始化控制台
  │   - printfinit()         # 初始化 printf 锁
  │   - kinit()              # 初始化物理页分配器
  │   - kvminit()            # 创建内核页表
  │   - kvminithart()        # 启用分页 (satp → 内核页表)
  │   - procinit()           # 初始化进程表
  │   - trapinit()           # 初始化时间锁
  │   - trapinithart()       # 设置 stvec = kernelvec
  │   - plicinit()           # 初始化 PLIC
  │   - plicinithart()       # 使能外部中断
  │   - binit()              # 初始化块缓存
  │   - iinit()              # 初始化 inode 表
  │   - fileinit()           # 初始化文件表
  │   - virtio_disk_init()   # 初始化磁盘
  │   - userinit()           # 创建第一个用户进程
  │   - started = 1          # 通知其他 CPU
  │
  │ 其他 CPU:
  │   - 等待 started == 1
  │   - kvminithart()        # 启用分页
  │   - trapinithart()       # 设置陷阱向量
  │   - plicinithart()       # 使能外部中断
  ▼
[scheduler()]  # 所有 CPU 进入调度循环
```

### 4.2 关键启动文件

**entry.S** — [kernel/entry.S](../kernel/entry.S)
```asm
_entry:
    la sp, stack0          # 从 start.c 获取 stack0
    li a0, 1024*4          # 每个 CPU 栈大小 = 4096 字节
    csrr a1, mhartid       # 读取当前 hart ID
    addi a1, a1, 1
    mul a0, a0, a1         # 偏移 = hartid * 4096
    add sp, sp, a0         # sp = stack0 + (hartid+1) * 4096
    call start              # 跳转到 start()
```

**start.c** — [kernel/start.c](../kernel/start.c)
- 在 M 模式 (Machine mode) 下运行
- 配置 `mstatus` 寄存器, 设置 `MPP = Supervisor mode`
- 通过 `mret` 切换到 S 模式并跳转到 `main()`
- 同时设置 M 模式的定时器中断向量 `timervec`

**kernel.ld** — [kernel/kernel.ld](../kernel/kernel.ld)
```ld
OUTPUT_ARCH("riscv")
ENTRY(_entry)
SECTIONS {
    . = 0x80000000;       # 内核加载地址
    .text : {
        kernel/entry.o(_entry)  # entry.S 必须放在最前面
        *(.text .text.*)
        . = ALIGN(0x1000);
        _trampoline = .;        # trampoline 页标记
        *(trampsec)             # trampoline.S 代码
        ...
    }
}
```

---

## 5. 进程管理

### 5.1 进程状态

```
          allocproc()
  UNUSED ──────────→ USED/RUNNABLE
                         │
                    scheduler()
                    swtch()
                         │
                      RUNNING ────→ SLEEPING ────→ RUNNABLE
                         │            sleep()        wakeup()
                         │
                         │ yield()
                         │
                      RUNNABLE
                         │
                         │ kexit()
                         │
                      ZOMBIE ────→ UNUSED
                                 kwait()/freeproc()
```

**6 种进程状态** (定义于 [proc.h](../kernel/proc.h)):
```c
enum procstate { UNUSED, USED, SLEEPING, RUNNABLE, RUNNING, ZOMBIE };
```

### 5.2 核心数据结构

**struct proc** — 进程控制块, 位于 [proc.h:85-107](../kernel/proc.h):
```c
struct proc {
    struct spinlock lock;         // 进程锁
    enum procstate state;         // 进程状态
    void *chan;                   // 睡眠通道
    int killed;                   // 是否被杀死
    int xstate;                   // 退出状态
    int pid;                      // 进程 ID
    struct proc *parent;          // 父进程
    uint64 kstack;                // 内核栈虚拟地址
    uint64 sz;                    // 用户内存大小
    pagetable_t pagetable;        // 用户页表
    struct trapframe *trapframe;  // 陷阱帧
    struct context context;       // 上下文 (保存的寄存器)
    struct file *ofile[NOFILE];   // 打开的文件 (最多 16 个)
    struct inode *cwd;            // 当前目录
    char name[16];                // 进程名
};
```

**struct cpu** — 每 CPU 状态, 位于 [proc.h:22-27](../kernel/proc.h):
```c
struct cpu {
    struct proc *proc;            // 当前运行的进程
    struct context context;       // 调度器上下文
    int noff;                     // push_off 嵌套深度
    int intena;                   // push_off 前的中断使能状态
};
```

**struct context** — 上下文切换的寄存器快照, 位于 [proc.h:2-19](../kernel/proc.h):
```c
struct context {
    uint64 ra;    // 返回地址
    uint64 sp;    // 栈指针
    uint64 s0..s11;  // callee-saved 寄存器
};
```

### 5.3 进程生命周期函数

| 函数 | 文件行 | 描述 |
|------|--------|------|
| `procinit()` | [proc.c:48](../kernel/proc.c) | 初始化进程表, 分配锁和内核栈地址 |
| `allocproc()` | [proc.c:109](../kernel/proc.c) | 分配未使用的进程槽, 设置 trapframe/页表/上下文 |
| `freeproc()` | [proc.c:155](../kernel/proc.c) | 释放进程资源 (trapframe, 页表, ...) |
| `userinit()` | [proc.c:219](../kernel/proc.c) | 创建第一个用户进程 (`/init`) |
| `kfork()` | [proc.c:259](../kernel/proc.c) | 复制当前进程 (fork 的内核实现) |
| `kexit()` | [proc.c:326](../kernel/proc.c) | 进程退出: 关闭文件, 回收子进程, 唤醒父进程 |
| `kwait()` | [proc.c:370](../kernel/proc.c) | 等待子进程退出 (wait 的内核实现) |
| `growproc()` | [proc.c:236](../kernel/proc.c) | 增长/收缩进程内存 (sbrk 的内核实现) |
| `kkill()` | [proc.c:592](../kernel/proc.c) | 杀死进程 (设置 killed 标志) |
| `scheduler()` | [proc.c:424](../kernel/proc.c) | CPU 调度器: 无限循环选择 RUNNABLE 进程执行 |
| `sched()` | [proc.c:472](../kernel/proc.c) | 进程主动放弃 CPU (调用 swtch 到调度器) |
| `yield()` | [proc.c:493](../kernel/proc.c) | 时间片用尽, 让出 CPU |
| `sleep()` | [proc.c:542](../kernel/proc.c) | 进程睡眠在通道上 (释放锁, 等待唤醒) |
| `wakeup()` | [proc.c:573](../kernel/proc.c) | 唤醒所有在通道上睡眠的进程 |

### 5.4 fork 实现流程

```
kfork()                                    # proc.c:259
  │
  ├─ allocproc()                            # 分配新进程结构
  │   ├─ 在 proc[] 中找 UNUSED 槽
  │   ├─ 分配 pid
  │   ├─ 分配 trapframe 页
  │   ├─ proc_pagetable(p)                  # 创建用户页表 (含 trampoline + trapframe)
  │   └─ 设置 context.ra = forkret          # 新进程首次调度时执行 forkret
  │
  ├─ uvmcopy(old, new, sz)                  # 复制父进程用户内存
  ├─ np->sz = p->sz
  ├─ *(np->trapframe) = *(p->trapframe)     # 复制陷阱帧 (保存的寄存器)
  ├─ np->trapframe->a0 = 0                  # 子进程返回 0
  ├─ 复制文件描述符 (filedup)
  ├─ 复制当前目录 (idup)
  ├─ 复制进程名
  └─ np->state = RUNNABLE                   # 子进程就绪
```

### 5.5 sleep / wakeup 机制

```
sleep(chan, lk):                wakeup(chan):
  │                               │
  acquire(&p->lock)               for each proc:
  release(lk)                       acquire(&p->lock)
  p->chan = chan                    if state == SLEEPING && chan == chan:
  p->state = SLEEPING                 state = RUNNABLE
  sched()  → 调度其他进程           release(&p->lock)
  // 被唤醒后:
  p->chan = 0
  release(&p->lock)
  acquire(lk)  // 重新获取条件锁
```

**关键设计**: sleep 在释放 `lk` 之前获取 `p->lock`, 保证了不会丢失 wakeup — 这正是 xv6 中 sleep/wakeup 避免 **lost wakeup** 的核心机制。

---

## 6. 虚拟内存管理

### 6.1 Sv39 页表结构

RISC-V Sv39 使用三级页表, 每级 512 个条目 (9 bits), 页大小 4KB:

```
63              39 38        30 29        21 20       12 11          0
┌──────────────────┬───────────┬───────────┬───────────┬─────────────┐
│  必须是 0 或 1    │  L2 索引   │  L1 索引   │  L0 索引  │    页内偏移   │
│    (25 bits)     │  (9 bits) │  (9 bits) │  (9 bits) │  (12 bits)  │
└──────────────────┴───────────┴───────────┴───────────┴─────────────┘
                        │            │           │           │
                        ▼            ▼           ▼           ▼
                      VPN[2]       VPN[1]     VPN[0]      offset
                      L2 页表      L1 页表     L0 页表     物理页内偏移
```

**PTE 格式** (8 字节):
```
63          54 53    28 27      19 18      10 9 8   7 6 5 4 3 2 1 0
┌─────────────┬────────┬──────────┬──────────┬───┬───┬───┬───┬───┬─┐
│  保留       │ PPN[2] │  PPN[1]  │  PPN[0]  │RSW│ D │ A │ G │ U │X│W│R│V│
└─────────────┴────────┴──────────┴──────────┴───┴───┴───┴───┴───┴─┘
                                                                    │
  V: Valid, R: Read, W: Write, X: Execute, U: User accessible       │
  G: Global, A: Accessed, D: Dirty, RSW: Reserved for supervisor      │
```

### 6.2 核心 VM 函数

| 函数 | 文件行 | 描述 |
|------|--------|------|
| `kvminit()` | [vm.c:65](../kernel/vm.c) | 创建内核页表 (`kernel_pagetable`) |
| `kvmmake()` | [vm.c:22](../kernel/vm.c) | 构建内核直接映射页表 |
| `kvmmap()` | [vm.c:57](../kernel/vm.c) | 添加内核页表映射 (直接映射 pa=va) |
| `kvminithart()` | [vm.c:73](../kernel/vm.c) | 每个 CPU 启用内核页表 |
| `walk()` | [vm.c:97](../kernel/vm.c) | **三级页表遍历**: 给定 VA, 返回 PTE 地址; alloc=1 时按需创建页表页 |
| `walkaddr()` | [vm.c:120](../kernel/vm.c) | 查找 VA 对应的物理地址 |
| `mappages()` | [vm.c:145](../kernel/vm.c) | 映射 VA 范围到 PA 范围 |
| `uvmcreate()` | [vm.c:178](../kernel/vm.c) | 创建空用户页表 |
| `uvmalloc()` | [vm.c:216](../kernel/vm.c) | 增长用户内存 (分配+映射物理页) |
| `uvmdealloc()` | [vm.c:246](../kernel/vm.c) | 收缩用户内存 (取消映射+释放物理页) |
| `uvmcopy()` | [vm.c:296](../kernel/vm.c) | 复制用户页表 (fork 使用): 逐页复制物理内存 |
| `uvmfree()` | [vm.c:282](../kernel/vm.c) | 释放用户页表及其物理内存 |
| `uvmunmap()` | [vm.c:192](../kernel/vm.c) | 取消映射 VA 范围的页 |
| `copyout()` | [vm.c:342](../kernel/vm.c) | 内核→用户空间拷贝 |
| `copyin()` | [vm.c:380](../kernel/vm.c) | 用户→内核空间拷贝 |
| `copyinstr()` | [vm.c:409](../kernel/vm.c) | 从用户空间拷贝 null 结尾字符串 |
| `vmfault()` | [vm.c:452](../kernel/vm.c) | **惰性分配**: 按需分配用户内存页 |
| `ismapped()` | [vm.c:475](../kernel/vm.c) | 检查虚拟地址是否已映射 |

### 6.3 惰性内存分配

xv6 实现了 **惰性页分配** (lazy page allocation) 优化:

```
sys_sbrk(n) → growproc(n)
  │
  └─ 仅增加 p->sz, 不立即分配物理内存

当进程访问未分配页时:
  硬件触发 Page Fault (scause = 13 或 15)
    │
    ▼
  usertrap() [trap.c]
    └─ vmfault() [vm.c:452]
        ├─ 检查 va < p->sz (在合法范围内)
        ├─ 检查页未被映射
        ├─ kalloc() 分配物理页, 清零
        └─ mappages() 映射到用户页表
```

### 6.4 内核物理页分配器

[kalloc.c](../kernel/kalloc.c) 实现简单的 **空闲链表** 分配器:

```c
struct run {
    struct run *next;
};
struct run *freelist;   // 空闲页链表头

// kinit(): 初始化, 将 [end, PHYSTOP) 的所有页加入 freelist
// kalloc(): 从 freelist 取一页 (4096 字节), 清零后返回
// kfree():   将页还回 freelist, 写入毒化数据 0x01
```

---

## 7. 陷阱与中断处理

### 7.1 陷阱处理架构

xv6 区分两种来源的陷阱:

| 来源 | 陷阱向量 | 处理函数 | 入口汇编 |
|------|----------|----------|----------|
| 用户模式 → S 模式 | `uservec` (TRAMPOLINE) | `usertrap()` | [trampoline.S](../kernel/trampoline.S) |
| 内核模式 → S 模式 | `kernelvec` | `kerneltrap()` | [kernelvec.S](../kernel/kernelvec.S) |
| M 模式定时器 | `timervec` (start.c) | 软件中断下沉到 S 模式 | [start.c](../kernel/start.c) |

### 7.2 用户陷阱处理流程

```
用户进程执行 → ecall (系统调用) 或 中断/异常
  │
  │ 硬件: 切换到 S 模式, PC → stvec (= TRAMPOLINE + uservec)
  ▼
[trampoline.S] uservec:
  ├─ sscratch ← a0                     # 暂存 a0
  ├─ a0 ← TRAPFRAME                    # 获取 trapframe 地址
  ├─ 保存 32 个通用寄存器到 trapframe
  ├─ 恢复 a0 (从 sscratch)
  ├─ sp ← trapframe->kernel_sp         # 切换到内核栈
  ├─ tp ← trapframe->kernel_hartid     # 设置 hart ID
  ├─ t0 ← trapframe->kernel_trap       # = usertrap
  ├─ t1 ← trapframe->kernel_satp       # = 内核页表
  ├─ sfence.vma; csrw satp, t1         # 切换到内核页表
  └─ jalr t0                           # → usertrap()
  
[trap.c] usertrap():
  ├─ 检查 SPP = 用户模式
  ├─ stvec ← kernelvec                 # 在内核中的陷阱使用 kernelvec
  ├─ sepc → trapframe->epc             # 保存用户 PC
  │
  ├─ 分支处理:
  │   ├─ scause == 8 (ecall):
  │   │   ├─ epc += 4                 # 返回地址 = 下一条指令
  │   │   ├─ intr_on()                # 使能中断 (syscall 可能阻塞)
  │   │   └─ syscall()               # 分发系统调用
  │   │
  │   ├─ devintr() != 0:              # 设备中断
  │   │   └─ (处理 UART, virtio, 定时器)
  │   │
  │   └─ scause == 13/15 (page fault):
  │       └─ vmfault()               # 惰性页分配
  │
  ├─ if killed: kexit(-1)
  ├─ if timer interrupt (which_dev==2): yield()
  │
  └─ prepare_return()
      ├─ intr_off()
      ├─ stvec ← TRAMPOLINE + uservec  # 设置返回到用户态的陷阱向量
      ├─ 填充 trapframe 中的内核信息 (satp, sp, usertrap, hartid)
      ├─ sstatus: SPP=User, SPIE=1    # sret 后回到用户模式
      └─ sepc ← trapframe->epc

usertrap() 返回 satp 给 trampoline.S
  ▼
[trampoline.S] userret:
  ├─ csrw satp, a0                     # 切换到用户页表
  ├─ 从 TRAPFRAME 恢复 32 个寄存器
  └─ sret                             # 返回用户模式
```

### 7.3 内核陷阱处理

```
内核执行中 → 中断/异常
  │
  ▼
[kernelvec.S] kernelvec:
  ├─ sp -= 256                         # 在内核栈上预留空间
  ├─ 保存 caller-saved 寄存器
  ├─ call kerneltrap()                 # 调用 C 处理函数
  ├─ 恢复寄存器
  ├─ sp += 256
  └─ sret                             # 返回

[trap.c] kerneltrap():
  ├─ 检查 SPP = S 模式 (必须在内核中)
  ├─ 检查中断已禁用
  ├─ devintr() 处理设备中断
  │   ├─ scause = 0x8000000000000009 (external interrupt): PLIC 中断
  │   └─ scause = 0x8000000000000005 (timer interrupt): 定时器中断
  └─ 如果是定时器中断且 myproc() ≠ 0: yield()
```

### 7.4 设备中断分发

```c
devintr() [trap.c:185]:
  scause == 0x8000000000000009 (SEI):    # 外部中断 (PLIC)
    irq = plic_claim()
    switch(irq):
      UART0_IRQ (10): uartintr()
      VIRTIO0_IRQ (1): virtio_disk_intr()
    plic_complete(irq)
    return 1

  scause == 0x8000000000000005 (STI):    # 定时器中断
    clockintr()
    return 2
```

---

## 8. 上下文切换

### 8.1 swtch 汇编

[swtch.S](../kernel/swtch.S) 是上下文切换的核心:

```asm
# void swtch(struct context *old, struct context *new);
swtch:
    sd ra, 0(a0)        # 保存 callee-saved 寄存器到 old
    sd sp, 8(a0)
    sd s0..s11, 16..104(a0)
    
    ld ra, 0(a1)        # 从 new 恢复 callee-saved 寄存器
    ld sp, 8(a1)
    ld s0..s11, 16..104(a1)
    
    ret                 # 返回 (此时 ra 指向新上下文的返回地址)
```

### 8.2 两种上下文切换场景

**场景 1: 进程 → 调度器 → 进程**
```
进程 A (用户态) → trap → usertrap() → yield()/sleep()/kexit()
  → sched()
    → swtch(&p->context, &c->context)   # 保存进程 A 上下文, 恢复调度器上下文
    → 返回到 scheduler() 循环

  → scheduler() 选择进程 B
    → swtch(&c->context, &p->context)   # 保存调度器上下文, 恢复进程 B 上下文
    → 返回到进程 B 的 sched() 中
```

**场景 2: forkret 首次调度**
```
scheduler() 选择新 fork 的子进程
  → swtch(&c->context, &child->context)
    → child->context.ra = forkret, child->context.sp = 子进程内核栈顶
    → ret → forkret()
      → (首次调用) fsinit(), kexec("/init", ...)
      → prepare_return() + userret → 进入用户空间
```

---

## 9. 系统调用

### 9.1 系统调用表

xv6 支持 **21 个系统调用** (定义于 [syscall.h](../kernel/syscall.h)):

| 编号 | 名称 | 实现 | 描述 |
|------|------|------|------|
| 1 | `SYS_fork` | [sysproc.c](../kernel/sysproc.c) | 创建子进程 |
| 2 | `SYS_exit` | sysproc.c | 退出进程 |
| 3 | `SYS_wait` | sysproc.c | 等待子进程 |
| 4 | `SYS_pipe` | sysfile.c | 创建管道 |
| 5 | `SYS_read` | sysfile.c | 读文件 |
| 6 | `SYS_kill` | sysproc.c | 杀死进程 |
| 7 | `SYS_exec` | sysfile.c (调用 kexec) | 执行程序 |
| 8 | `SYS_fstat` | sysfile.c | 获取文件状态 |
| 9 | `SYS_chdir` | sysfile.c | 切换目录 |
| 10 | `SYS_dup` | sysfile.c | 复制文件描述符 |
| 11 | `SYS_getpid` | sysproc.c | 获取进程 ID |
| 12 | `SYS_sbrk` | sysproc.c | 增长/收缩内存 |
| 13 | `SYS_pause` | sysproc.c | 暂停进程 (弃用) |
| 14 | `SYS_uptime` | sysproc.c | 获取系统运行时间 |
| 15 | `SYS_open` | sysfile.c | 打开文件 |
| 16 | `SYS_write` | sysfile.c | 写文件 |
| 17 | `SYS_mknod` | sysfile.c | 创建设备节点 |
| 18 | `SYS_unlink` | sysfile.c | 删除文件 |
| 19 | `SYS_link` | sysfile.c | 创建硬链接 |
| 20 | `SYS_mkdir` | sysfile.c | 创建目录 |
| 21 | `SYS_close` | sysfile.c | 关闭文件 |

### 9.2 系统调用分发流程

```
用户进程调用 write(fd, buf, n)
  │
  │ user/usys.pl 生成 usys.S:
  │   write: li a7, SYS_write; ecall; ret
  ▼
[trampoline.S] uservec
  │
  ▼
[trap.c] usertrap() → syscall()
                        │
                        ▼
[syscall.c:132] syscall():
  num = p->trapframe->a7;          # 系统调用号
  if (num > 0 && num < NELEM(syscalls) && syscalls[num])
    p->trapframe->a0 = syscalls[num]();  # 调用对应的 sys_* 函数
  // 返回值写入 a0 → fork 返回子进程 PID, write 返回字节数 ...
```

### 9.3 系统调用参数获取

系统调用参数通过 RISC-V 寄存器传递 (`a0`..`a5`), 内核从当前进程的 trapframe 中提取:

```c
argraw(n) [syscall.c:34]:           # 原始 64 位参数
  switch(n):
    case 0: return p->trapframe->a0;
    case 1: return p->trapframe->a1;
    ...            (最多 6 个参数)

argint(n, ip)  [syscall.c:57]:     # int 参数
argaddr(n, ip) [syscall.c:66]:     # 地址/指针参数 (uint64)
argstr(n, buf, max) [syscall.c:75]: # 从用户空间读取字符串
```

### 9.4 用户态系统调用入口

[usys.pl](../user/usys.pl) 是 Perl 脚本, 为每个系统调用生成汇编桩:

```perl
# 输入: "write" → 输出:
.globl write
write:
    li a7, SYS_write     # 加载系统调用号到 a7
    ecall                # 触发陷阱进入内核
    ret                  # 返回用户态
```

---

## 10. 文件系统

### 10.1 磁盘布局

xv6 文件系统是类 Unix V6 的简单实现, 磁盘布局如下:

```
┌──────┬───────────┬──────┬──────────────┬──────────────┬──────────────┐
│ Boot │ Superblock│ Log  │ Inode blocks │ Free bitmap  │ Data blocks  │
│block │    (1)    │      │              │              │              │
└──────┴───────────┴──────┴──────────────┴──────────────┴──────────────┘
```

**超级块** ([fs.h:14-23](../kernel/fs.h)):
```c
struct superblock {
    uint magic;        // 魔数: 0x10203040
    uint size;         // 文件系统总块数
    uint nblocks;      // 数据块数
    uint ninodes;      // inode 总数
    uint nlog;         // 日志块数
    uint logstart;     // 日志起始块
    uint inodestart;   // inode 起始块
    uint bmapstart;    // 空闲位图起始块
};
```

**磁盘 inode** ([fs.h:32-39](../kernel/fs.h)):
```c
struct dinode {
    short type;              // 文件类型 (T_FILE, T_DIR, T_DEVICE)
    short major, minor;      // 设备号
    short nlink;             // 硬链接数
    uint size;               // 文件大小 (字节)
    uint addrs[NDIRECT+2];   // 12 直接块 + 1 一级间接块 + 1 二级间接块
};
// 最大文件大小 = (12 + 256 + 256*256) * 1024 ≈ 64MB
```

### 10.2 文件系统分层架构

```
┌─────────────────┐
│  Path Names     │  namei(), nameiparent(), dirlookup(), dirlink()
├─────────────────┤
│  Directories    │  skipelem(), namecmp()
├─────────────────┤
│  Inodes         │  ialloc(), iget(), ilock(), iunlock(), iput()
│                 │  readi(), writei(), stati(), itrunc()
├─────────────────┤
│  Logging        │  initlog(), begin_op(), end_op(), log_write()
├─────────────────┤
│  Block Cache    │  bread(), bwrite(), brelse(), bpin(), bunpin()
├─────────────────┤
│  Disk (virtio)  │  virtio_disk_rw(), virtio_disk_intr()
└─────────────────┘
```

### 10.3 核心文件系统函数

| 函数 | 文件行 | 描述 |
|------|--------|------|
| `fsinit()` | [fs.c:42](../kernel/fs.c) | 读取超级块, 初始化日志, 回收孤儿 inode |
| `ialloc()` | [fs.c:199](../kernel/fs.c) | 分配磁盘 inode |
| `iget()` | [fs.c:247](../kernel/fs.c) | 获取或创建内存 inode 缓存条目 |
| `ilock()` | [fs.c:293](../kernel/fs.c) | 锁定 inode, 必要时从磁盘读取 |
| `iput()` | [fs.c:337](../kernel/fs.c) | 减少引用计数, 无链接时释放 inode |
| `readi()` | [fs.c:494](../kernel/fs.c) | 从 inode 读取数据 |
| `writei()` | [fs.c:528](../kernel/fs.c) | 向 inode 写入数据 |
| `bmap()` | [fs.c:405](../kernel/fs.c) | 将文件内块号映射到磁盘块号 (支持间接块) |
| `itrunc()` | [fs.c:448](../kernel/fs.c) | 截断文件, 释放所有数据块 |
| `dirlookup()` | [fs.c:574](../kernel/fs.c) | 在目录中按名称查找目录项 |
| `dirlink()` | [fs.c:602](../kernel/fs.c) | 在目录中创建新目录项 |
| `namei()` | [fs.c:709](../kernel/fs.c) | 路径名 → inode 解析 |
| `nameiparent()` | [fs.c:716](../kernel/fs.c) | 解析父目录路径 (用于创建/删除文件) |
| `skipelem()` | [fs.c:645](../kernel/fs.c) | 从路径中提取下一个元素名 |

### 10.4 块缓存层 (Buffer Cache)

[bio.c](../kernel/bio.c) 实现 LRU (最近最少使用) 块缓存, 使用 **双向链表 + 睡眠锁**。

```c
struct buf {
    int valid;     // 数据是否已从磁盘读取
    int disk;      // 是否正在进行磁盘操作
    uint dev;      // 设备号
    uint blockno;  // 块号
    struct sleeplock lock;  // 每缓冲区锁
    uint refcnt;   // 引用计数
    struct buf *prev, *next;  // LRU 链表
    uchar data[BSIZE];  // 块数据 (1024 字节)
};
```

**核心函数**:
- `binit()` — 初始化缓冲区链表 (所有缓冲区形成循环双向链表)
- `bread(dev, blockno)` — 获取块 (缓存命中返回, 未命中则从磁盘读取)
- `bwrite(bp)` — 将缓冲区写回磁盘
- `brelse(bp)` — 释放缓冲区引用, 移到链表头部 (LRU)

### 10.5 日志层

[log.c](../kernel/log.c) 实现 **write-ahead logging** (预写日志) 以提供崩溃一致性:

```
begin_op()          # 开始文件系统操作 (可能阻塞直到日志有空间)
  ...
  log_write(bp)     # 将块修改记录到日志
  ...
end_op()            # 提交操作: 如果这是最后的活动操作, 执行提交
  commit():
    1. write_log()      # 将所有脏块写入日志区
    2. write_head()     # 写日志头 (commit 记录)
    3. install_trans()  # 将日志块复制到实际磁盘位置
    4. write_head()     # 写日志头 (清空日志)
```

**关键函数**:
- `initlog(dev, sb)` — 初始化日志, 发现未完成事务时执行恢复
- `begin_op()` — 开始操作, 必要时等待日志空间
- `end_op()` — 结束操作, 当所有并发操作完成时提交
- `log_write(bp)` — 标记缓冲区为脏, 将在提交时写入日志
- `recover_from_log()` — 崩溃恢复: 重放已提交的日志事务

### 10.6 文件描述符管理

[file.c](../kernel/file.c) 管理三种文件类型:
- **FD_INODE**: 普通文件和目录
- **FD_PIPE**: 管道
- **FD_DEVICE**: 设备文件 (如控制台)

```c
struct file {
    enum { FD_NONE, FD_PIPE, FD_INODE, FD_DEVICE } type;
    int ref;              // 引用计数
    char readable;
    char writable;
    struct pipe *pipe;    // FD_PIPE
    struct inode *ip;     // FD_INODE / FD_DEVICE
    uint off;             // FD_INODE 的读写偏移
    short major;          // FD_DEVICE 的主设备号
};
```

**核心函数**:
- `filealloc()` — 从全局文件表分配文件结构
- `fileread()` / `filewrite()` — 根据类型分发读写操作
- `filedup()` — 复制文件引用 (用于 fork / dup)
- `fileclose()` — 减少引用, 引用为 0 时释放

### 10.7 管道

[pipe.c](../kernel/pipe.c) 实现单向管道通信:

```c
struct pipe {
    struct spinlock lock;
    char data[PIPESIZE];  // 512 字节环形缓冲区
    uint nread;           // 已读取字节数
    uint nwrite;          // 已写入字节数
    int readopen;         // 读端是否打开
    int writeopen;        // 写端是否打开
};
```

- `pipealloc()` — 创建管道, 返回两个文件描述符 (读端 + 写端)
- `pipewrite()` — 写入管道, 满时阻塞等待
- `piperead()` — 从管道读取, 空时阻塞等待
- `pipeclose()` — 关闭管道一端, 唤醒等待者

---

## 11. 锁与同步机制

### 11.1 自旋锁 (Spinlock)

[spinlock.c](../kernel/spinlock.c) 实现互斥自旋锁:

```c
struct spinlock {
    uint locked;       // 是否被锁定
    char *name;        // 调试用名称
    struct cpu *cpu;   // 持有锁的 CPU
};
```

**关键特性**:
- `acquire()`: 禁用中断 (`push_off`), 使用 `__sync_lock_test_and_set` 原子操作自旋等待, 记录持有 CPU
- `release()`: 清除 CPU 信息, 内存屏障 (`__sync_synchronize`), 使用 `__sync_lock_release` 原子释放, 恢复中断
- `push_off()` / `pop_off()`: 嵌套中断管理 — 记录初始中断状态, 支持嵌套调用
- `holding()`: 检查当前 CPU 是否持有锁 (用于断言)

### 11.2 睡眠锁 (Sleeplock)

[sleeplock.c](../kernel/sleeplock.c) 实现可睡眠的互斥锁:

```c
struct sleeplock {
    uint locked;       // 是否被锁定
    struct spinlock lk; // 保护 locked 字段的自旋锁
    char *name;
    int pid;           // 持有锁的进程
};
```

- 使用 `sleep()` 和 `wakeup()` 实现阻塞等待
- 适用于可能长时间持有的锁 (如 inode 锁)
- 自旋锁保护其内部状态, 睡眠锁保护外部资源
- 正确的锁顺序: **自旋锁不能嵌套在睡眠锁内部** — `acquiresleep` 内部使用自旋锁仅短暂持有

### 11.3 锁的层次结构

```
wait_lock          (保护 wait/parent 关系)
  ├─ 进程锁         (p->lock)
  │   ├─ 睡眠锁     (inode->lock, buf->lock)
  │   └─ 其他自旋锁 (tickslock, itable.lock, pid_lock)
  └─ 文件系统日志锁 (log.lock)
```

**死锁避免原则**:
1. 自旋锁必须禁止中断
2. 持有自旋锁期间不能睡眠
3. 先获取 `wait_lock`, 再获取进程锁
4. 先获取进程锁, 再获取睡眠锁

---

## 12. 设备驱动

### 12.1 UART 串口

[uart.c](../kernel/uart.c) 实现 16550 兼容串口驱动:

**MMIO 寄存器** (基地址 `UART0 = 0x10000000`):
- `RHR` (0x00): 接收保持寄存器 (读)
- `THR` (0x00): 发送保持寄存器 (写)
- `LSR` (0x14): 线路状态寄存器

**核心函数**:
- `uartinit()` — 初始化 UART (设置波特率、数据位等)
- `uartputc(c)` — 发送字符 (内核直接输出)
- `uartputc_sync(c)` — 同步发送字符 (轮询等待)
- `uartgetc()` — 接收字符 (中断驱动)
- `uartintr()` — UART 中断处理: 读取所有等待的字节, 传递给控制台
- `uartwrite(buf, n)` — 输出缓冲区

### 12.2 控制台

[console.c](../kernel/console.c) 实现基于 UART 的终端:

- 管理输入缓冲区 (支持退格、行编辑)
- `consoleinit()` — 初始化控制台锁
- `consputc(c)` — 输出字符 (处理特殊字符如 `\n` → `\r\n`)
- `consoleintr(c)` — 处理输入字符 (支持 `^P` 调试, `^D` EOF, `^U` 清除行, backspace)

### 12.3 PLIC 中断控制器

[plic.c](../kernel/plic.c) 管理平台级中断控制器:

```
PLIC 中断处理模型:
  每个 CPU (hart):
    ┌──────────────────────┐
    │ M-mode external int  │ ← mie.MEIE
    │ S-mode external int  │ ← mie.SEIE (xv6 使用)
    └──────────────────────┘
            │
    PLIC 分发中断到 CPU:
      - 设置中断优先级
      - 使能每个 hart 的中断源
      - 设置每个 hart 的优先级阈值
      - claim: 读取当前 hart 待处理的最大优先级中断
      - complete: 标记中断处理完成
```

**核心函数**:
- `plicinit()` — 设置所有中断源的优先级
- `plicinthart()` — 为当前 CPU 使能 S 模式外部中断
- `plic_claim()` — 获取当前待处理中断 ID
- `plic_complete(irq)` — 通知 PLIC 中断处理完成

### 12.4 virtio 磁盘

[virtio_disk.c](../kernel/virtio_disk.c) 实现 virtio 块设备驱动。

**VirtIO MMIO 寄存器** (基地址 `VIRTIO0 = 0x10001000`):

VirtIO 使用 **virtqueue** (虚拟队列) 进行 I/O:
1. 描述符表 (descriptor table): 描述数据缓冲区
2. 可用环 (available ring): 驱动程序提交请求
3. 已用环 (used ring): 设备完成请求后通知

**核心函数**:
- `virtio_disk_init()` — 初始化 virtio 磁盘:
  - 复位设备, 协商特性
  - 设置 virtqueue (描述符表、可用环、已用环)
  - 配置设备, 设置状态为 DRIVER_OK
- `virtio_disk_rw(buf, write)` — 读写磁盘扇区 (异步):
  - 构造 virtqueue 描述符链
  - 更新可用环
  - 通知设备 (写入 QueueNotify 寄存器)
- `virtio_disk_intr()` — 中断处理:
  - 从已用环获取完成的请求
  - 唤醒等待的进程

---

## 13. 用户程序

### 13.1 用户库

[user/ulib.c](../user/ulib.c) 提供用户态基础库函数:
- `strcpy()`, `strcmp()`, `strlen()`
- `memset()`, `memcpy()`
- `gets()`, `stat()`
- `atoi()`, `fprintf()`, `fputs()`

[user/umalloc.c](../user/umalloc.c) 实现用户态内存分配器 (类似 malloc):
- `malloc()` — 从当前进程 brk 获取内存
- `free()` — 释放内存 (当前实现为 no-op)

[user/printf.c](../user/printf.c) 用户态格式化输出:
- `printf()` — 格式化打印 (使用 `write()` 系统调用)

### 13.2 系统调用入口生成

[user/usys.pl](../user/usys.pl): Perl 脚本, 自动为每个系统调用生成汇编入口:

```perl
# 输入系统调用名称列表
# 输出 usys.S 文件
print "# generated by usys.pl\n";
while(<>) {
    my ($name) = /^{.* SYS_(\w+)/;
    print ".global $name\n$name:\n";
    print " li a7, SYS_$name\n";
    print " ecall\n";
    print " ret\n";
}
```

### 13.3 Shell

[user/sh.c](../user/sh.c) 实现简单的 Unix shell:
- **重定向**: `>`, `<` (仅简单文件, 不支持合并)
- **管道**: `|` (支持多级管道)
- **后台执行**: `&`
- **内建命令**: `cd`
- **支持的执行模式**:
  - 绝对路径: `/bin/ls`
  - 相对路径: `./a.out`
  - PATH 搜索: `ls` (在 `/` 和 `/bin` 中查找)

### 13.4 第一个用户进程

[user/init.c](../user/init.c) 是系统启动的第一个用户进程:

```c
int main(void) {
    int pid, wpid;
    
    if(open("console", O_RDWR) < 0) {  // 打开控制台
        mknod("console", CONSOLE, 0);
        open("console", O_RDWR);
    }
    dup(0);  // stdout
    dup(0);  // stderr
    
    for(;;) {
        printf("init: starting sh\n");
        pid = fork();
        if(pid < 0) { ... }
        if(pid == 0) {                   // 子进程
            exec("sh", argv);
        }
        // 父进程等待 shell 退出, 然后重启
        while((wpid = wait(0)) >= 0 && wpid != pid);
    }
}
```

**init 进程职责**:
1. 打开控制台作为 stdin/stdout/stderr
2. 循环创建 shell 进程
3. 当 shell 退出时重新启动一个新的 shell
4. 回收所有孤儿进程 (僵尸进程最终由 init 清理)

### 13.5 用户程序列表

| 程序 | 文件 | 功能 |
|------|------|------|
| `cat` | [cat.c](../user/cat.c) | 显示文件内容 |
| `echo` | [echo.c](../user/echo.c) | 回显命令行参数 |
| `grep` | [grep.c](../user/grep.c) | 正则表达式搜索 |
| `kill` | [kill.c](../user/kill.c) | 向进程发送信号 |
| `ln` | [ln.c](../user/ln.c) | 创建硬链接 |
| `ls` | [ls.c](../user/ls.c) | 列出目录内容 |
| `mkdir` | [mkdir.c](../user/mkdir.c) | 创建目录 |
| `rm` | [rm.c](../user/rm.c) | 删除文件 |
| `wc` | [wc.c](../user/wc.c) | 统计文件行数/单词数/字符数 |
| `sh` | [sh.c](../user/sh.c) | shell |
| `init` | [init.c](../user/init.c) | 初始化进程 |
| `zombie` | [zombie.c](../user/zombie.c) | 僵尸进程测试 |
| `forktest` | [forktest.c](../user/forktest.c) | fork 压力测试 |
| `usertests` | [user/tests](../user/tests) | 按子系统拆分的用户态回归套件 |
| `grind` | [grind.c](../user/grind.c) | 随机系统调用压力测试 |
| `stressfs` | [stressfs.c](../user/stressfs.c) | 文件系统压力测试 |
| `logstress` | [logstress.c](../user/logstress.c) | 日志系统压力测试 |
| `forphan` | [forphan.c](../user/forphan.c) | fork + 孤儿进程测试 |
| `dorphan` | [dorphan.c](../user/dorphan.c) | 孤儿进程双重检查测试 |

---

## 14. 核心执行流程

### 14.1 系统启动 → 第一个用户进程

```
QEMU 启动
  → entry.S: _entry
    → start.c: start() [M 模式]
      → main.c: main() [S 模式, CPU 0]
        ├─ 初始化: console, kalloc, vm, proc, trap, plic, buf, inode, file, disk
        ├─ userinit():
        │   ├─ allocproc() 创建进程 1
        │   ├─ initproc→cwd = namei("/")
        │   ├─ state = RUNNABLE
        │   └─ 此时进程没有用户内存和代码!
        └─ scheduler():
            → 选择 initproc (进程 1)
              → swtch() → forkret() [proc.c:506]
                ├─ (首次) fsinit(ROOTDEV)  ← 必须在进程上下文中运行!
                ├─ kexec("/init", ...)     ← 加载 /init 程序
                │   ├─ namei("/init")       # 打开可执行文件
                │   ├─ 解析 ELF 头
                │   ├─ uvmalloc()           # 分配用户内存
                │   ├─ loadseg()            # 加载代码段到内存
                │   ├─ 设置用户栈 + 参数 (argc, argv)
                │   ├─ trapframe->epc = elf.entry  # 用户入口点
                │   └─ 切换页表 (丢弃旧的空页表)
                └─ prepare_return() + userret:
                    → sret → 用户空间 /init 程序开始执行!

/init 进程:
  → open("console", O_RDWR)
  → dup(0); dup(0)  # 设置 stdin/stdout/stderr
  → fork()
    → 子进程: exec("sh", ["sh"])
    → 父进程: wait() ... 循环
```

### 14.2 系统调用往返 (Round-Trip)

```
用户态: write(1, "hello", 5)
  │ libc 调用 → usys.S: write:
  │   li a7, SYS_write     # a7 = 16
  │   ecall                # 陷入内核
  ▼
硬件:
  sepc ← PC+4 (ecall 下一条指令)
  scause ← 8 (Environment call from U-mode)
  sstatus.SPP ← U-mode
  sstatus.SPIE ← sstatus.SIE
  sstatus.SIE ← 0
  PC ← stvec (= uservec 地址)
  ▼
[trampoline.S] uservec:
  保存用户寄存器到 TRAPFRAME
  加载内核信息: sp, tp, satp, usertrap 地址
  切换到内核页表
  跳转到 usertrap()
  ▼
[trap.c] usertrap():
  stvec ← kernelvec (内核中的陷阱)
  sepc → trapframe->epc
  scause == 8 → trapframe->epc += 4
  intr_on()
  syscall()
  ▼
[syscall.c] syscall():
  num = trapframe->a7 = 16 (SYS_write)
  trapframe->a0 = sys_write()
  ▼
[sysfile.c] sys_write():
  argint(0, &fd)           # fd = 1
  argaddr(1, &p)           # p = "hello"
  argint(2, &n)            # n = 5
  filewrite(f, p, n)
  ▼
[file.c] filewrite():
  if type == FD_PIPE: pipewrite(f->pipe, addr, n)
  if type == FD_INODE: writei(f->ip, 1, addr, f->off, n)
  if type == FD_DEVICE:
    if f->major == CONSOLE:
      uartwrite(...)
      → consputc() → uartputc_sync() → 输出字符到 QEMU 终端
  ▼
返回用户态:
  usertrap() → prepare_return()
    stvec ← uservec
    填充 trapframe 中的内核信息
    sstatus.SPP ← User
    sepc ← trapframe->epc
    返回 satp 给 trampoline.S
  ▼
[trampoline.S] userret:
  切换到用户页表
  从 TRAPFRAME 恢复用户寄存器
  sret → 用户态, PC = sepc
  ▼
用户态: write() 返回 5
```

### 14.3 进程调度时间线

```
假设 3 个 CPU 核心, 5 个进程:

CPU 0:                    CPU 1:                    CPU 2:
──────────────────────    ──────────────────────    ──────────────────────
scheduler() 循环          scheduler() 循环          scheduler() 循环
  │                         │                         │
  选择 P1 (RUNNABLE)        选择 P2 (RUNNABLE)        选择 P3 (RUNNABLE)
  swtch→P1                  swtch→P2                  swtch→P3
  │                         │                         │
  P1 运行...                P2 运行...                P3 运行...
  │ timer interrupt!        │                         │
  ▼ yield()                 │                         │
    state=RUNNABLE          │                         │
    sched()→swtch→CPU0      │                         │
  │                         │                         │
  选择 P4 (RUNNABLE)        选择 P5 (RUNNABLE)         │
  swtch→P4                  或睡眠的 P2               P3 运行...
  │                         │                         │
  P4 运行...                ...                       │ timer interrupt!
                                                      ▼ yield()
                                                      │
                                                      选择 P1 (RUNNABLE)
                                                      swtch→P1
```

---

## 附录

### A. RISC-V 关键 CSR 寄存器

| CSR | 名称 | 说明 |
|-----|------|------|
| `satp` | Supervisor Address Translation and Protection | 页表基地址 + 模式 (Sv39) |
| `stvec` | Supervisor Trap Vector | 陷阱入口地址 |
| `sstatus` | Supervisor Status | SPP (之前的特权级), SPIE, SIE |
| `sepc` | Supervisor Exception PC | 陷阱时的程序计数器 |
| `scause` | Supervisor Cause | 陷阱原因 (8=ecall, 13=load page fault, 15=store page fault) |
| `stval` | Supervisor Trap Value | 陷阱相关信息 (如页错误地址) |
| `sscratch` | Supervisor Scratch | 临时寄存器 (用于 uservec) |
| `mstatus` | Machine Status | MPP, MPIE, MIE |
| `mepc` | Machine Exception PC | M 模式陷阱返回地址 |
| `mtvec` | Machine Trap Vector | M 模式陷阱入口 |
| `mie` | Machine Interrupt Enable | 中断使能位 (MTIE, MSIE, MEIE) |
| `mhartid` | Machine Hart ID | 当前 CPU 核 ID |

### B. 关键常量速查

| 常量 | 值 | 定义于 | 说明 |
|------|-----|--------|------|
| `PGSIZE` | 4096 | riscv.h | 页大小 |
| `NCPU` | 8 | param.h | 最大 CPU 数 |
| `NPROC` | 64 | param.h | 最大进程数 |
| `NOFILE` | 16 | param.h | 每进程最大打开文件数 |
| `NFILE` | 100 | param.h | 全局打开文件数 |
| `NINODE` | 50 | param.h | 内存 inode 表大小 |
| `ROOTDEV` | 1 | param.h | 根设备号 |
| `MAXARG` | 32 | param.h | exec 最大参数数 |
| `BSIZE` | 1024 | fs.h | 文件系统块大小 |
| `NDIRECT` | 12 | fs.h | 直接块数 |
| `NINDIRECT` | 256 | fs.h | 间接块数 (BSIZE/4) |
| `MAXFILE` | 65580 | fs.h | 最大文件块数（含二级间接块） |
| `DIRSIZ` | 14 | fs.h | 目录项名最大长度 |
| `PIPESIZE` | 512 | param.h | 管道缓冲区大小 |
| `USERSTACK` | 1 | memlayout.h | 用户栈页数 |

### C. 参考资料

- [MIT 6.1810 课程网站](https://pdos.csail.mit.edu/6.1810/)
- [xv6 官方源码](https://github.com/mit-pdos/xv6-riscv)
- [RISC-V 特权架构手册](https://riscv.org/technical/specifications/)
- [xv6: a simple, Unix-like teaching operating system (book)](https://pdos.csail.mit.edu/6.1810/2024/xv6/book-riscv-rev4.pdf)
- [The RISC-V Instruction Set Manual](https://riscv.org/specifications/)
- John Lions, *Commentary on UNIX 6th Edition*

---

> **生成说明**: 本文档基于对 xv6-riscov 源码的完整阅读和分析生成, 涵盖了所有 `.c`、`.S`、`.h` 文件以及构建脚本。文档结构按照操作系统的模块化设计进行组织, 以便于理解教学操作系统的核心概念。
