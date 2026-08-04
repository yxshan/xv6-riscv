// ps：读取 /proc 并打印进程与系统信息。

#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"

int
main(void)
{
  char buf[512];
  int fd, n, total = 0;

  if((fd = open("proc", O_RDONLY)) < 0){
    printf("ps: cannot open /proc\n");
    exit(1);
  }

  while((n = read(fd, buf, sizeof(buf))) > 0){
    write(1, buf, n);
    total += n;
  }
  close(fd);
  if(n < 0){
    printf("ps: read failed\n");
    exit(1);
  }
  if(total == 0){
    printf("ps: empty /proc\n");
    exit(1);
  }
  exit(0);
}
