// 内核模块功能测试，例如 FIFO 与 COW。
//
// 每个测试函数由 usertests 驱动在独立子进程中运行。
#include "tests.h"
void
fifo_rdwr(char *s)
{
  char c;
  int fd, pid, st;

  unlink("fifordwr");
  if(mkfifo("fifordwr", 0) < 0){
    printf("%s: mkfifo failed\n", s);
    exit(1);
  }

  fd = open("fifordwr", O_RDWR);
  if(fd < 0){
    printf("%s: open fifo O_RDWR failed\n", s);
    exit(1);
  }
  if(write(fd, "x", 1) != 1 || read(fd, &c, 1) != 1 || c != 'x'){
    printf("%s: fifo O_RDWR read/write failed\n", s);
    exit(1);
  }
  close(fd);

  pid = fork();
  if(pid < 0){
    printf("%s: fork failed\n", s);
    exit(1);
  }
  if(pid == 0){
    fd = open("fifordwr", O_RDONLY);
    if(fd < 0)
      exit(1);
    if(read(fd, &c, 1) != 1 || c != 'y')
      exit(1);
    close(fd);
    exit(0);
  }

  fd = open("fifordwr", O_RDWR);
  if(fd < 0 || write(fd, "y", 1) != 1){
    printf("%s: fifo O_RDWR writer failed\n", s);
    exit(1);
  }
  close(fd);
  if(wait(&st) != pid || st != 0){
    printf("%s: fifo reader failed\n", s);
    exit(1);
  }
  unlink("fifordwr");
  exit(0);
}

// COW fork：子进程写入多个页面后，父进程数据必须保持不变。

void
cowfork(char *s)
{
  enum { NPAGES = 16 };
  char *base = sbrk(0);
  char *a = sbrk(NPAGES * PGSIZE);

  if(a == SBRK_ERROR){
    printf("%s: sbrk failed\n", s);
    exit(1);
  }
  memset(a, 0x5a, NPAGES * PGSIZE);

  for(int i = 0; i < 4; i++){
    int pid = fork();
    int st;
    if(pid < 0){
      printf("%s: fork failed\n", s);
      exit(1);
    }
    if(pid == 0){
      for(int j = 0; j < NPAGES; j++)
        a[j * PGSIZE + (j * 17) % PGSIZE] = (char)('a' + i);
      exit(0);
    }
    if(wait(&st) != pid || st != 0){
      printf("%s: COW child failed\n", s);
      exit(1);
    }
    for(int j = 0; j < NPAGES; j++){
      if(a[j * PGSIZE + (j * 17) % PGSIZE] != 0x5a){
        printf("%s: COW parent page corrupted at %d\n", s, j);
        exit(1);
      }
    }
  }

  sbrk(-((uint64)sbrk(0) - (uint64)base));
  exit(0);
}


struct test module_quicktests[] = {
  {fifo_rdwr, "fifo_rdwr"},
  {cowfork, "cowfork"},
  { 0, 0},
};
struct test module_slowtests[] = {
  { 0, 0},
};
