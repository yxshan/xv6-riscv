// strace：基于 trace 模块的系统调用汇总工具。
//
// 用法：
//   strace <command> [args...]
// 它先重置 trace 统计，运行指定命令，然后打印系统调用汇总。

#include "kernel/types.h"
#include "user/user.h"
#include "kernel/module/module_ids.h"
#include "kernel/syscall.h"
#include "kernel/syscall_names.h"

#define STRACE_NAME(num, name) [num] = #name,
static const char *syscall_names[] = {
  SYSCALL_NAME_TABLE(STRACE_NAME)
};

static void
usage(void)
{
  printf("usage: strace <command> [args...]\n");
  exit(1);
}

int
main(int argc, char *argv[])
{
  int pid, status;
  uint64 total, forks, exits, ticks;

  if(argc < 2)
    usage();

  module_call(KMOD_TRACE, TRACE_CMD_RESET, 0, 0);

  pid = fork();
  if(pid < 0){
    printf("strace: fork failed\n");
    exit(1);
  }
  if(pid == 0){
    exec(argv[1], &argv[1]);
    printf("strace: exec %s failed\n", argv[1]);
    exit(1);
  }

  wait(&status);

  total = module_call(KMOD_TRACE, TRACE_CMD_TOTAL, 0, 0);
  forks = module_call(KMOD_TRACE, TRACE_CMD_FORKS, 0, 0);
  exits = module_call(KMOD_TRACE, TRACE_CMD_EXITS, 0, 0);
  ticks = module_call(KMOD_TRACE, TRACE_CMD_TICKS, 0, 0);

  printf("strace: syscalls=%d forks=%d exits=%d ticks=%d status=%d\n",
         (int)total, (int)forks, (int)exits, (int)ticks, status);

  for(int i = 1; i < (int)(sizeof(syscall_names)/sizeof(syscall_names[0])); i++){
    uint64 n = module_call(KMOD_TRACE, TRACE_CMD_SYS, i, 0);
    if(n > 0){
      if(syscall_names[i])
        printf("  syscall %d (%s): %d\n", i, syscall_names[i], (int)n);
      else
        printf("  syscall %d: %d\n", i, (int)n);
    }
  }

  exit(0);
}
