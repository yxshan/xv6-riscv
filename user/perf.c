// perf：轻量性能计数器。
//
// 用法：
//   perf <command> [args...]
// 运行命令并报告系统调用数、时钟节拍、fork/exit 次数和空闲内存页。

#include "kernel/types.h"
#include "user/user.h"
#include "kernel/module/module_ids.h"

static void
usage(void)
{
  printf("usage: perf <command> [args...]\n");
  exit(1);
}

int
main(int argc, char *argv[])
{
  int pid, status;
  uint64 total, forks, exits, ticks, mem;

  if(argc < 2)
    usage();

  module_call(KMOD_TRACE, TRACE_CMD_RESET, 0, 0);

  pid = fork();
  if(pid < 0){
    printf("perf: fork failed\n");
    exit(1);
  }
  if(pid == 0){
    exec(argv[1], &argv[1]);
    printf("perf: exec %s failed\n", argv[1]);
    exit(1);
  }

  wait(&status);

  total = module_call(KMOD_TRACE, TRACE_CMD_TOTAL, 0, 0);
  forks = module_call(KMOD_TRACE, TRACE_CMD_FORKS, 0, 0);
  exits = module_call(KMOD_TRACE, TRACE_CMD_EXITS, 0, 0);
  ticks = module_call(KMOD_TRACE, TRACE_CMD_TICKS, 0, 0);
  mem = module_call(KMOD_SYSINFO, SYSINFO_CMD_MEM, 0, 0);

  printf("perf: syscalls=%d ticks=%d forks=%d exits=%d free_pages=%d\n",
         (int)total, (int)ticks, (int)forks, (int)exits, (int)mem);

  exit(0);
}
