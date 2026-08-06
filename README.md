# xv6-riscv

xv6 是 Dennis Ritchie 和 Ken Thompson 的 Unix Version 6 的教学重实现。
本仓库是面向 RISC-V 64 位多处理器的移植版本，并在保留教学简洁性的基础上，
增加了模块化拓展架构、动态模块加载、可观测性工具和一系列 P0 功能。

## 主要特性

- 基于 RISC-V Sv39 页表、多核调度、陷阱与中断、日志文件系统
- 核心源码补充中文注释和原理讲解
- 编译期模块注册表：模块自动扫描，无需反复修改 `Makefile`
- 通用系统调用 `module_call()`：新增功能优先通过模块命令暴露
- 事件钩子机制：tick、syscall enter/exit、proc fork/exit
- 完整设备文件接口：`open/read/write/close`
- 伪设备：`/dev/sysinfo`、`/dev/stats`
- `/proc` 支持小缓冲分页读取，进程多时不再截断
- 教学级动态模块加载：`modload` / `modunload`，支持多槽位并行
- 动态模块 ELF 格式加载：解析程序头、复制 LOAD 段并初始化 BSS
- 内核自测模块：`kernel_selftest`
- P0 工具：`strace`、`perf`、`prio`、`procinfo`
- 进程优先级调度与 `setpriority`
- MLFQ 多级反馈队列调度
- MLFQ 周期性提升按优先级恢复队列
- 内核崩溃转储与栈回溯：`crashdump`
- 共享内存：`shmget` / `shmat` / `shmdt` / `shmrm`
- 写时复制 fork：`cowtest`
- 信号机制：`signal` / `sigkill` / `sigreturn`，支持 `SIG_DFL` / `SIG_IGN` 与防重入
- `/proc` 伪文件系统：`ps`、`cat proc`
- 符号链接：`ln -s`
- shell `&&` 短路执行
- 命名管道 FIFO：`mkfifo`
- 文件权限与用户/组：`chmod` / `chown` / `setuid` / `setgid` / `umask`
- 需求分页与 `mmap`：支持 `MAP_PRIVATE` / `MAP_SHARED`、部分 `munmap` 与共享写回
- 多磁盘支持：第二块 virtio 磁盘以 `/disk1` 挂载，可读可写
- VFS 挂载表：`mount` / `umount` 支持把第二磁盘挂到任意目录，`/disk1` 为默认挂载点，同一设备同一时刻一个挂载点
- 双重间接块：单文件上限从 268KB 扩展到约 64MB
- 交换空间：第三块原始交换盘，内存不足时换出、缺页自动换入，`swapinfo` 查看统计
- ASLR：`exec` 随机化用户栈起始位置，增强地址空间布局随机性
- `clone` 轻量线程：共享父进程地址空间，使用独立用户栈、trapframe 与内核栈
- `futex` 与 `thread_create`：用户态互斥同步、线程入口 stub 与 `gettid`
- 内核线程：`kthread_create` 复用进程表与 MLFQ 调度器，内核态执行后自动退出
- 线程组语义：`getpid()` 返回 tgid，`gettid()` 返回 tid，clone 线程共享 tgid
- clone 共享文件表与 cwd：子线程 close/chdir 对同组线程可见
- 线程组退出：组长退出或按 tgid kill 时终止并回收同组线程
- `waitpid`：按 tid 精确等待并回收指定 clone 线程
- TLS：每个线程独立的 `tp` 指针，`set_tls` / `get_tls`
- `tgkill`：精确向线程组内指定 tid 发送终止信号
- 线程组信号：clone 线程共享信号处理表，`tgkill` 精确投递，`sigprocmask` 支持信号阻塞
- clone 共享 VMA 表：`mmap` / `munmap` 与需求分页页面对同组线程可见

## 目录结构

```text
kernel/
  module/         模块基础设施
  modules/        静态与动态示例模块
  ...             内核核心源码
user/
  tests/           拆分后的用户态测试套件
  modules/        自动扫描的用户模块
  ...             用户程序与用户库
mkfs/
  mkfs.c          文件系统镜像构建工具
test-xv6.py       QEMU 自动化测试脚本
```

## 构建与运行

需要 RISC-V newlib 工具链和 `qemu-system-riscv64` 7.2 以上版本。

```bash
make qemu
```

其他常用命令：

```bash
make kernel/kernel      # 只构建内核
make fs.img             # 构建文件系统镜像
make qemu-gdb           # 使用 GDB 调试
make clean              # 清理构建产物
```

自动化测试：

```bash
make test-quick     # host checks + usertests -q + tools + grind + modules
make test           # 默认稳定测试入口
make test-all       # test + crash
./test-xv6.py -q usertests
./test-xv6.py usertests
./test-xv6.py tools
./test-xv6.py modules
./test-xv6.py crash
```

## 使用示例

进入 xv6 shell 后：

```text
$ procinfo
pid 1 sleep prio 50 init
pid 2 sleep prio 50 sh

$ prio 2 10
setpriority(2, 10) = 0

$ strace echo hi
strace: syscalls=7 forks=1 exits=1 ticks=0 status=0
  syscall 1 (fork): 1
  syscall 2 (exit): 1
  syscall 3 (wait): 1
  syscall 7 (exec): 1
  syscall 16 (write): 142
  syscall 22 (module_call): 26

$ perf echo hi
perf: syscalls=7 ticks=0 forks=1 exits=1 free_pages=32529

$ cat /dev/sysinfo
processes 3
free_pages 32533
ticks 113

$ cat /dev/stats
syscalls 387
forks 14
exits 12
ticks 352
  1 fork 14
  2 exit 12
  16 write 387

$ ln -s README.md link
$ cat link
xv6 is a re-implementation...

$ echo one && echo two
one
two

$ mkfifo fifo
$ cat fifo &
$ echo hello > fifo
hello

$ id
uid=0 gid=0 euid=0 egid=0

$ swapinfo
swap total 2048 free 2048 swapouts 0 swapins 0

$ aslr
aslr stack 3ffffff8e0

$ echo hi > permfile
$ chmod 600 permfile
$ chown 1 1 permfile

$ ls /disk1
.              1 1 1024
..             1 1 1024
README.md      2 2 5696
echo           2 3 37456

$ cat /disk1/README.md
# xv6-riscv

$ echo disk1 > /disk1/newfile
$ cat /disk1/newfile
disk1
$ rm /disk1/newfile

$ mkdir /mnt
$ mount 2 /mnt
mount(2, /mnt) = 0
$ ls /mnt
.
..
README.md
echo
$ umount /mnt
umount(/mnt) = 0

$ crashdump
=== kernel crash dump ===
pid=3 name=crashdump
backtrace:
  8000689a

$ shmtest
shared=SHM

$ cowtest
parent=before
COW OK

$ signaltest
handler sig=10
signaltest: after

$ ps
processes 3
free_pages 32490
ticks 60
pid 1 sleep prio 50 q 0 init
```

动态模块示例：

```text
$ modload dynmod
dynmod loaded
module_load = 0

$ modcli 3 1
module_call(3, 1) = 4660

$ modunload 0
dynmod unloaded
module_unload = 0
```

## 模块拓展方式

### 静态内核模块

在 `kernel/modules/` 新建 C 文件：

```c
#include "module.h"
#include "module_ids.h"

static uint64
foo_handler(int cmd, uint64 arg0, uint64 arg1)
{
  return 0;
}

KMOD_SYSREG(KMOD_FOO, foo, foo_handler);
```

在 `kernel/module/module_ids.h` 登记模块 ID 后重新构建即可。

### 用户模块

在 `user/modules/` 新建 C 文件，`Makefile` 会自动生成 `_程序名` 并加入
`fs.img`。

### 动态模块

动态模块被链接到固定地址 `DYNMOD_BASE`，通过 `struct kmod_api *` 访问内核
功能，由 `modload` 加载、`modunload` 卸载。

当前动态模块支持多个槽位的固定地址 ELF 加载，并支持基础 ELF 重定位；
仍不解析内核符号。

## 文档

- [xv6-riscv-source-analysis.md](docs/xv6-riscv-source-analysis.md)：源码分析
- [xv6-riscv-extension-ideas.md](docs/xv6-riscv-extension-ideas.md)：扩展思路
- [xv6-riscv-module-architecture.md](docs/xv6-riscv-module-architecture.md)：模块架构
- [xv6-riscv-module-priorities.md](docs/xv6-riscv-module-priorities.md)：模块方向与优先级
- [xv6-riscv-module-refinement.md](docs/xv6-riscv-module-refinement.md)：现有模块完善批次
- [xv6-riscv-testing.md](docs/xv6-riscv-testing.md)：测试架构与指南
- [AGENTS.md](AGENTS.md)：贡献者指南

## 致谢

xv6 源自 MIT 6.1810 操作系统课程，参考了 John Lions 对 UNIX 6th Edition
的经典注释。原始贡献者包括 Russ Cox、Cliff Frey、Xiao Yu、Nickolai
Zeldovich、Austin Clements，以及众多提交补丁和报告问题的开发者。

## License

本仓库遵循原 xv6 的 MIT License，详见 [LICENSE](LICENSE)。
