# xv6-riscv 功能扩展与深度改造思路

> **目的**: 通过为 xv6 添加新功能，深入理解操作系统核心概念。  
> **难度标记**: 🟢 入门  🟡 中等  🔴 进阶  ⚫ 挑战

---

## 目录

1. [进程管理](#1-进程管理)
2. [内存管理](#2-内存管理)
3. [文件系统](#3-文件系统)
4. [进程间通信 (IPC)](#4-进程间通信-ipc)
5. [调度器](#5-调度器)
6. [网络栈](#6-网络栈)
7. [安全与权限](#7-安全与权限)
8. [设备驱动](#8-设备驱动)
9. [用户空间增强](#9-用户空间增强)
10. [可观测性与调试](#10-可观测性与调试)

---

## 1. 进程管理

### 1.1 🟢 实现进程优先级调度

**现状**: xv6 使用简单的轮转 (round-robin) 调度，`scheduler()` 在 `[proc.c:424](../kernel/proc.c)` 中遍历进程表，选择第一个 `RUNNABLE` 进程。

**改造思路**:
1. 在 `struct proc` 中增加 `int priority` 字段 (0=高 ~ 127=低)
2. 在 `struct proc` 中增加 `int runtime_ticks` 字段记录累计运行时间
3. 修改 `allocproc()` 设置默认优先级
4. 修改 `scheduler()` 不再简单遍历，而是选择 `RUNNABLE` 中 priority 最小的进程
5. 增加 `setpriority(int pid, int prio)` 系统调用
6. 实现优先级老化 (priority aging)：定期提升长期等待进程的优先级，防止饥饿
7. 添加 `nice(int inc)` 系统调用供用户程序调整自身优先级

**涉及文件**: [proc.h](../kernel/proc.h), [proc.c](../kernel/proc.c), [syscall.h](../kernel/syscall.h), [syscall.c](../kernel/syscall.c), [sysproc.c](../kernel/sysproc.c), [usys.pl](../user/usys.pl), [user.h](../user/user.h)

**学习重点**: 调度策略设计、饥饿问题、CPU 时间统计

---

### 1.2 🟡 实现 Unix 信号机制

**现状**: xv6 仅有简单的 `kill` → `killed` 标志机制，进程在从 `usertrap()` 返回用户空间时检查 `killed` 标志并退出。

**改造思路**:
1. 定义信号编号 `SIGKILL(9)`, `SIGINT(2)`, `SIGSTOP(17)`, `SIGCONT(18)`, `SIGCHLD(17)` 等
2. 在 `struct proc` 中增加信号处理表:
   ```c
   struct sigaction {
       void (*sa_handler)(int);  // 信号处理函数
       uint64 sa_mask;           // 信号掩码
       int sa_flags;             // SA_RESTART 等
   };
   struct sigaction sigactions[NSIG];  // 每进程信号处理表
   uint64 pending_signals;      // 位图：待处理信号
   uint64 blocked_signals;      // 位图：被阻塞的信号
   struct trapframe saved_tf;   // 保存用户上下文用于信号处理后恢复
   ```
3. 实现 `sigaction()` / `signal()` 系统调用
4. 实现 `sigprocmask()` 系统调用 (阻塞/解除阻塞信号)
5. 实现 `sigreturn()` 系统调用 (从信号处理函数返回)
6. 修改 `kkill()` 向目标进程递送信号
7. 修改 `usertrap()` 和 `prepare_return()`，在返回用户空间前检查并递送信号:
   - 保存当前 trapframe 到 `saved_tf`
   - 修改 trapframe 的 `epc` 为信号处理函数地址
   - 在用户栈上构造返回帧 (包含 `sigreturn` 调用)
   - 设置 `ra` 指向 sigreturn 桩

**涉及文件**: [proc.h](../kernel/proc.h), [proc.c](../kernel/proc.c), [trap.c](../kernel/trap.c), [syscall.h](../kernel/syscall.h), [syscall.c](../kernel/syscall.c), [sysproc.c](../kernel/sysproc.c), trampoline.S

**学习重点**: 异步事件处理、用户态栈帧构造、控制流劫持与恢复

---

### 1.3 🔴 实现内核级线程与 clone()

**现状**: xv6 只有进程 fork，父子进程有独立的地址空间。

**改造思路**:
1. 区分"线程"和"进程"概念：
   - 进程 = 独立的地址空间 (pagetable)
   - 线程 = 共享地址空间但独立的栈和执行流
2. 实现 `clone(flags, stack, ...)` 系统调用:
   - `CLONE_VM`: 共享地址空间 (不复制页表)
   - `CLONE_FILES`: 共享文件描述符表
   - `CLONE_FS`: 共享当前目录
   - `CLONE_THREAD`: 放入同一线程组
3. 修改 `allocproc()` 支持线程创建：
   - 如果 `CLONE_VM` 设置，直接共享父进程的 `pagetable`，但需要增加引用计数
   - 为线程分配独立的 trapframe 和内核栈
   - 线程有独立的 tid 但共享 pid
4. 实现线程同步原语:
   - `futex()` 系统调用 (fast userspace mutex)
   - 或实现 `pthread_mutex` 的内核支持
5. 修改 `kexit()` 处理线程退出 vs 进程退出
6. 修改 `kwait()` 等待线程

**涉及文件**: [proc.h](../kernel/proc.h), [proc.c](../kernel/proc.c), [vm.c](../kernel/vm.c), [exec.c](../kernel/exec.c), [sysproc.c](../kernel/sysproc.c)

**学习重点**: 共享资源管理、引用计数、线程 vs 进程语义、TLS (线程局部存储)

---

### 1.4 🟡 实现进程组与作业控制

**现状**: shell 的 `&` 仅实现后台运行，没有作业控制 (jobs, fg, bg, Ctrl-Z)。

**改造思路**:
1. 在 `struct proc` 中添加 `struct proc *pgleader` (进程组组长)
2. 添加 `pid_t pgid` (进程组 ID)
3. 实现 `setpgid(pid, pgid)` 系统调用
4. 实现终端的前台进程组概念 (跟踪当前控制终端的前台进程组)
5. 实现 Ctrl-Z 发送 `SIGTSTP`，停止前台进程组
6. 实现 `SIGCONT` 继续被停止的进程
7. Shell 端实现 `jobs`, `fg`, `bg` 内部命令
8. 结合信号机制 (1.2) 一起实现

**涉及文件**: [proc.h](../kernel/proc.h), [proc.c](../kernel/proc.c), [console.c](../kernel/console.c), [sysproc.c](../kernel/sysproc.c), [sh.c](../user/sh.c)

**学习重点**: 终端控制、作业抽象、Shell-内核协作

---

## 2. 内存管理

### 2.1 🟡 实现写时复制 (Copy-on-Write) Fork

**现状**: `uvmcopy()` 在 `[vm.c:296](../kernel/vm.c)` 中逐页复制物理内存，fork 大进程代价高。

**改造思路**:
1. 为每个物理页维护引用计数 (需要一个独立的引用计数数组或 hash 表)
   - 物理页引用计数初始为 1 (被一个进程引用)
   - fork 时：子进程的 PTE 指向相同物理页，清除 `PTE_W` 位 (标记只读)，设置 COW 标志位 (可用 RSW 位)
   - 物理页引用计数 +1
2. 写时复制处理:
   - 父或子进程写入只读页 → Page Fault (scause=15)
   - 在 `vmfault()` / `usertrap()` 中检查是否为 COW 页 (`PTE_W` clear + RSW bit set)
   - 引用计数 == 1：直接恢复 `PTE_W`
   - 引用计数 > 1：分配新页，复制内容，更新 PTE，引用计数-1
3. 修改 `uvmcopy()`：不分配新页，只复制 PTE 并设置 COW
4. 修改 `kfree()`：检查引用计数，仅当为 0 时释放
5. 修改 `copyout()`：检查 COW 页 (内核写用户页时也需要触发 COW)

**涉及文件**: [vm.c](../kernel/vm.c), [kalloc.c](../kernel/kalloc.c), [trap.c](../kernel/trap.c), [riscv.h](../kernel/riscv.h)

**学习重点**: 页级引用计数、惰性复制、Page Fault 处理、物理页生命周期

---

### 2.2 🔴 实现完整的需求分页 (Demand Paging)

**现状**: xv6 已经实现了基于 sbrk 的惰性分配 (`vmfault()`)，但 exec 加载程序时仍然一次全部加载。

**改造思路**:
1. 修改 `exec.c` 的 `kexec()`:
   - ELF 加载时不调用 `uvmalloc` 分配物理页
   - 仅为 text/data 段建立虚拟地址映射但不分配物理页
   - 在 PTE 中设置一个 swap-like 标记表示"来自文件"
   - 记录每个段的文件映射信息 (文件 inode, 文件偏移, 大小, 权限)
2. 在 `struct proc` 中添加 VMA (Virtual Memory Area) 链表:
   ```c
   struct vma {
       uint64 vm_start, vm_end;  // 虚拟地址范围
       int prot;                  // 保护属性
       int flags;                 // MAP_PRIVATE / MAP_SHARED
       struct inode *ip;          // 映射的文件
       uint off;                  // 文件偏移
       struct vma *next;
   };
   ```
3. Page Fault 处理增强:
   - 检查 VMA 链表找到对应的映射
   - 从文件读取页面内容到新分配的物理页
   - 建立映射
4. 实现页面回收 (Page Reclamation):
   - 当物理内存不足时，回收 clean 页 (直接释放) 和 dirty 页 (写回文件或 swap)
5. 实现 `mmap()` / `munmap()` 系统调用

**涉及文件**: [vm.c](../kernel/vm.c), [exec.c](../kernel/exec.c), [kalloc.c](../kernel/kalloc.c), [trap.c](../kernel/trap.c), [proc.h](../kernel/proc.h), [sysfile.c](../kernel/sysfile.c)

**学习重点**: VMA 管理、按需加载、内存过量使用 (overcommit)、页面回收

---

### 2.3 🔴 实现交换 (Swap) 机制

**现状**: xv6 的物理内存严格限制在 128MB，无法使用超过物理内存的空间。

**改造思路**:
1. 在磁盘上预留 swap 分区 (或在 fs.img 中创建 swap 文件)
2. 实现页面换出 (Swap-out):
   - 页面回收算法 (LRU/Clock)
   - 将选中的用户页内容写入 swap 区
   - 更新 PTE 为无效，记录 swap 位置
3. 实现页面换入 (Swap-in):
   - Page Fault 时检查 PTE 是否指向 swap 区
   - 从 swap 区读入物理内存
4. 修改 `kalloc()` 在内存不足时触发页面回收
5. 实现交换统计 (pgfault, pswpin, pswpout)

**涉及文件**: [vm.c](../kernel/vm.c), [kalloc.c](../kernel/kalloc.c), [trap.c](../kernel/trap.c), 新建 swap.c

**学习重点**: 二级存储管理、页面置换算法、颠簸 (thrashing)

---

### 2.4 🟡 实现共享内存

**现状**: 进程间通过管道或文件共享数据，无直接共享内存。

**改造思路**:
1. 实现 `shmget(key, size, flags)` 系统调用:
   - 创建/获取共享内存段
   - 全局共享内存段表 (`struct shmid_ds`)
   - 权限检查
2. 实现 `shmat(shmid, addr, flags)` 系统调用:
   - 将共享内存段映射到进程地址空间
   - 在进程的 VMA 链表中添加共享段
   - 物理页由多个进程共享 (引用计数管理)
3. 实现 `shmdt(addr)` 系统调用:
   - 解除映射
   - 减少引用计数
4. 实现 `shmctl(shmid, cmd, buf)` 系统调用:
   - IPC_RMID: 标记删除
   - IPC_STAT: 获取状态
   - IPC_SET: 设置属性
5. 在 `kexit()` 中清理进程的所有共享内存附加

**涉及文件**: [vm.c](../kernel/vm.c), [proc.h](../kernel/proc.h), [proc.c](../kernel/proc.c), 新建 shm.c

**学习重点**: 共享资源管理、IPC 命名空间、System V IPC 模型

---

## 3. 文件系统

### 3.1 🟢 实现符号链接 (Symlink)

**现状**: xv6 仅支持硬链接 (`link`/`unlink`)，不支持符号链接。

**改造思路**:
1. 新增 inode 类型 `T_SYMLINK` (4)
2. 实现 `symlink(char *target, char *linkpath)` 系统调用:
   - 创建一个新的 `T_SYMLINK` inode
   - 将目标路径字符串写入该 inode 的数据块 (`writei`)
   - 在 `linkpath` 目录中创建目录项指向该 inode
3. 修改 `namex()` 在 [fs.c:675](../kernel/fs.c) 中处理符号链接:
   - 解析路径时遇到 `T_SYMLINK` 类型
   - 读取链接内容 (目标路径)
   - 拼接剩余路径
   - 递归解析 (需限制递归深度防止循环)
4. 修改 `open()` 系统调用:
   - 添加 `O_NOFOLLOW` 标志 (打开链接本身而非目标)
5. 实现 `readlink()` 系统调用 (读取链接内容)
6. 安全检查: 最大 symlink 深度 (如 10 层)

**涉及文件**: [fs.h](../kernel/fs.h), [fs.c](../kernel/fs.c), [sysfile.c](../kernel/sysfile.c), [stat.h](../kernel/stat.h), [fcntl.h](../kernel/fcntl.h)

**学习重点**: 路径解析递归、inode 类型扩展、文件系统原子性

---

### 3.2 🟡 实现文件权限系统

**现状**: xv6 没有用户概念，所有进程具有完全访问权限。

**改造思路**:
1. 实现用户 ID 系统 (结合 7.1 的用户/组):
2. 扩展 `struct dinode` 添加:
   ```c
   ushort uid;   // 所有者用户 ID
   ushort gid;   // 所有者组 ID
   ushort mode;  // 文件类型 + 权限位 (rwxrwxrwx)
   ```
3. 修改 `ialloc()` 创建 inode 时设置所有者
4. 实现权限检查函数 `accessok(struct inode *ip, int want)`:
   - 进程为 root (uid=0)：全部允许
   - 进程 uid == ip->uid：检查 owner 权限
   - 进程属于同组：检查 group 权限
   - 否则：检查 other 权限
5. 在所有文件系统操作中加入权限检查:
   - `open()`: 检查读/写权限
   - `mkdir()` / `unlink()` / `link()`: 检查父目录写权限
   - `exec()`: 检查执行权限
6. 实现 `chmod(path, mode)` 系统调用
7. 实现 `chown(path, uid, gid)` 系统调用
8. 修改 `mkfs/mkfs.c` 生成正确的权限

**涉及文件**: [fs.h](../kernel/fs.h), [fs.c](../kernel/fs.c), [sysfile.c](../kernel/sysfile.c), [file.h](../kernel/file.h), [mkfs/mkfs.c](../mkfs/mkfs.c)

**学习重点**: 自主访问控制 (DAC)、权限位设计、setuid/setgid 语义

---

### 3.3 🔴 支持更大的文件 (双重间接块)

**现状**: xv6 最大文件 = `(12 + 256) * 1024 = 268KB`，受限于 12 个直接块 + 1 个间接块。

**改造思路**:
1. 在 `struct dinode` 中增加 `uint addrs[NDIRECT+2]` (添加双重间接块)
2. 修改 `bmap()` 添加双重间接块支持:
   ```c
   if (bn < NDIRECT) { ... }                              // 直接块
   bn -= NDIRECT;
   if (bn < NINDIRECT) { ... }                            // 一级间接块
   bn -= NINDIRECT;
   if (bn < NINDIRECT * NINDIRECT) {                      // 二级间接块
       uint idx1 = bn / NINDIRECT;   // 一级索引
       uint idx2 = bn % NINDIRECT;   // 二级索引
       // 读取双重间接块 → 从一级间接块 → 到数据块
   }
   ```
3. 修改 `itrunc()` 释放双重间接块
4. 修改 `iappend()` 在 mkfs 中
5. 最大文件变为: `(12 + 256 + 256*256) * 1024 ≈ 64MB`

**涉及文件**: [fs.h](../kernel/fs.h), [fs.c](../kernel/fs.c), [mkfs/mkfs.c](../mkfs/mkfs.c)

**学习重点**: 大文件索引、多级索引性能、空间利用率分析

---

### 3.4 🟡 实现多文件系统挂载

**现状**: xv6 仅支持单个根文件系统 (ROOTDEV)。

**改造思路**:
1. 实现 mount 表 (全局):
   ```c
   struct mount {
       int dev;                  // 挂载的设备
       struct inode *mountpoint; // 挂载点目录
       struct inode *root;       // 挂载文件系统的根 inode
       struct mount *next;
   };
   ```
2. 修改 `namex()` 路径解析:
   - 当路径穿过挂载点时，切换到挂载文件系统的根
   - 遇到 ".." 时检查是否需要回到父文件系统
3. 实现 `mount(dev, mountpoint)` 系统调用
4. 实现 `umount(mountpoint)` 系统调用
5. 实现多磁盘支持 (结合 8.3 多 virtio 磁盘)
6. 实现 `/proc` 伪文件系统 (结合 10.2)

**涉及文件**: [fs.c](../kernel/fs.c), [sysfile.c](../kernel/sysfile.c), [file.h](../kernel/file.h)

**学习重点**: VFS (虚拟文件系统) 概念、命名空间、设备抽象

---

## 4. 进程间通信 (IPC)

### 4.1 🟢 实现命名管道 (FIFO)

**现状**: xv6 仅支持匿名管道 (`pipe()` 系统调用)。

**改造思路**:
1. 新增 inode 类型 `T_FIFO` (4 或 5)
2. 实现 `mkfifo(path, mode)` 系统调用:
   - 创建 `T_FIFO` 类型的 inode
   - 不分配数据块 (管道缓冲区在内存中)
3. 修改 `open()` 处理 `T_FIFO`:
   - 打开 FIFO 时创建 `struct pipe` 内核对象
   - 读端打开：如果没有写端，阻塞等待 (除非 `O_NONBLOCK`)
   - 写端打开：如果没有读端，阻塞等待
4. 修改 `fileclose()` 清理 FIFO 的管道缓冲区
5. 允许多个进程同时读写同一个 FIFO

**涉及文件**: [pipe.c](../kernel/pipe.c), [fs.h](../kernel/fs.h), [fs.c](../kernel/fs.c), [sysfile.c](../kernel/sysfile.c), [file.c](../kernel/file.c)

**学习重点**: 命名 vs 匿名 IPC、文件系统与 IPC 的交集

---

### 4.2 🟡 实现 Unix Domain Socket

**现状**: 仅支持管道 IPC。

**改造思路**:
1. 定义 socket 类型 `FD_SOCKET`
2. 实现 `socketpair()` 系统调用 (创建一对已连接的 socket)
3. 实现 `socket()` / `bind()` / `listen()` / `connect()` / `accept()` 系统调用
4. 在文件系统中创建 socket 文件节点 (与 FIFO 类似)
5. socket 内部缓冲区管理 (流式、消息边界)
6. 支持 `SOCK_STREAM` (流式) 和 `SOCK_DGRAM` (数据报)

**涉及文件**: [file.h](../kernel/file.h), [file.c](../kernel/file.c), 新建 socket.c, [sysfile.c](../kernel/sysfile.c)

**学习重点**: Socket API 语义、连接建立流程、流式 vs 数据报

---

### 4.3 🟡 实现信号量 (Semaphore)

**现状**: 无内核级信号量支持。

**改造思路**:
1. 实现 System V 信号量:
   - `semget(key, nsems, flags)`: 创建/获取信号量集合
   - `semop(semid, sops, nsops)`: 原子操作 (P/V)
   - `semctl(semid, semnum, cmd, ...)`: 控制操作
2. 信号量的内核数据结构:
   ```c
   struct sem {
       int val;             // 当前值
       int waiters;         // 等待进程数
       struct spinlock lock;
   };
   struct semid_ds {
       struct sem *sems;
       int nsems;
       int refcnt;
   };
   ```
3. P (wait/acquire) 操作: 如果 `val > 0` 则 `val--`，否则 sleep
4. V (signal/release) 操作: `val++`，wakeup 等待者
5. `SEM_UNDO`: 进程退出时自动释放持有的信号量

**涉及文件**: 新建 sem.c, [syscall.h](../kernel/syscall.h), [proc.c](../kernel/proc.c)

**学习重点**: 同步原语、死锁避免、原子操作

---

## 5. 调度器

### 5.1 🟡 实现多级反馈队列 (MLFQ)

**现状**: 简单的单队列轮转调度。

**改造思路**:
1. 定义多个优先级队列 (如 3~4 个级别)
2. 规则:
   - 高优先级队列时间片短 (如 1 tick)
   - 低优先级队列时间片长 (如 4, 8, 16 ticks)
   - 新进程进入最高优先级
   - 用完时间片 → 降级
   - 定期 boost: 将所有进程提升到最高优先级 (防止饥饿)
3. 修改 `scheduler()`:
   - 从高到低遍历队列
   - 选择第一个非空队列的第一个进程
4. 修改 `clockintr()` 更新进程的运行时间
5. 修改 `yield()` / `sleep()` / `wakeup()` 考虑优先级队列

**涉及文件**: [proc.h](../kernel/proc.h), [proc.c](../kernel/proc.c), [trap.c](../kernel/trap.c)

**学习重点**: 公平调度、优先级反转、交互式 vs 批处理

---

### 5.2 🔴 实现 O(1) 或 CFS 调度器

**现状**: O(N) 遍历进程表。

**改造思路 (CFS 简化版)**:
1. 使用红黑树或跳表组织进程 (按 `vruntime`)
2. `vruntime` = 实际运行时间 × (NICE_0_LOAD / 进程权重)
3. `scheduler()` 始终选择 `vruntime` 最小的进程
4. 运行时间记录在 `clockintr()` 中
5. 新进程的 `vruntime` = 当前最小 `vruntime` (避免新进程获得不公平的优势)
6. 实现红黑树 (从 Linux 简化移植或用跳表简化)

**涉及文件**: [proc.h](../kernel/proc.h), [proc.c](../kernel/proc.c), 新建 rbtree.c/rbtree.h

**学习重点**: 完全公平调度、红黑树、虚拟运行时间、权重计算

---

## 6. 网络栈

### 6.1 🔴 实现轻量 TCP/IP 协议栈

**现状**: xv6 无网络支持。

**改造思路**:
1. 移植 lwIP (轻量级 TCP/IP 协议栈)
2. 实现 virtio-net 驱动 (QEMU 的 virtio 网络设备):
   - MMIO 初始化类似 virtio_disk
   - virtqueue 用于发送/接收网络包
3. 实现 socket API:
   - `socket()`, `bind()`, `listen()`, `connect()`, `accept()`
   - `send()`, `recv()`, `sendto()`, `recvfrom()`
   - `getsockopt()`, `setsockopt()`
4. 实现 select/poll 多路复用
5. 协议支持:
   - TCP (可靠流)
   - UDP (不可靠数据报)
   - ARP (地址解析)
   - DHCP (自动获取 IP)
6. 用户程序: 简单的 HTTP 服务器、telnet 客户端

**涉及文件**: 大量新建文件 (net/, lwip/), [virtio.h](../kernel/virtio.h), [file.h](../kernel/file.h), [syscall.c](../kernel/syscall.c)

**学习重点**: TCP/IP 协议、socket 编程模型、网络驱动模型、协议栈分层

---

### 6.2 🟡 实现简化的 UDP 协议栈

**难度低于完整 TCP/IP**:

1. 实现 virtio-net 驱动
2. 仅实现 IP + UDP + ARP:
   - Ethernet 帧封装/解封
   - ARP 请求/响应
   - IP 路由 (最简单：仅本地子网)
   - UDP 发送/接收
   - 无需 TCP 的连接管理和拥塞控制
3. socket API 限制为 `SOCK_DGRAM`

**涉及文件**: 新建 net/ (eth.c, arp.c, ip.c, udp.c), virtio_net.c

**学习重点**: 网络分层、协议封装、checksum 计算

---

## 7. 安全与权限

### 7.1 🟡 实现用户和组

**现状**: 所有进程以相同权限运行，无用户概念。

**改造思路**:
1. 全局定义:
   ```c
   #define NUSERS 32
   struct user {
       char name[32];
       ushort uid;
       ushort gid;
       char passwd[64];  // 哈希后的密码
   };
   struct user users[NUSERS];
   ```
2. 在 `struct proc` 中增加:
   ```c
   ushort uid;     // 真实用户 ID
   ushort euid;    // 有效用户 ID (用于权限检查)
   ushort gid;     // 真实组 ID
   ushort egid;    // 有效组 ID
   ushort suid;    // 保存的用户 ID
   ```
3. 实现 `login` 程序 (用户名/密码验证)
4. 实现 `/etc/passwd` 文件解析
5. 修改 `exec()` 支持 setuid bit
6. 文件权限检查 (结合 3.2)
7. 系统调用: `getuid()`, `geteuid()`, `setuid()`, `getgid()`, `setgid()`
8. init 进程以 root (uid=0) 运行，login 后切换用户
9. 修改 `userinit()` 设置 init 的 uid=0

**涉及文件**: [proc.h](../kernel/proc.h), [proc.c](../kernel/proc.c), [sysproc.c](../kernel/sysproc.c), [sysfile.c](../kernel/sysfile.c), [exec.c](../kernel/exec.c), 新建 user/login.c

**学习重点**: 权限模型、setuid 机制、进程凭证管理

---

### 7.2 🟡 实现 chroot 机制

**改造思路**:
1. 在 `struct proc` 中添加 `struct inode *root` (进程根目录)
2. 修改 `namex()` 路径解析:
   - 如果路径以 '/' 开头，从 `p->root` 开始 (而非全局 `ROOTDEV`)
3. 实现 `chroot(path)` 系统调用:
   - 仅允许 root (uid=0) 调用
   - 设置 `p->root` 为指定目录的 inode
4. 在 `kfork()` 中继承 root

**涉及文件**: [proc.h](../kernel/proc.h), [fs.c](../kernel/fs.c), [sysfile.c](../kernel/sysfile.c)

**学习重点**: 命名空间隔离、容器基础概念

---

### 7.3 🔴 实现地址空间布局随机化 (ASLR)

**改造思路**:
1. 随机化用户栈的起始位置:
   - 在 `kexec()` 的栈分配中，随机偏移 (如 0~16 页)
2. 随机化 mmap 区域
3. 随机化 text/data 段的加载地址 (需要 PIE 支持)
4. 随机化堆的起始位置
5. 使用硬件随机数源 (RISC-V 的 `seed` 扩展或定时器)
6. 实现随机数生成器

**涉及文件**: [exec.c](../kernel/exec.c), [vm.c](../kernel/vm.c)

**学习重点**: 安全缓解技术、随机化与兼容性

---

## 8. 设备驱动

### 8.1 🟢 实现简单的图形显示

**改造思路**:
1. 在 QEMU 中启用 virtio-gpu 或简单 framebuffer:
   - QEMU 的 `-device virtio-gpu` 或 `-device VGA`
   - 或使用 `-device sysfb` (简单 framebuffer)
2. 映射 framebuffer 到内核地址空间
3. 实现简单的图形操作:
   - `draw_pixel(x, y, color)`
   - `fill_rect(x, y, w, h, color)`
   - `draw_char(x, y, c)` (使用内置字体)
4. 设备文件 `/dev/fb0` 允许用户程序 mmap framebuffer
5. 用户程序: 简单的图形 demo (mandelbrot, 画线, etc.)

**涉及文件**: 新建 fb.c, [memlayout.h](../kernel/memlayout.h), [file.c](../kernel/file.c)

**学习重点**: 内存映射 I/O、framebuffer、像素操作

---

### 8.2 🟡 实现多磁盘支持

**改造思路**:
1. 在 QEMU 命令行添加多个 `-drive` 设备:
   ```
   -drive file=fs1.img,if=none,format=raw,id=x1
   -device virtio-blk-device,drive=x1,bus=virtio-mmio-bus.1
   ```
2. 修改 `virtio_disk_init()` 支持多个设备:
   - 扫描 virtio MMIO 总线寻找所有块设备
   - 为每个磁盘创建独立的 `struct disk`
3. 修改文件系统层支持多设备:
   - `ROOTDEV` 仍为根设备
   - 新增设备号分配
4. 实现 `mount` / `umount` (结合 3.4)

**涉及文件**: [virtio_disk.c](../kernel/virtio_disk.c), [virtio.h](../kernel/virtio.h), [fs.c](../kernel/fs.c), [bio.c](../kernel/bio.c)

**学习重点**: 设备枚举、总线扫描、多设备管理

---

### 8.3 🟢 实现更高精度的定时器

**现状**: 定时器中断周期约为 1/10 秒。

**改造思路**:
1. 修改 `clockintr()` 中的 `stimecmp` 值实现毫秒级中断
2. 实现高精度睡眠 `usleep(microseconds)` 系统调用
3. 实现 `gettimeofday()` 系统调用
4. 使用 RISC-V time CSR 实现微秒级时间戳

**涉及文件**: [trap.c](../kernel/trap.c), [start.c](../kernel/start.c), [sysproc.c](../kernel/sysproc.c)

**学习重点**: 定时器硬件编程、时间管理、中断频率权衡

---

## 9. 用户空间增强

### 9.1 🟢 增强 Shell 功能

**现状**: Shell ([sh.c](../user/sh.c)) 功能较基础。

**改造思路**:
1. **环境变量**: 
   - 在进程控制块中添加 `char **envp`
   - 实现 `getenv()`, `setenv()`, `unsetenv()`
   - Shell 支持 `export`, `$PATH`, `$HOME`
2. **命令历史**:
   - Shell 内维护历史缓冲区
   - 支持 `!!` 和上箭头 (如果终端支持)
3. **Tab 补全**:
   - 命令名和文件名补全
4. **脚本支持**:
   - 支持 `#!` shebang
   - 简单的 `if`, `for`, `while` 语句
5. **管道和重定向增强**:
   - `2>&1` 合并 stderr
   - `>>&` 追加重定向

**涉及文件**: [sh.c](../user/sh.c), [init.c](../user/init.c), [proc.h](../kernel/proc.h), [exec.c](../kernel/exec.c)

**学习重点**: Shell 编程、词法/语法分析、环境变量传递

---

### 9.2 🟡 实现动态链接与共享库

**现状**: 所有用户程序静态链接。

**改造思路**:
1. 定义共享库格式 (简化的 ELF shared object)
2. 编译用户程序为 PIE (位置无关可执行文件)
3. 实现动态链接器 `ld.so`:
   - 加载 ELF 的 DYNAMIC 段
   - 解析符号 (符号查找)
   - 重定位 (GOT/PLT 填充)
   - 延迟绑定 (lazy binding via PLT stub)
4. 修改 `exec()` 内核加载:
   - 检测是否为动态链接的 ELF
   - 将解释器 (`ld.so`) 作为实际的加载目标
   - 通过辅助向量 (aux vector) 传递信息给 `ld.so`
5. 将用户库 (ulib, printf, umalloc) 编译为 `libc.so`
6. 实现 `dlopen()`, `dlsym()`, `dlclose()`

**涉及文件**: [exec.c](../kernel/exec.c), [elf.h](../kernel/elf.h), 新建 user/ldso.c, Makefile

**学习重点**: ELF 格式、PLT/GOT、重定位、延迟绑定、共享内存映射

---

### 9.3 🟡 实现简单的文本编辑器

**改造思路**:
1. 实现类似 `ed` (行编辑器) 或简化的 `vi`:
   - 使用原始终端模式 (需要终端 ioctl)
   - 行缓冲读写
   - 插入、删除、替换操作
2. 或实现简化的全屏编辑器:
   - ANSI escape codes 控制光标
   - 基本编辑命令 (移动、插入、删除)
   - 文件打开/保存

**涉及文件**: 新建 user/edit.c, [console.c](../kernel/console.c)

**学习重点**: 终端控制、文本缓冲区、UI 编程

---

## 10. 可观测性与调试

### 10.1 🟢 实现 strace (系统调用追踪)

**改造思路**:
1. 在 `struct proc` 中添加 `int strace_on` 标志
2. 实现 `strace(pid, on)` 系统调用:
   - 设置目标进程的 `strace_on` 标志
3. 修改 `syscall()` 分发函数:
   - 如果 `p->strace_on`，在调用前后打印信息
   - 打印格式: `pid syscall_name(args) = result`
4. 实现系统调用名称表和参数解码
5. 用户程序 `strace` 用法:
   ```
   strace ls          # 追踪 ls 的系统调用
   strace -p 5        # 追踪 pid 5 的进程
   ```

**涉及文件**: [proc.h](../kernel/proc.h), [syscall.c](../kernel/syscall.c), [sysproc.c](../kernel/sysproc.c), 新建 user/strace.c

**学习重点**: 可观测性设计、系统调用拦截、用户态工具

---

### 10.2 🔴 实现 /proc 伪文件系统

**改造思路**:
1. 实现伪文件系统 (无磁盘后备):
   - 实现 VFS 层抽象 (结合 3.4 mount)
   - 文件 inode 的 read/write 直接访问内核数据结构
2. `/proc` 目录结构:
   ```
   /proc/
   ├── cpuinfo          # CPU 信息
   ├── meminfo          # 内存统计
   ├── uptime           # 系统运行时间
   ├── version          # 内核版本
   ├── self/            # 当前进程的符号链接
   └── <pid>/
       ├── stat         # 进程状态
       ├── cmdline      # 进程命令行
       ├── fd/          # 打开的文件描述符
       │   ├── 0 -> /dev/console
       │   ├── 1 -> /dev/console
       │   └── 2 -> /dev/console
       └── maps         # 内存映射 (VMA)
   ```
3. 实现动态生成文件内容 (on-demand read):
   - 不需要预先生成所有文件
   - 每次 read 时动态生成内容
4. 用户程序: `cat /proc/cpuinfo`, `ps` (利用 /proc)

**涉及文件**: 新建 procfs.c, [fs.c](../kernel/fs.c), [proc.c](../kernel/proc.c)

**学习重点**: 虚拟文件系统、内核数据导出、on-demand 数据生成

---

### 10.3 🟡 实现内核性能计数器

**改造思路**:
1. 使用 RISC-V 硬件性能计数器 (HPM counters):
   - 指令数: `cycle`, `instret`
   - 分支预测失败: `bpred`
   - Cache 缺失: 通过 QEMU 的 perf 模拟
2. 进程级别的性能统计:
   ```c
   struct perf_counters {
       uint64 cycles;
       uint64 instructions;
       uint64 page_faults;
       uint64 context_switches;
       uint64 syscalls;
   };
   ```
3. 在 `struct proc` 中添加 `struct perf_counters perf`
4. 在关键路径上更新计数器:
   - `clockintr()`: cycles
   - `syscall()`: syscalls
   - `vmfault()`: page_faults
   - `sched()` → `swtch()`: context_switches
5. 实现 `perf_getpid(pid)` 系统调用读取统计

**涉及文件**: [proc.h](../kernel/proc.h), [proc.c](../kernel/proc.c), [riscv.h](../kernel/riscv.h), [sysproc.c](../kernel/sysproc.c)

**学习重点**: 性能分析基础、硬件计数器、开销最小化

---

### 10.4 🟢 实现内核崩溃转储 (Kernel Crash Dump)

**改造思路**:
1. 增强 `panic()`:
   - 打印所有 CPU 的寄存器状态
   - 打印所有进程的信息
   - 打印内核栈回溯 (stack backtrace)
2. 实现栈回溯 (stack unwinding):
   - 利用 RISC-V frame pointer
   - 从当前 fp 向上遍历调用栈
   - 解析符号名称 (从 kernel.sym 加载)
3. 在 `panic()` 时将内存内容写入磁盘的预留区域
4. 引导时检测 crash dump 并显示

**涉及文件**: [printf.c](../kernel/printf.c), [proc.c](../kernel/proc.c), 新建 backtrace.c

**学习重点**: 调用栈布局、崩溃分析、事后调试

---

## 难度路线图

根据不同学习目标，推荐以下实现顺序：

### 入门路线 (掌握基本概念)
```
1. 进程优先级调度 (1.1)     →  2. 符号链接 (3.1)
    ↓                              ↓
3. strace (10.1)             →  4. 崩溃转储 (10.4)
    ↓                              ↓
5. Shell 增强 (9.1)          →  6. 命名管道 (4.1)
```

### 中级路线 (理解核心子系统)
```
1. 信号 (1.2)                →  2. 写时复制 fork (2.1)
    ↓                              ↓
3. 文件权限 + 用户/组 (3.2+7.1) →  4. 共享内存 (2.4)
    ↓                              ↓
5. MLFQ 调度 (5.1)           →  6. 多文件系统挂载 (3.4)
```

### 进阶路线 (深入操作系统设计)
```
1. 内核线程 clone (1.3)      →  2. 需求分页 mmap (2.2)
    ↓                              ↓
3. 双重间接块 (3.3)          →  4. Swap 交换 (2.3)
    ↓                              ↓
5. CFS 调度 (5.2)            →  6. 动态链接器 (9.2)
    ↓                              ↓
7. /proc 文件系统 (10.2)     →  8. UDP 网络栈 (6.2)
```

### 挑战路线 (完整 OS 经验)
```
1. TCP/IP 协议栈 (6.1)       →  2. ASLR (7.3)
    ↓                              ↓
3. Socket API (6.1+4.2)      →  4. 完整 VFS + 多设备 (3.4+8.2)
```

---

## 通用改造技巧

1. **增量开发**: 先实现最小可用版本，逐步完善
2. **善用测试**: `usertests.c` 是很好的参考，为新功能编写测试
3. **阅读参考**: Linux 0.11, Minix, FreeBSD 等真实 OS 的实现
4. **利用 QEMU**: GDB 调试 (`make qemu-gdb`) 是理解代码的最佳方式
5. **谨慎修改头文件**: `memlayout.h`, `riscv.h`, `proc.h` 等的修改会影响全局，需格外小心
6. **锁序**: 添加新锁时必须考虑死锁问题
7. **保持简单**: xv6 的设计哲学是简洁，添加功能时保持这种精神
