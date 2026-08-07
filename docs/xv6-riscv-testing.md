# xv6-riscv 测试指南

## 1. 测试分层

当前测试分为四层：

1. 用户态回归套件：`user/tests/`
2. 独立测试程序：如 `cowtest`、`shmtest`、`signaltest`
3. QEMU 主机自动化：`test-xv6.py`
4. 崩溃恢复测试：`./test-xv6.py crash`
5. 内核自测模块：`kernel/modules/selftest.c`

自动化稳定性约定：

- `test-xv6.py` 的 QEMU 输出读取为非阻塞，`monitor` 超时不会被无输出卡死。
- `usertests` 驱动为每个测试增加 300 tick 的单测超时；超时后终止该测试并标记
  `TIMEOUT`，避免单个用例挂起拖垮整个阶段。
- `./test-xv6.py shell` 覆盖 shell 历史、变量、别名和 `||` 逻辑或。

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

P3 批次六新增回归用例：

- `kernel_kthread`：通过 selftest 模块创建内核线程，用户态 `wait` 回收并检查执行计数。

P3 批次七新增回归用例：

- `clone_tgid`：clone 子线程的 `getpid()` 等于父进程 tgid，`gettid()` 是新 tid。

P3 批次八新增回归用例：

- `clone_files`：子线程 close 共享 fd 后，父进程同一 fd 失效。
- `clone_cwd`：子线程 chdir 后，父进程相对路径随之改变。

P3 批次九新增回归用例：

- `clone_group_exit`：组长 exit 时同 tgid 线程被终止并回收，进程数恢复。

P3 批次十新增回归用例：

- `clone_join`：父线程用 `waitpid` 按 tid 精确等待两个 clone 线程。

P3 批次十一新增回归用例：

- `clone_tls`：子线程修改自己的 TLS 不影响父线程。

P3 批次十二新增回归用例：

- `clone_tgkill`：`tgkill` 只终止指定 tid，父线程不受影响。

P3 批次十三新增回归用例：

- `clone_vma`：父线程 `mmap` 后创建 clone 线程，子线程缺页写入后退出，
  父线程再次访问应看到同一物理页；`munmap` 后线程组内全部解除映射。

P3 批次十四新增回归用例：

- `sig_mask`：阻塞期间信号保持待处理，解除阻塞后在下一次返回用户态时投递。
- `clone_signal`：clone 线程共享信号处理表，`tgkill` 精确投递后子线程执行处理器。

P3-K1 新增回归用例：

- `pgid_basic`：子进程创建新进程组，父进程通过 `killpg` 只向该组投递信号，
  自身进程组保持不变。

P3-K2a 新增回归用例：

- `sig_stop_cont`：子进程被 `SIGSTOP` 停止后 `waitpid_flags` 以 `WUNTRACED`
  返回停止状态，`SIGCONT` 后以 `WCONTINUED` 返回继续事件，子进程随后正常退出。
- `tc_pgid`：`tcsetpgrp` / `tcgetpgrp` 能设置并读回终端前台进程组。

P3-K3 新增回归用例：

- `clone_flags_files`：`CLONE_FILES` 共享 fd 表但不共享地址空间。
- `clone_flags_fs`：`CLONE_FS` 共享 cwd 但保留独立 fd 表。
- `clone_flags_vm`：`CLONE_VM` 共享地址空间但不加入线程组。
- `clone_thread_fork`：非组长线程调用 `fork` 时，子进程只包含调用线程，
  `getpid() == gettid()` 且获得新线程组。
- `exit_group_basic`：非组长线程调用 `exit_group(7)` 后整个线程组退出，
  父进程 wait 到状态 7。
- `exec_thread_cleanup`：线程调用 `exec` 成功后其他线程被终止，
  新程序作为单线程进程运行并正确输出。

P3-K4 新增回归用例：

- `sigaction_mask`：`sigaction` 设置 `sa_mask` 后，处理器执行期间该信号被阻塞，
  `sigreturn` 后恢复原阻塞掩码。
- `sigpending_basic`：阻塞期间发送的信号会出现在 `sigpending` 结果中，
  解除阻塞后正常投递。
- `sigaction_reset`：`SA_RESETHAND` 只触发一次，随后恢复默认终止动作。
- `kill_negative_pgid`：`sigkill(-pgid, sig)` 只向目标进程组投递信号，
  调用者所在进程组不受影响。

P3-K5 新增回归用例：

- `dup2_basic`：`dup2` 复制 fd、保持同 fd 幂等，写入内容可读回。
- `getcwd_basic`：根目录返回 `/`，`chdir` 后返回对应绝对路径。
- `chroot_basic`：`chroot` 后 `/` 指向新根，进程只能看到 jail 内文件。
- `time_basic`：`clock_gettime` 单调推进，`nanosleep` 至少等待请求时长。
- `readv_writev_basic`：多个 iovec 连续写入和读回。
- `pipe2_poll_select`：`pipe2` 创建管道，`poll` / `select` 能检测可读与无效 fd。

P3-K6 新增回归用例：

- `mprotect_basic`：`mprotect` 修改 mmap 权限后原数据仍可读，非法参数被拒绝。
- `mprotect_none`：`PROT_NONE` 后访问页面会触发缺页并终止进程。
- `pseudo_devices`：`/dev/zero` 返回零字节，`/dev/null` 丢弃写入并返回 EOF。
- `flock_basic`：共享锁可同时持有，独占锁跨进程互斥，`LOCK_NB` 冲突时立即失败。

P3-K7 新增回归用例：

- `sem_basic`：信号量初值为 0 时子进程阻塞，父进程释放后子进程继续并正确退出。
- `futex_timeout`：等待值不变化时超时返回，值已变化时立即返回。
- `futex_robust`：持有者线程退出后，共享 futex 值被标记 owner-died，
  等待者立即返回而不是永久阻塞。
- `dynmod_multi`：扩展为验证 `dynmod2` 依赖 `dynmod` 时，先卸载依赖方，
  再卸载被依赖方；被依赖模块在仍有引用时卸载会失败。
- `dynmod_lifecycle`：扩展为验证模块参数注册与读写，参数卸载后自动清理。

P3-K8 新增回归用例：

- `clock_device`：`/dev/clock` 单调推进，`/dev/rtc` 返回合法实时时钟。

稳定性边界新增回归用例：

- `clone_badargs`：非法 flags、`CLONE_THREAD` 缺少 `CLONE_VM`、栈未对齐均被拒绝。
- `fd_badargs`：`dup2`、`pipe2`、`readv/writev`、`poll/select` 非法参数返回 -1。
- `sem_badargs`：`semget` / `semop` / `semctl` 非法参数与 `IPC_NOWAIT` 冲突被拒绝。
- `futex_badargs`：空地址、负超时等 futex 非法参数被拒绝。
- `mprotect_badargs`：未页对齐、零长度、非法 prot 被拒绝。
- `module_badargs`：非法槽位、零长度、超大模块被拒绝。

P3-K2b 新增自动化用例：

- `test-xv6.py jobs`：后台启动 `sleep`，`stop %1` 停止、`bg %1` 继续。

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
