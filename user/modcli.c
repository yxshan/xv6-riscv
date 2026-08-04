// modcli：module_call 系统调用的通用测试/调用工具。
//
// 用法：
//   modcli <module_id> <cmd> [arg0] [arg1]
// 示例：
//   modcli 1 1    -> 进程数
//   modcli 1 2    -> 空闲物理页数
//   modcli 1 3    -> 系统 ticks

#include "kernel/types.h"
#include "user/user.h"

static void
usage(void)
{
  printf("usage: modcli <module_id> <cmd> [arg0] [arg1]\n");
  exit(1);
}

int
main(int argc, char *argv[])
{
  int id, cmd;
  uint64 arg0 = 0, arg1 = 0;
  uint64 ret;

  if(argc < 3)
    usage();

  id = atoi(argv[1]);
  cmd = atoi(argv[2]);
  if(argc > 3)
    arg0 = (uint64)atoi(argv[3]);
  if(argc > 4)
    arg1 = (uint64)atoi(argv[4]);

  ret = module_call(id, cmd, arg0, arg1);
  printf("module_call(%d, %d) = %d\n", id, cmd, (int)ret);
  exit(0);
}
