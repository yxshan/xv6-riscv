// ps：读取 /proc 并打印进程与系统信息。

#include "kernel/types.h"
#include "kernel/fcntl.h"
#include "user/user.h"

int
main(void)
{
  char buf[2048];
  int fd, n;

  if((fd = open("proc", O_RDONLY)) < 0){
    printf("ps: cannot open /proc\n");
    exit(1);
  }

  n = read(fd, buf, sizeof(buf));
  close(fd);
  if(n < 0){
    printf("ps: read failed\n");
    exit(1);
  }
  if(n > 0)
    write(1, buf, n);
  exit(0);
}
