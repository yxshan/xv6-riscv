# xv6-riscv 测试指南

## 1. 测试分层

当前测试分为四层：

1. 用户态回归套件：`user/tests/`
2. 独立测试程序：如 `cowtest`、`shmtest`、`signaltest`
3. QEMU 主机自动化：`test-xv6.py`
4. 崩溃恢复测试：`./test-xv6.py crash`
5. 内核自测模块：`kernel/modules/selftest.c`

## 2. 用户态回归套件

`usertests` 不再是一个大文件，而是按子系统拆分的多个源文件：

```text
user/tests/
  tests.h            # 公共头文件与 struct test
  usertests.c        # 驱动：参数解析、quick/slow 执行
  syscall_tests.c    # 参数安全与非法指针
  mem_tests.c        # sbrk、惰性分配、内存边界
  fs_tests.c         # 文件、目录、日志、inode
  proc_tests.c       # fork、wait、exec、进程状态
  module_tests.c     # FIFO、COW、stats、/proc 等内核模块功能
  perm_tests.c       # 文件权限、用户/组凭证与 exec 权限
  mmap_tests.c       # mmap 私有映射与需求分页
```

每个测试文件提供两个测试数组：

```c
struct test mem_quicktests[] = {
  {sbrkbasic, "sbrkbasic"},
  {lazy_sbrk, "lazy_sbrk"},
  { 0, 0},
};

struct test mem_slowtests[] = {
  { 0, 0},
};
```

驱动 [user/tests/usertests.c](../user/tests/usertests.c) 按套件顺序执行，
保留原有命令：

```text
usertests            # 全部测试
usertests -q         # 快速测试
usertests -c         # 连续模式
usertests -C         # 失败后继续
usertests linktest   # 只运行指定测试
```

## 3. 新增用户态测试

1. 在对应子系统的 `user/tests/*_tests.c` 中新增 `void foo(char *s)`。
2. 把 `{foo, "foo"}` 加入该文件的 `quicktests` 或 `slowtests` 数组。
3. 重新构建：

```bash
make user/_usertests
make fs.img
```

4. 在 QEMU 中验证：

```text
usertests foo
usertests -q
```

如果新测试不属于现有分类，可以新增 `user/tests/xxx_tests.c`，
然后在 [tests.h](../user/tests/tests.h) 和
[usertests.c](../user/tests/usertests.c) 的 `suites[]` 中登记。

## 4. 独立测试程序

适合单个模块的端到端验证，例如：

- `cowtest`：写时复制 fork
- `shmtest`：共享内存
- `signaltest`：信号处理
- `crashdump`：主动崩溃转储
- `modload` / `modunload`：动态模块

独立程序放在 `user/` 下，加入 `UPROGS` 后即可进入 `fs.img`。
它们由 QEMU 中的 shell 或 `test-xv6.py` 调用。

## 5. QEMU 主机自动化

`test-xv6.py` 负责启动 QEMU、发送命令并匹配输出：

```bash
./test-xv6.py usertests        # 完整用户态回归
./test-xv6.py -q usertests     # 快速回归
./test-xv6.py tools            # 独立工具、shell 特性与可观测工具
./test-xv6.py grind            # 随机 syscall 压力测试
./test-xv6.py modules          # 动态模块加载/调用/卸载
./test-xv6.py crash            # 崩溃恢复测试
```

第一个参数同时支持正则匹配脚本中的 `test_*` 函数。
新增 orchestrated 测试时，在 `test-xv6.py` 中添加：

```python
def test_cow():
    q = QEMU(True)
    q.cmd("cowtest\n")
    q.monitor("^COW OK")
    q.stop()
```

也可以直接使用聚合入口：

```bash
make test-quick   # host checks + 构建 + usertests -q + tools + grind + modules
make test         # 与 test-quick 相同，作为默认稳定入口
make test-all     # test + crash
```

## 6. CI

仓库包含 [.github/workflows/xv6.yml](../.github/workflows/xv6.yml)，
每次 `push` 或 `pull_request` 会在 Ubuntu 上安装 RISC-V 交叉工具链和 QEMU，
依次运行：

```bash
python3 tools/check-tests.py
make kernel/kernel fs.img
./test-xv6.py -q usertests
./test-xv6.py tools
./test-xv6.py grind
./test-xv6.py modules
./test-xv6.py crash
```

CI 使用 `cpus: [1, 3]` 矩阵，并在失败时上传 `test-xv6.out`。

## 7. 进程资源管理

为避免 QEMU 和 `make` 残留进程占满系统资源：

- `test-xv6.py` 每次运行只构建一次内核与文件系统镜像。
- 干净镜像缓存为 `fs.img.clean` / `fs2.img.clean`，crash 多次重启直接复制。
- QEMU 直接由 Python 启动并放入独立进程组，`stop()` / `crash()` 会终止整个进程组。
- 脚本注册 SIGINT/SIGTERM 处理，中断时强制清理活动 QEMU。
- 手工清理残留 QEMU：

```bash
make kill-qemu
```

## 8. 测试约定

- 每个测试函数在一个独立子进程中运行，失败时以非 0 状态退出。
- 测试输出统一为 `test <name>: OK / FAILED`。
- 允许故意触发内核 `usertrap` 错误信息，只要测试最终通过即可。
- 涉及文件系统状态的测试应先清理旧文件，避免污染其他用例。
- 提交前至少运行 `usertests -q`；涉及崩溃恢复时再运行 `./test-xv6.py crash`。

批次2新增回归用例：

- `stats_reset`：验证 `/dev/stats` 写入后重置统计。
- `proc_chunked`：验证 `/proc` 使用小缓冲多次读取。
- `procinfo_full`：创建多个子进程后验证进程列表不截断。

批次3新增回归用例：

- `signal_no_reenter`：信号处理期间不重入，新信号在 `sigreturn` 后交付。
- `signal_ignore`：`SIG_IGN` 不终止进程。
- `signal_default`：`SIG_DFL` 默认终止进程。
- `dynmod_lifecycle`：动态模块加载后能执行卸载回调。
- `prio_boost`：周期性提升后按优先级保持目标队列（慢测试）。

P2 批次一新增回归用例：

- `perm_credentials`：uid/gid/euid/egid 读取、`setuid`/`setgid` 与 `umask`。
- `perm_basic`：默认权限、`chmod`、非 root 的读写拒绝与 root 绕过。
- `perm_classes`：owner/group/other 三类权限匹配。
- `perm_dir`：目录执行权限影响路径解析与目录内创建。
- `perm_create_existing`：`O_CREATE` 打开已存在文件不要求父目录写权限。
- `perm_exec`：非 root 无执行位时 `exec` 失败，有执行位时成功。

工具测试新增 `id`，用于确认 shell 进程的 root 凭证输出。

P2 批次二新增回归用例：

- `mmap_file_demand`：文件映射、偏移映射、私有写不落盘。
- `mmap_anonymous`：匿名映射补零和 `munmap`。
- `mmap_fork`：fork 复制映射及私有写隔离。
- `mmap_badargs`：非法地址、长度、共享映射和设备 fd 被拒绝。
- `mmap_after_unmap`：`munmap` 后继续访问映射地址会被杀死。
- `mmap_shared_file`：共享映射写回文件。
- `mmap_shared_fork`：fork 后共享匿名页保持同一物理页。
- `mmap_munmap_partial`：部分 `munmap` 后两侧仍可访问，中间访问被杀。
- `mmap_shared_partial`：部分解除共享映射时只写回被解除的页。

P2 批次三新增回归用例：

- `dynmod_badelf`：非 ELF 动态模块文件应被拒绝，加载失败可安全回滚。
- `dynmod_multi`：多个动态模块可同时驻留，并按槽位分别卸载。
- `modcli 5 2` 验证带初始化数据指针的动态模块重定位结果。

P2 批次四新增回归用例：

- `disk1_read`：第二磁盘 `/disk1` 可读、可执行，并可创建、写入和删除文件。

工具测试新增：

- `ls /disk1` 与 `cat /disk1/README.md` 验证第二磁盘挂载。
- `echo WROTE-OK > /disk1/newfile` 验证第二磁盘写操作。

P3 批次一新增回归用例：

- `mount_basic`：把第二磁盘挂到任意目录，写入、读取、卸载并确认挂载树消失。
- `mount_dotdot`：挂载根目录执行 `..` 时跨回父文件系统，而不是停在挂载根。
- `dindirect`：写入并读回超过单间接块上限的文件，覆盖双重间接块和截断释放。

工具测试新增：

- `mkdir /mnt`、`mount 2 /mnt`、`ls /mnt`、`echo MNT-OK > /mnt/mntfile`、`cat /mnt/mntfile`、`umount /mnt`。

P3 批次二新增回归用例：

- `swap_basic`：强制换出一页后通过缺页换入，验证数据完整和 `swapouts` / `swapins` 计数。

P3 批次四新增回归用例：

- `clone_basic`：`clone` 子线程写入共享变量，父进程等待后应看到更新且无物理页泄漏。

P3 批次五新增回归用例：

- `clone_sync`：两个 `thread_create` 线程通过 futex 互斥锁累加计数器，结果必须精确为 1000。

工具测试新增：

- `swapinfo`：显示交换盘总页数、空闲页数和换入换出计数。
- `aslr`：连续两次运行应打印不同的用户栈地址。

内核自测：

- `SELFTEST_CMD_BASIC`：进程状态、MLFQ 队列、优先级和内存可用性。
- `SELFTEST_CMD_REGISTRY`：模块 ID 唯一性和处理器完整性。
- `SELFTEST_CMD_SIGNAL`：当前进程信号活动状态。
- `SELFTEST_CMD_MEMORY`：COW 引用计数与共享内存段不变量。

## 9. 后续可扩展方向

- 宿主机构建单元测试：覆盖无硬件依赖的纯逻辑。
- 随机 syscall 压力测试：结合 QEMU 超时和崩溃检测。
- 更多 CI 矩阵：多工具链、多 QEMU 版本。

## 10. 相关文档

- [xv6-riscv-module-refinement.md](xv6-riscv-module-refinement.md)
- [xv6-riscv-module-architecture.md](xv6-riscv-module-architecture.md)
- [../README.md](../README.md)
