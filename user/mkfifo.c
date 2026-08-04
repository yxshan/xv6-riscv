// mkfifo：创建命名管道。
//
// 用法：
//   mkfifo <path>

#include "kernel/types.h"
#include "user/user.h"

int
main(int argc, char *argv[])
{
  int r;

  if(argc != 2){
    printf("usage: mkfifo <path>\n");
    exit(1);
  }

  r = mkfifo(argv[1], 0);
  printf("mkfifo(%s) = %d\n", argv[1], r);
  exit(0);
}
