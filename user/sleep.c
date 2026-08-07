// sleep：按 tick 数睡眠，供 shell 作业控制测试使用。
#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int n = 1;

  if(argc > 1)
    n = atoi(argv[1]);
  if(n < 0)
    n = 1;
  if(pause(n) < 0)
    exit(1);
  printf("sleep done\n");
  exit(0);
}
