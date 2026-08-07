# 用户态测试套件

`usertests` 驱动按下面的套件顺序执行 `quicktests`，可选执行 `slowtests`。
每个测试函数都由驱动 fork 到独立子进程中运行，失败以非 0 退出码表示。

| 文件 | 覆盖范围 |
|---|---|
| `syscall_tests.c` | 基础系统调用参数与行为 |
| `mem_tests.c` | sbrk、惰性分配、交换、内存压力 |
| `fs_tests.c` | 文件系统、目录、挂载、日志与崩溃恢复 |
| `proc_tests.c` | fork/wait/exec、进程组、clone 基础 |
| `module_tests.c` | 动态模块加载、依赖、参数、设备 |
| `perm_tests.c` | 权限、uid/gid、umask |
| `mmap_tests.c` | mmap/munmap、文件映射、共享映射 |
| `thread_tests.c` | 线程组语义、clone flags、exit_group、多线程 exec |
| `signal_tests.c` | sigaction、sigpending、进程组信号 |
| `k5_tests.c` | K5 系统调用面 |
| `k6_tests.c` | mprotect、伪设备、文件锁 |
| `k7_tests.c` | 信号量、futex 超时与 robust |
| `k8_tests.c` | `/dev/clock`、`/dev/rtc` |
| `boundary_tests.c` | 非法参数与边界输入 |

新增测试时：

1. 按子系统放到对应 `*_tests.c`，并加入 `quicktests[]` 或 `slowtests[]`。
2. 测试名称使用 `subsystem_case` 形式，例如 `sem_badargs`。
3. 新增套件时同步更新 `tests.h` 与 `usertests.c` 的 `suites[]`。
