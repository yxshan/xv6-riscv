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

状态：已完成

完成内容：

- MLFQ 周期性提升时按优先级重新计算队列，而不是无差别重置到队列 0。
- 信号支持 `SIG_DFL` / `SIG_IGN`，处理期间防重入，`sigreturn` 校验活动状态。
- 动态模块增加卸载回调和加载失败回滚。
- 新增测试：`prio_boost`、`signal_no_reenter`、`signal_ignore`、`signal_default`、`dynmod_lifecycle`。

验证命令：

```bash
make kernel/kernel user/_usertests dynmod fs.img
usertests signal_no_reenter
usertests signal_ignore
usertests signal_default
usertests dynmod_lifecycle
usertests prio_boost
usertests -q
```

## P2 批次一：文件权限与用户/组

状态：已完成

完成内容：

- 磁盘 inode 增加 `mode`、`uid`、`gid` 字段，并用填充保持
  `sizeof(struct dinode) == 128`，使每个块仍可整除。
- `struct stat` 同步返回权限与所有者信息。
- `struct proc` 增加 `uid/euid/gid/egid/umask`，fork 时继承。
- 新增 `iaccess()` 权限检查：owner/group/other 三类权限位，
  root 按教学简化直接绕过读/写/执行检查。
- 检查点覆盖：`open` 读写、`exec` 执行、`chdir` 执行、
  路径遍历搜索、创建/删除/硬链接时的父目录写权限。
- 新增系统调用：`chmod`、`chown`、`getuid`、`geteuid`、
  `getgid`、`getegid`、`setuid`、`setgid`、`umask`。
- 新增用户工具：`id`、`chmod`、`chown`，以及测试用可执行文件
  `permexec`。
- 新增 `user/tests/perm_tests.c` 权限与凭证测试套件。

涉及文件：

- `kernel/fs.h`、`kernel/fs.c`、`kernel/file.h`、`kernel/stat.h`
- `kernel/proc.h`、`kernel/proc.c`
- `kernel/sysfile.c`、`kernel/sysproc.c`、`kernel/exec.c`
- `mkfs/mkfs.c`
- `user/id.c`、`user/chmod.c`、`user/chown.c`、`user/permexec.c`
- `user/tests/perm_tests.c`

验证命令：

```bash
make kernel/kernel user/_usertests user/_id user/_chmod user/_chown user/_permexec fs.img
usertests perm_credentials
usertests perm_basic
usertests perm_classes
usertests perm_dir
usertests perm_create_existing
usertests perm_exec
make test-quick
```

## P2 批次二：需求分页与 mmap

状态：已完成

完成内容：

- 在 `struct proc` 上增加固定数组 VMA 表，支持文件映射和匿名映射。
- 新增 `mmap` / `munmap` 系统调用，映射区域位于共享内存区域下方，
  从高地址向下分配。
- 缺页路径优先检查 VMA：文件映射按需调用 `readi()` 读取页面，
  匿名映射补零页，再按 `prot` 建立页表权限。
- fork 复制 VMA 描述符和已驻留页面，子进程保持私有副本语义。
- exec/exit/munmap 时解除 VMA 映射、释放物理页和 inode 引用。
- `copyinstr()` 也支持对惰性页面和 mmap 页面按需触发缺页。
- 堆的上限调整为 `MMAP_BASE`，避免 sbrk 与 mmap 区域重叠。
- 新增 `user/tests/mmap_tests.c`：文件/偏移映射、匿名映射、fork、
  非法参数和 munmap 后访问杀死进程。

涉及文件：

- `kernel/vma.h`、`kernel/vma.c`
- `kernel/proc.h`、`kernel/proc.c`
- `kernel/vm.c`、`kernel/exec.c`
- `kernel/sysfile.c`、`kernel/sysproc.c`
- `kernel/memlayout.h`、`kernel/fcntl.h`
- `user/tests/mmap_tests.c`

验证命令：

```bash
make kernel/kernel user/_usertests fs.img
usertests mmap_file_demand
usertests mmap_anonymous
usertests mmap_fork
usertests mmap_badargs
usertests mmap_after_unmap
make test-quick
```

## P2 批次三：动态模块 ELF 化

状态：已完成

完成内容：

- `dynmod` 从 `objcopy -O binary` 原始格式改为 ELF 可执行文件。
- 内核动态模块加载器解析 ELF 头，校验 `ET_EXEC`、RISC-V 架构和入口地址。
- 按程序头复制 LOAD 段到 `DYNMOD_BASE`，并对 BSS 区域清零。
- 加载失败时完整回滚，不会影响后续重新加载。
- 新增 `dynmod_badelf` 测试，验证非 ELF 文件会被拒绝。

涉及文件：

- `Makefile`
- `kernel/module/module.c`
- `kernel/modules/dynmod_sample.c`
- `kernel/module/dynmod.h`
- `user/modload.c`
- `user/tests/module_tests.c`

验证命令：

```bash
make kernel/kernel dynmod fs.img
usertests dynmod_lifecycle
usertests dynmod_badelf
modload dynmod
modcli 3 1
modunload
make test-quick
```

## P2 批次四：多磁盘支持

状态：已完成

完成内容：

- virtio 驱动从单盘改为 `disks[NDISK]`，初始化两块 MMIO 磁盘。
- PLIC 与 `devintr` 支持第二块磁盘中断。
- 文件系统使用 `sb[dev]` 每设备超级块，根磁盘仍负责日志与回收。
- `/disk1` 作为第二磁盘的挂载前缀，支持 `ls`、`cat`、`exec`。
- 第二磁盘支持创建、删除、修改和打开写。
- `Makefile` 增加 `fs2.img` 和第二个 `-drive`，QEMU 双盘启动。
- `test-xv6.py` 增加 `/disk1` 工具测试，并改为终止整个 QEMU 进程组。
- 新增 `disk1_read` 用户态回归测试。

涉及文件：

- `kernel/param.h`、`kernel/memlayout.h`、`kernel/vm.c`
- `kernel/virtio_disk.c`、`kernel/plic.c`、`kernel/trap.c`
- `kernel/fs.c`、`kernel/sysfile.c`、`kernel/proc.c`
- `Makefile`、`test-xv6.py`
- `user/tests/fs_tests.c`

验证命令：

```bash
make kernel/kernel fs.img fs2.img
usertests disk1_read
ls /disk1
cat /disk1/README.md
/disk1/echo disk1-ok
echo disk1 > /disk1/newfile
cat /disk1/newfile
rm /disk1/newfile
make test-quick
./test-xv6.py crash
```

## 完善批次一：mmap 语义补全

状态：已完成

完成内容：

- 支持 `MAP_SHARED` 文件映射与匿名共享映射。
- 共享页使用 `PTE_SHM|PTE_COW` 作为引用计数标记，`munmap`/退出时安全释放。
- `munmap` 支持页对齐的部分解除，VMA 可拆分为左右两段。
- 共享文件映射在 `munmap` 和进程清理时把已驻留可写页写回文件。
- fork 时私有映射复制页面，共享映射继续映射同一物理页。
- 新增测试：共享文件写回、共享匿名 fork、部分 `munmap`、部分共享写回。
- `test-xv6.py` 改为直接启动 QEMU，并注册退出清理，避免残留 QEMU 占满进程表。

涉及文件：

- `kernel/vma.c`、`kernel/vm.c`
- `kernel/modules/cow.c`
- `kernel/sysfile.c`
- `test-xv6.py`
- `user/tests/mmap_tests.c`

验证命令：

```bash
make kernel/kernel user/_usertests fs.img
usertests mmap_shared_file
usertests mmap_shared_fork
usertests mmap_munmap_partial
usertests mmap_shared_partial
make test-quick
```

## 完善批次二：动态模块多实例

状态：已完成

完成内容：

- 动态模块区域扩展为 4 个槽位，每个槽位独立清空、加载和卸载。
- `module_load` 返回槽位号，`module_unload(slot)` 按槽位卸载。
- 每个槽位独立保存退出回调，卸载时只移除本槽位注册的 `module_call` 处理器。
- 新增第二个动态模块 `dynmod2`，模块 ID 为 `KMOD_DYN_TWO`。
- 新增 `dynmod_multi` 测试，验证两个模块同时驻留并分别卸载。
- 更新 `modunload` 工具支持槽位参数，默认卸载槽位 0。

涉及文件：

- `kernel/memlayout.h`、`kernel/kalloc.c`、`kernel/vm.c`
- `kernel/module/module.h`、`kernel/module/module.c`
- `kernel/module/module_ids.h`
- `kernel/modules/dynmod_two.c`
- `Makefile`
- `user/modunload.c`
- `user/tests/module_tests.c`
- `test-xv6.py`

验证命令：

```bash
make kernel/kernel dynmod dynmod2 fs.img
usertests dynmod_lifecycle
usertests dynmod_multi
modload dynmod
modload dynmod2
modcli 3 1
modcli 5 1
modunload 0
modunload 1
```

## 完善批次三：第二磁盘可写挂载

状态：已完成

完成内容：

- 日志层重构为全局事务 + 每设备日志，第二磁盘拥有独立日志区。
- `fsinit()` 为所有磁盘初始化日志并执行孤立 inode 回收。
- 移除 `/disk1` 的只读限制，支持创建、写入、删除、修改和 `exec`。
- `disk1_read` 测试扩展为第二磁盘读写往返与删除验证。
- `test-xv6.py tools` 使用唯一标记验证第二磁盘写入。

涉及文件：

- `kernel/log.c`
- `kernel/fs.c`
- `kernel/sysfile.c`
- `test-xv6.py`
- `user/tests/fs_tests.c`

验证命令：

```bash
make kernel/kernel fs.img fs2.img
usertests disk1_read
echo disk1 > /disk1/newfile
cat /disk1/newfile
rm /disk1/newfile
make test-quick
```

## 完善批次四：动态模块 ELF 重定位

状态：已完成

完成内容：

- ELF 头新增 section header、symbol、RELA 结构。
- 动态模块链接时使用 `-q` 保留重定位信息。
- 加载器支持 `R_RISCV_64`、`R_RISCV_32`、`R_RISCV_RELATIVE`。
- PC-relative、RVC 分支和 RELAX 重定位在整块搬移后仍然有效，直接跳过。
- 动态模块槽位扩到 32KB，`modload` 与测试上限同步扩大。
- `dynmod2` 增加初始化数据指针，用于验证搬移到槽位 1 后重定位正确。
- `test-xv6.py modules` 增加 `modcli 5 2` 检查。

涉及文件：

- `kernel/elf.h`
- `kernel/memlayout.h`
- `kernel/module/module.c`
- `kernel/modules/dynmod_two.c`
- `Makefile`
- `user/modload.c`
- `user/tests/module_tests.c`
- `test-xv6.py`

验证命令：

```bash
make TOOLPREFIX=riscv64-elf- kernel/kernel dynmod dynmod2 fs.img
usertests dynmod_multi
modload dynmod
modload dynmod2
modcli 5 2
modunload 0
modunload 1
```

## P3 批次一：VFS 挂载表与双重间接块

状态：已完成

完成内容：

- 新增独立 VFS 挂载层 `kernel/mount.c`，挂载表按设备、挂载点和路径登记。
- 路径解析经过挂载点时切换到被挂载文件系统的根；挂载根执行 `..` 会跨回父文件系统。
- 新增 `mount` / `umount` 系统调用、用户库桩和命令行工具。
- `mkfs` 只在根镜像创建 `/disk1` 目录，内核启动时把第二磁盘默认挂载到该目录。
- 卸载按挂载路径精确匹配；同一设备同一时刻只允许一个挂载点，避免共享根 inode 的 `..` 语义歧义。
- 磁盘 inode 增加二级间接块地址，单文件上限从 268KB 扩展到约 64MB。
- `bmap`、`itrunc` 和 `mkfs` 同步支持二级间接块分配与释放。
- 新增 `mount_basic`、`mount_dotdot`、`dindirect` 回归测试。
- `test-xv6.py` 增加挂载工具测试，并修复旧输出重复匹配与镜像缓存污染问题。

涉及文件：

- `kernel/mount.c`、`kernel/defs.h`
- `kernel/fs.c`、`kernel/fs.h`、`kernel/file.h`
- `kernel/syscall.c`、`kernel/syscall.h`、`kernel/syscall_names.h`
- `kernel/main.c`、`kernel/proc.c`
- `mkfs/mkfs.c`、`Makefile`
- `user/mount.c`、`user/umount.c`、`user/user.h`、`user/usys.pl`
- `user/tests/fs_tests.c`、`test-xv6.py`

验证命令：

```bash
make kernel/kernel fs.img fs2.img
usertests mount_basic
usertests mount_dotdot
usertests dindirect
mkdir /mnt
mount 2 /mnt
ls /mnt
echo MNT-OK > /mnt/mntfile
cat /mnt/mntfile
umount /mnt
make test-quick
```

## P3 批次二：交换空间

状态：已完成

完成内容：

- 新增第三块原始 virtio 交换盘 `swap.img`，不承载文件系统。
- `swap.c` 管理 2048 个交换槽位，每页由 4 个 1KB 磁盘块组成。
- 换出页使用两个 RSW 位组合作为 PTE 交换标记（`PTE_V=0` 且
  `PTE_SHM|PTE_COW` 同时置位），PPN 字段记录交换槽号。
- 原始页权限保存在交换槽元数据中，换入时恢复 R/W/X/U。
- `swap_evict()` 在物理内存不足时从当前进程换出最高地址用户页，
  COW 页先私有化再换出。
- `vmfault()` 识别交换标记后从交换盘读回，并归还交换槽。
- `uvmunmap()` / `uvmfree()` 释放换出页时自动回收交换槽。
- fork 遇到换出页时读入一份独立物理副本，父进程保持换出状态。
- 新增 `swapout` / `swapinfo` 系统调用、`swapinfo` 工具和 `swap_basic` 回归测试。
- 内核 BSS 增长后，动态模块保留区上移到 `0x80080000`，避免模块加载覆盖内核数据。

涉及文件：

- `kernel/swap.c`、`kernel/swap.h`、`kernel/defs.h`
- `kernel/vm.c`、`kernel/riscv.h`、`kernel/memlayout.h`
- `kernel/param.h`、`kernel/plic.c`、`kernel/trap.c`
- `kernel/syscall.c`、`kernel/syscall.h`、`kernel/syscall_names.h`
- `kernel/main.c`、`Makefile`、`test-xv6.py`
- `user/swapinfo.c`、`user/user.h`、`user/usys.pl`
- `user/tests/mem_tests.c`

验证命令：

```bash
make kernel/kernel fs.img fs2.img swap.img
usertests swap_basic
swapinfo
make test-quick
```

## P3 批次三：ASLR

状态：已完成

完成内容：

- `exec` 在分配用户栈前生成随机页偏移，栈起始位置在 0 到 16 页之间变化。
- 使用 `r_time()`、进程号和线性同余生成器初始化随机种子。
- 保留 mmap 与堆的固定布局，避免破坏现有需求分页和惰性分配测试。
- 新增 `aslr` 用户工具，打印当前用户栈地址。
- `test-xv6.py tools` 连续运行两次 `aslr`，确认地址不同。

涉及文件：

- `kernel/exec.c`
- `user/aslr.c`
- `Makefile`、`test-xv6.py`

验证命令：

```bash
make kernel/kernel fs.img
aslr
aslr
make test-quick
```

## P3 批次四：clone 轻量线程

状态：已完成（基础版，共享地址空间）

完成内容：

- 新增 `clone` 系统调用，子线程返回 0 并使用调用者提供的新用户栈。
- `uvmshare()` 为 clone 子线程建立独立页表根，但叶页映射父进程同一物理页。
- clone 共享页使用 `PTE_SHM|PTE_COW` 作为引用计数标记，最后一个映射释放时回收物理页。
- COW 页面在 clone 前先私有化，避免线程写入穿透到 fork 父进程。
- System V 共享内存仍使用原有 `seg->ref` 计数，共享 mmap 继续使用 cow 引用计数。
- 子线程复制文件描述符、cwd 和进程凭证，共享地址空间但不共享 VMA 描述符。
- 新增 `clone_basic` 回归测试，验证共享变量可见且无物理页泄漏。

涉及文件：

- `kernel/proc.c`、`kernel/proc.h`（无布局变化）、`kernel/vm.c`
- `kernel/sysproc.c`、`kernel/syscall.c`、`kernel/syscall.h`
- `kernel/syscall_names.h`、`kernel/defs.h`
- `user/user.h`、`user/usys.pl`、`user/tests/proc_tests.c`

验证命令：

```bash
make kernel/kernel user/_usertests fs.img
usertests clone_basic
make test-quick
```

## P3 批次五：clone 生态（futex / thread_create）

状态：已完成

完成内容：

- 新增 `futex_wait` / `futex_wake` 系统调用，以共享内存地址为等待通道。
- 新增 `gettid` 系统调用，返回当前线程/进程 pid。
- 新增用户库 `thread_create(fn, arg, stack)`，通过 `clone_stub` 启动线程。
- `clone_stub` 是用户态汇编入口，线程从 stub 调用 `fn(arg)` 后自动 `exit(0)`，
  不依赖父进程栈上的局部变量。
- 新增 `clone_sync` 测试：两个线程使用 RISC-V `amoswap` 自旋锁 + futex 睡眠，
  精确累加共享计数器。

涉及文件：

- `kernel/futex.c`、`kernel/defs.h`、`kernel/main.c`
- `kernel/sysproc.c`、`kernel/syscall.c`、`kernel/syscall.h`
- `kernel/syscall_names.h`
- `user/clone_stub.S`、`user/ulib.c`、`user/user.h`、`user/usys.pl`
- `user/tests/proc_tests.c`、`Makefile`

验证命令：

```bash
make kernel/kernel user/_usertests fs.img
usertests clone_sync
make test-quick
```

## 文档入口

- 模块架构：[xv6-riscv-module-architecture.md](xv6-riscv-module-architecture.md)
- 模块优先级：[xv6-riscv-module-priorities.md](xv6-riscv-module-priorities.md)
- 项目主页：[../README.md](../README.md)
