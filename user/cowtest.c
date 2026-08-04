// cowtest：验证写时复制 fork。

#include "kernel/types.h"
#include "user/user.h"

int
main(void)
{
  char msg[] = "before";
  int pid;

  pid = fork();
  if(pid < 0){
    printf("cowtest: fork failed\n");
    exit(1);
  }

  if(pid == 0){
    msg[0] = 'A';
    msg[1] = 'f';
    msg[2] = 't';
    msg[3] = 'e';
    msg[4] = 'r';
    exit(0);
  }

  wait(0);
  printf("parent=%s\n", msg);
  if(msg[0] == 'b')
    printf("COW OK\n");
  else
    printf("COW FAIL\n");
  exit(0);
}
