#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "kernel/module/module_ids.h"
#include "user/user.h"

// Stress xv6 logging system by having several processes writing
// concurrently to their own file (e.g., logstress f1 f2 f3 f4).
// 每次写满后通过 O_TRUNC 重置并循环，保证崩溃测试时始终有写入进行。
// 第一个参数为 stall 时启用测试专用提交停顿，供 crash 测试稳定命中恢复窗口。

enum { N = 250, SZ = 2000 };
char buf[SZ];

int
main(int argc, char **argv)
{
  int fd, n;
  int first = 1;

  if(argc > 1 && strcmp(argv[1], "stall") == 0){
    first = 2;
    for(int i = first; i < argc; i++){
      fd = open(argv[i], O_CREATE | O_RDWR | O_TRUNC);
      if(fd < 0){
        printf("%s: create %s failed\n", argv[0], argv[i]);
        exit(1);
      }
      close(fd);
    }
    printf("logstress files ready\n");
    module_call(KMOD_SELFTEST, SELFTEST_CMD_LOG_STALL, 100, 0);
  }

  printf("logstress start\n");
  while(1){
    for (int i = first; i < argc; i++){
      int pid1 = fork();
      if(pid1 < 0){
        printf("%s: fork failed\n", argv[0]);
        exit(1);
      }
      if(pid1 == 0) {
        fd = open(argv[i], O_CREATE | O_RDWR | O_TRUNC);
        if(fd < 0){
          printf("%s: create %s failed\n", argv[0], argv[i]);
          exit(1);
        }
        memset(buf, '0'+i, SZ);
        for(i = 0; i < N; i++){
          if((n = write(fd, buf, SZ)) != SZ)
            break;
        }
        close(fd);
        exit(0);
      }
    }
    int xstatus;
    for(int i = first; i < argc; i++){
      wait(&xstatus);
      if(xstatus != 0)
        exit(xstatus);
    }
  }
}
