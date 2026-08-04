# xv6-riscv 现有模块完善批次

> 目标：不新增功能方向，而是把已经实现的模块补完整：
> 修复边界条件、补齐错误路径、增强测试覆盖，并同步更新文档。

## 批次一：FIFO 与 COW 基础完善

状态：已完成

### FIFO O_RDWR 端计数

修复前 `sys_open()` 只根据 `O_WRONLY` 判断 FIFO 端类型，
`O_RDWR` 打开时只增加读端、不增加写端。
现在 `O_RDWR` 会同时登记读端和写端，读写阻塞语义保持一致。

涉及文件：

- `kernel/sysfile.c`

### COW fork 错误路径

修复前 `uvmcopy()` 把可写页转为 COW 后，如果子进程映射失败，
只释放一次新增引用，且父进程页表不会恢复原样，可能泄漏物理页引用。
现在失败时恢复父进程 PTE，并精确释放两次新增引用。

涉及文件：

- `kernel/vm.c`

### COW 非法地址校验

修复前 `cow_handle()` 对大于 `MAXVA` 的写地址直接调用 `walk()`，
`MAXVAplus` 用例会触发 `panic: walk`。现在先在 COW 缺页路径校验地址。

涉及文件：

- `kernel/modules/cow.c`

### 路径解析自引用目录死锁

修复前 `namex()` 在锁住父目录后又锁子目录，解析 `.` / `..` 时
会试图重复锁同一个 inode，导致 `ln . x` 和 `usertests linktest` 卡死。
现在查找子目录前先释放父目录锁，再锁子目录。

涉及文件：

- `kernel/fs.c`

### 惰性 sbrk 与共享内存边界

修复前惰性 `sbrk` 只限制到 `TRAPFRAME`，普通 `sbrk` 却限制到
`SHM_BASE`，两者上限不一致，堆可能侵入共享内存区域。
现在惰性分配也限制到 `SHM_BASE`，并把 `usertests lazy_sbrk`
的预期值同步为 `SHM_BASE - PGSIZE`。

涉及文件：

- `kernel/sysproc.c`
- `user/tests/mem_tests.c`

### 测试与验证

新增 `usertests` 用例：

- `fifo_rdwr`：验证 FIFO `O_RDWR` 可先写后读，并能唤醒阻塞读端。
- `cowfork`：多轮 fork，子进程写多页，验证父进程数据不被污染。

同时把 `usertests` 中遗留的 `README` 引用更新为 `README.md`。

验证命令：

```bash
make kernel/kernel
make fs.img
make qemu
usertests fifo_rdwr
usertests cowfork
usertests linktest
usertests lazy_sbrk
usertests -q
```

## 批次二：可观测性与伪文件系统

状态：已完成

完成内容：

- `strace` 按 `syscall.h` 全量统计，并输出系统调用名。
- `/dev/stats` 支持通过设备文件写入重置计数，并输出逐系统调用明细。
- `/proc` 使用每打开文件独立的缓冲和偏移，支持小缓冲多次读取。
- `procinfo` 与 `procinfo_full` 测试覆盖进程多时输出不截断。
- 新增 `kernel/syscall_names.h` 共享系统调用名称表。
- 新增 `kernel/modules/proc_common.[ch]`，统一 `sysinfo` 与 `procfs` 格式化。

验证命令：

```bash
make kernel/kernel user/_usertests user/_strace user/_procinfo user/_ps fs.img
usertests stats_reset
usertests proc_chunked
usertests procinfo_full
usertests -q
```

## 批次三：调度、信号与动态模块

状态：待完成

计划内容：

- MLFQ 周期性提升时按优先级重新计算队列，而不是无差别重置到队列 0。
- 信号补充处理期间防重入、默认动作与 `sigreturn` 校验。
- 动态模块增加卸载回调与加载失败回滚，完善生命周期。

## 文档入口

- 模块架构：[xv6-riscv-module-architecture.md](xv6-riscv-module-architecture.md)
- 模块优先级：[xv6-riscv-module-priorities.md](xv6-riscv-module-priorities.md)
- 项目主页：[../README.md](../README.md)
