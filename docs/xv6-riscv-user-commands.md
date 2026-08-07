# xv6-riscv 用户命令参考

以下命令来自 `Makefile` 的 `UPROGS`、`user/modules/` 自动收集程序，
以及 `sh` 的内建命令。除特殊说明外，所有命令都在 xv6 shell 中执行。

## 1. 文件与目录

| 命令 | 说明 | 使用案例 |
|---|---|---|
| `cat` | 连接并输出文件，无参数时读标准输入 | `cat README.md` |
| `echo` | 输出参数 | `echo hello world` |
| `grep` | 按模式匹配文本 | `grep xv6 README.md` |
| `wc` | 统计行数、单词数、字节数 | `wc README.md` |
| `ls` | 列出目录内容，无参数时列出当前目录 | `ls /` |
| `mkdir` | 创建目录 | `mkdir /tmp` |
| `rm` | 删除文件或空目录 | `rm file.txt` |
| `ln` | 创建硬链接；`-s` 创建符号链接 | `ln -s README.md link` |
| `chmod` | 修改文件权限，八进制模式 | `chmod 600 file.txt` |
| `chown` | 修改文件 uid/gid | `chown 1 1 file.txt` |
| `mkfifo` | 创建命名管道 | `mkfifo /tmp/pipe` |
| `mount` | 挂载磁盘到目录 | `mount 2 /mnt` |
| `umount` | 卸载挂载点 | `umount /mnt` |

## 2. 进程与系统

| 命令 | 说明 | 使用案例 |
|---|---|---|
| `sleep` | 按 tick 数睡眠 | `sleep 100` |
| `kill` | 终止指定进程 | `kill 5` |
| `ps` | 读取 `/proc` 显示进程列表 | `ps` |
| `id` | 显示 uid/gid/euid/egid | `id` |
| `prio` | 设置进程优先级 | `prio 3 200` |
| `aslr` | 打印当前用户栈地址 | `aslr` |
| `swapinfo` | 显示交换空间状态 | `swapinfo` |
| `crashdump` | 触发内核崩溃转储 | `crashdump` |
| `init` | 系统第一个用户进程，启动 shell | 通常由内核自动启动 |

## 3. 模块与观测

| 命令 | 说明 | 使用案例 |
|---|---|---|
| `modload` | 加载动态模块 ELF | `modload dynmod` |
| `modunload` | 卸载指定槽位模块 | `modunload 0` |
| `modcli` | 调用 `module_call` | `modcli 1 1` |
| `strace` | 跟踪命令的系统调用统计 | `strace echo hi` |
| `perf` | 显示命令的 syscall/exit/ticks 统计 | `perf echo hi` |
| `procinfo` | 通过模块读取进程信息 | `procinfo` |
| `hello` | 用户模块示例 | `hello` |

`modcli` 常见模块 ID：

- `1` sysinfo：`1` 进程数、`2` 空闲页、`3` ticks、`4` dump
- `2` trace：`1` 总 syscall、`3` 重置、`4` forks、`5` exits
- `3` 动态模块 `dynmod`：`1` 返回 `0x1234`、`3/4` 读写参数
- `4` selftest：`1` 基本检查、`2` 注册表、`3` 信号、`4` 内存
- `5` 动态模块 `dynmod2`：`1` 返回 `0xABCD`、`2` 返回字符、`3` 进程数

示例：

```sh
modload dynmod
modcli 3 1
modcli 3 4 42
modcli 3 3
modunload 0
```

## 4. 测试与压力程序

| 命令 | 说明 |
|---|---|
| `usertests` | 运行用户态回归套件；`-q` 只跑 quick，`testname` 只跑单个测试 |
| `grind` | 随机系统调用压力测试 |
| `stressfs` | 文件系统压力测试 |
| `cowtest` | COW fork 测试 |
| `shmtest` | 共享内存测试 |
| `signaltest` | 信号处理测试 |
| `permexec` | 权限 exec 测试，固定退出码 42 |
| `forktest` | fork 测试 |
| `zombie` | 僵尸进程测试 |
| `logstress` | 日志写入压力测试，参数为文件名列表 |
| `forphan` | 制造孤儿文件供崩溃恢复测试 |
| `dorphan` | 制造孤儿目录供崩溃恢复测试 |

示例：

```sh
usertests -q
usertests sem_basic
logstress f1 f2 f3
```

## 5. Shell 内建命令

| 命令 | 说明 | 使用案例 |
|---|---|---|
| `cd` | 切换目录 | `cd /tmp` |
| `pwd` | 打印当前目录 | `pwd` |
| `exit` | 退出 shell | `exit` |
| `jobs` | 列出后台作业 | `jobs` |
| `fg` | 把作业调到前台 | `fg %1` |
| `bg` | 继续后台作业 | `bg %1` |
| `stop` | 停止后台作业 | `stop %1` |
| `history` | 显示命令历史 | `history` |
| `!!` | 重放上一条命令 | `!!` |
| `!n` | 重放第 n 条历史命令 | `!3` |
| `↑` / `↓` | 在提示符下切换历史命令 | `↑` 选择上一条，`↓` 回到空行 |
| `export` | 设置或显示 shell 变量 | `export NAME=value` |
| `unset` | 删除 shell 变量 | `unset NAME` |
| `vars` | 显示所有 shell 变量 | `vars` |
| `alias` | 设置或显示别名 | `alias ll=ls` |
| `unalias` | 删除别名 | `unalias ll` |
| `aliases` | 显示所有别名 | `aliases` |

Shell 语法示例：

```sh
echo one && echo two
cat missing || echo OR-OK
ls / | wc
echo hi > out.txt
sleep 100 &
export GREETING=hello
echo $GREETING
alias hi=echo hi
hi
```
