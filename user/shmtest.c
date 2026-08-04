// shmtest：验证共享内存父子进程共享。

#include "kernel/types.h"
#include "user/user.h"

int
main(void)
{
  int id, pid;
  char *p;

  id = shmget(1, 4096);
  if(id < 0){
    printf("shmtest: shmget failed\n");
    exit(1);
  }

  p = (char*)shmat(id);
  if(p == 0){
    printf("shmtest: shmat failed\n");
    exit(1);
  }

  pid = fork();
  if(pid < 0){
    printf("shmtest: fork failed\n");
    exit(1);
  }

  if(pid == 0){
    p[0] = 'S';
    p[1] = 'H';
    p[2] = 'M';
    p[3] = 0;
    exit(0);
  }

  wait(0);
  printf("shared=%s\n", p);

  shmdt(id);
  shmrm(id);
  exit(0);
}
