# xv6-riscv 内核完全体路线

> 目标：在继续用户态扩展（动态链接、编辑器、完整 shell）之前，
> 先把内核侧缺失的语义和系统调用面补齐，达到“基本内核完全体”。
> 网络栈、图形、用户态动态链接暂不计入本阶段。

## 1. 已完成的内核基础

- RISC-V 多核启动、陷阱、PLIC、UART、virtio 磁盘。
- fork/exec/wait、MLFQ、优先级调度、COW fork、需求分页 mmap、交换空间、ASLR。
- clone 轻量线程、futex、tgid/tid、共享文件表/cwd/VMA、waitpid join、TLS、
  线程组退出、tgkill 精确信号投递、sigprocmask。
- 权限与用户/组、符号链接、FIFO、VFS 挂载、多磁盘、双间接块、日志恢复。
- 静态模块注册、动态 ELF 模块加载、strace、perf、/proc、崩溃转储。

## 2. 内核侧缺口与优先级

| 优先级 | 子系统 | 待完成任务 |
|---:|---|---|
| P0 | 进程组/作业控制 | `setpgid` / `getpgid` / `killpg`，随后补 `SIGSTOP` / `SIGCONT` 与 shell `jobs/fg/bg` |
| P0 | 线程语义 | `clone` flags、`exit_group`、多线程 `fork/exec` 语义 |
| P0 | 信号完整性 | `sigaction`、`sigpending`、进程组信号投递、标准信号编号补齐 |
| P1 | 系统调用面 | `dup2`、`getcwd`、`chroot`、`fsync`、`nanosleep`、`clock_gettime`、`select/poll`、`readv/writev`、`pipe2` |
| P1 | 内存管理 | `mprotect`、OOM/换页策略完善、`/dev/zero` `/dev/null` 等基础伪设备 |
| P2 | 文件系统 | `chroot`、文件锁、`fsync`、更完整目录操作 |
| P2 | IPC | System V 或 POSIX 信号量、futex 超时/robust 语义 |
| P2 | 模块基础设施 | 内核符号导出表、模块参数、模块依赖、卸载安全检查 |
| P3 | 设备/时钟 | `/dev/clock`、RTC、更多 virtio 设备 |
| 延后 | 网络栈 | 轻量 TCP/UDP，单独作为大项目 |

## 3. 分批次计划

### P3-K1：进程组 API

状态：已完成

- 新增 `setpgid` / `getpgid` / `killpg` 系统调用。
- `struct proc` 增加 `pgid`，fork 继承父进程组，clone 线程共享进程组。
- `killpg` 按进程组投递信号，只影响目标组，不影响调用者所在组。
- 新增 `pgid_basic` 回归测试。

### P3-K2a：内核 STOPPED 状态与 waitpid 选项

状态：已完成

- `enum procstate` 新增 `STOPPED`，`SIGSTOP` / `SIGTSTP` 延迟到返回用户态前生效。
- 停止后的进程不再返回用户态，而是切到调度器等待 `SIGCONT`。
- `waitpid_flags` 支持 `WUNTRACED` / `WCONTINUED`，停止和继续事件都不会回收 PCB。
- `SIGKILL` 和线程组退出会把停止进程恢复为可运行后回收。

### P3-K2b：shell 作业控制

状态：已完成

- shell 支持 `jobs`、`fg`、`bg`、`stop`。
- `Ctrl-Z` 通过 console 前台进程组投递 `SIGTSTP`，`Ctrl-C` 投递 `SIGINT`。
- 后台命令创建独立进程组，shell 用 `waitpid_flags` 跟踪停止/继续事件。

### P3-K3：线程语义收口

状态：已完成

- `clone` 支持 `CLONE_VM` / `CLONE_FILES` / `CLONE_FS` / `CLONE_THREAD`，
  文件描述符表与 `cwd` 拆为独立共享对象，各 flag 可以分别生效。
- 新增 `exit_group`，组内任意线程调用后终止整个线程组，并保留退出状态。
- 多线程 `fork` 只复制调用线程；多线程 `exec` 在成功提交新镜像前终止其余线程，
  失败时保留线程组。
- 新增 `clone_flags_files`、`clone_flags_fs`、`clone_flags_vm`、
  `clone_thread_fork`、`exit_group_basic`、`exec_thread_cleanup` 回归测试。

### P3-K4：信号完整性

状态：已完成

- 补齐标准信号编号，`SIGSTOP` 修正为 19，并新增 `SIGCHLD`、`SIGTERM`、
  `SIGSEGV`、`SIGPIPE` 等常用信号。
- 新增 `sigaction` / `sigpending`，`sigaction` 支持 `sa_mask`、
  `SA_NODEFER`、`SA_RESETHAND`，`sigreturn` 恢复进入处理器前的阻塞掩码。
- 新增标准默认动作表：终止、忽略、停止、继续；`SIGCHLD` / `SIGURG`
  默认忽略，`SIGTSTP` / `SIGTTIN` / `SIGTTOU` 默认停止。
- `sigkill(-pgid, sig)` 支持按进程组投递信号。
- 新增 `sigaction_mask`、`sigpending_basic`、`sigaction_reset`、
  `kill_negative_pgid` 回归测试。

### P3-K5：系统调用面补齐

状态：待开始

- `dup2`、`getcwd`、`chroot`、`fsync`、`nanosleep`、
  `clock_gettime`、`select/poll`、`readv/writev`、`pipe2`。

### P3-K6：内存与文件系统补强

状态：待开始

- `mprotect`、基础伪设备、文件锁、`chroot`、OOM/换页策略完善。

### P3-K7：IPC 与模块基础设施

状态：待开始

- 信号量、futex 超时/robust 语义、内核符号导出表、模块参数/依赖。

### P3-K8：设备与时钟

状态：待开始

- `/dev/clock`、RTC、更多 virtio 设备。

## 4. 完成判定

- 进程、线程、信号、调度语义不存在明显的“半成品”行为。
- 系统调用面足以支撑常用用户态程序，不再为缺失 API 频繁改内核。
- 内存、文件系统、IPC 的主要接口可用并有回归测试。
- 动态模块体系可以安全加载、卸载并支持内核符号引用。
- 网络和用户态动态链接作为后续独立阶段，不阻塞本路线。
