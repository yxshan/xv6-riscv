// sleep：按 tick 数睡眠，供 shell 作业控制测试使用。
#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int n = 1;
  int start;
  int elapsed;

  if(argc > 1)
    n = atoi(argv[1]);
  if(n < 0)
    n = 1;
  start = uptime();
  for(;;){
    elapsed = uptime() - start;
    if(elapsed >= n)
      break;
    // SIGSTOP 会让 pause 返回 -1；继续运行后按剩余 tick 重睡。
    if(pause(n - elapsed) < 0)
      continue;
    break;
  }
  printf("sleep done\n");
  exit(0);
}
