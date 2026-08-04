// prio：设置进程调度优先级。
//
// 用法：
//   prio <pid> <priority>

#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int pid, prio, r;

  if(argc != 3){
    printf("usage: prio <pid> <priority>\n");
    exit(1);
  }

  pid = atoi(argv[1]);
  prio = atoi(argv[2]);
  r = setpriority(pid, prio);
  printf("setpriority(%d, %d) = %d\n", pid, prio, r);
  exit(0);
}
