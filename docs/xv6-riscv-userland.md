# xv6-riscv 用户态扩展路线

> P4 阶段目标：内核已经达到教学级基本完全体，接下来把重点放到用户态应用，
> 优先让 shell、工具链和日常使用体验更完整，避免频繁回到内核侧改代码。

## 1. Shell 增强

当前 `sh` 已完成：

- 命令历史：`history`、`!!`、`!n`。
- 上/下方向键：在提示符下切换历史命令，`Down` 回到空行。
- Shell 变量：`export NAME=value`、`unset NAME`、`vars`、`$NAME` 展开。
- 别名：`alias name=cmd`、`unalias name`、`aliases`，命令首词展开。
- 内建命令：`pwd`、`exit`、`history`、`export`、`unset`、`vars`、
  `alias`、`unalias`、`aliases`。
- 逻辑或：`cmd1 || cmd2`，与已有 `&&`、`;`、管道、后台、重定向组合使用。

新增自动化测试：`./test-xv6.py shell`，并已接入 `test-quick` 与 GitHub Actions。

## 2. 下一批用户态候选

- 编辑器：适合小文件编辑的 `ed` 风格行编辑器。
- 用户态服务框架：通过 FIFO/管道提供后台服务协议。
- 更完整的 shell：引号解析、通配符、`for` 循环、`$?` 状态码。
- 动态链接器：用户态 ELF 加载器与共享库原型。
- 工具链完善：`cp`、`mv`、`which`、`env`、`time`。

## 2.1 Shell 进一步优化

- 引号与转义：单引号、双引号、反斜杠。
- 通配符：`*`、`?`、`[...]`。
- 命令替换：`$(cmd)` 与反引号。
- 退出码：`$?` 和 `set -e`。
- 环境变量继承：`export` 的变量传入子进程。
- `PATH` 查找与 `which`。
- 循环与脚本：`for`、`source`。
- 更完整的行编辑：左右移动、Home/End、Tab 补全。
- 历史持久化：把 `history` 保存到文件，重启 shell 后恢复。
- 作业控制增强：`kill %n`、`wait %n`、`disown`。
- 重定向增强：`2>`、`&>`、heredoc。
- 内建命令：`true`、`false`、`cd -`、`~`。
- 命令查找：当前目录未命中时回退到根目录标准命令。

## 3. 原则

- 优先复用现有系统调用与模块基础设施。
- 新功能尽量以独立用户程序或静态模块形式加入。
- 每个新功能必须提供 `test-xv6.py` 自动化或 `usertests` 用例。
