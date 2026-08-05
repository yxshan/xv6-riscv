// modload：把动态模块 ELF 文件加载进内核。
//
// 用法：
//   modload dynmod

#include "kernel/types.h"
#include "kernel/stat.h"
#include "kernel/fcntl.h"
#include "user/user.h"

#define MODLOAD_MAX 16384

static void
usage(void)
{
  printf("usage: modload <file>\n");
  exit(1);
}

int
main(int argc, char *argv[])
{
  struct stat st;
  char *buf;
  int fd, n, r;

  if(argc != 2)
    usage();

  if((fd = open(argv[1], O_RDONLY)) < 0){
    printf("modload: open %s failed\n", argv[1]);
    exit(1);
  }
  if(fstat(fd, &st) < 0 || st.size <= 0 || st.size > MODLOAD_MAX){
    printf("modload: bad file size\n");
    close(fd);
    exit(1);
  }

  buf = malloc(st.size);
  if(buf == 0){
    printf("modload: out of memory\n");
    close(fd);
    exit(1);
  }

  n = read(fd, buf, st.size);
  close(fd);
  if(n != st.size){
    printf("modload: read failed\n");
    exit(1);
  }

  r = module_load((uint64)buf, st.size);
  printf("module_load = %d\n", r);
  exit(0);
}
