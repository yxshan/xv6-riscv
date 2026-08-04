// 系统调用与参数安全测试。
//
// 每个测试函数由 usertests 驱动在独立子进程中运行。
#include "tests.h"
void
copyin(char *s)
{
  uint64 addrs[] = { 0x80000000LL, 0x3fffffe000, 0x3ffffff000, 0x4000000000,
                     0xffffffffffffffff };

  for(int ai = 0; ai < sizeof(addrs)/sizeof(addrs[0]); ai++){
    uint64 addr = addrs[ai];

    int fd = open("copyin1", O_CREATE|O_WRONLY);
    if(fd < 0){
      printf("open(copyin1) failed\n");
      exit(1);
    }
    int n = write(fd, (void*)addr, 8192);
    if(n >= 0){
      printf("write(fd, %p, 8192) returned %d, not -1\n", (void*)addr, n);
      exit(1);
    }
    close(fd);
    unlink("copyin1");

    n = write(1, (char*)addr, 8192);
    if(n > 0){
      printf("write(1, %p, 8192) returned %d, not -1 or 0\n", (void*)addr, n);
      exit(1);
    }

    int fds[2];
    if(pipe(fds) < 0){
      printf("pipe() failed\n");
      exit(1);
    }
    n = write(fds[1], (char*)addr, 8192);
    if(n > 0){
      printf("write(pipe, %p, 8192) returned %d, not -1 or 0\n", (void*)addr, n);
      exit(1);
    }
    close(fds[0]);
    close(fds[1]);
  }
}

// 给 copyout 类的系统调用传递非法指针，验证内核不会写入非法地址。

void
copyout(char *s)
{
  uint64 addrs[] = { 0LL, 0x80000000LL, 0x3fffffe000, 0x3ffffff000, 0x4000000000,
                     0xffffffffffffffff };

  for(int ai = 0; ai < sizeof(addrs)/sizeof(addrs[0]); ai++){
    uint64 addr = addrs[ai];

    int fd = open("README.md", 0);
    if(fd < 0){
      printf("open(README) failed\n");
      exit(1);
    }
    int n = read(fd, (void*)addr, 8192);
    if(n > 0){
      printf("read(fd, %p, 8192) returned %d, not -1 or 0\n", (void*)addr, n);
      exit(1);
    }
    close(fd);

    int fds[2];
    if(pipe(fds) < 0){
      printf("pipe() failed\n");
      exit(1);
    }
    n = write(fds[1], "x", 1);
    if(n != 1){
      printf("pipe write failed\n");
      exit(1);
    }
    n = read(fds[0], (void*)addr, 8192);
    if(n > 0){
      printf("read(pipe, %p, 8192) returned %d, not -1 or 0\n", (void*)addr, n);
      exit(1);
    }
    close(fds[0]);
    close(fds[1]);
  }
}

// 给字符串参数传递非法指针，验证 copyinstr 的安全边界。

void
copyinstr1(char *s)
{
  uint64 addrs[] = { 0x80000000LL, 0x3fffffe000, 0x3ffffff000, 0x4000000000,
                     0xffffffffffffffff };

  for(int ai = 0; ai < sizeof(addrs)/sizeof(addrs[0]); ai++){
    uint64 addr = addrs[ai];

    int fd = open((char *)addr, O_CREATE|O_WRONLY);
    if(fd >= 0){
      printf("open(%p) returned %d, not -1\n", (void*)addr, fd);
      exit(1);
    }
  }
}

// what if a string system call argument is exactly the size
// of the kernel buffer it is copied into, so that the null
// would fall just beyond the end of the kernel buffer?

void
copyinstr2(char *s)
{
  char b[MAXPATH+1];

  for(int i = 0; i < MAXPATH; i++)
    b[i] = 'x';
  b[MAXPATH] = '\0';

  int ret = unlink(b);
  if(ret != -1){
    printf("unlink(%s) returned %d, not -1\n", b, ret);
    exit(1);
  }

  int fd = open(b, O_CREATE | O_WRONLY);
  if(fd != -1){
    printf("open(%s) returned %d, not -1\n", b, fd);
    exit(1);
  }

  ret = link(b, b);
  if(ret != -1){
    printf("link(%s, %s) returned %d, not -1\n", b, b, ret);
    exit(1);
  }

  char *args[] = { "xx", 0 };
  ret = exec(b, args);
  if(ret != -1){
    printf("exec(%s) returned %d, not -1\n", b, fd);
    exit(1);
  }

  int pid = fork();
  if(pid < 0){
    printf("fork failed\n");
    exit(1);
  }
  if(pid == 0){
    static char big[PGSIZE+1];
    for(int i = 0; i < PGSIZE; i++)
      big[i] = 'x';
    big[PGSIZE] = '\0';
    char *args2[] = { big, big, big, 0 };
    ret = exec("echo", args2);
    if(ret != -1){
      printf("exec(echo, BIG) returned %d, not -1\n", fd);
      exit(1);
    }
    exit(747); // OK
  }

  int st = 0;
  wait(&st);
  if(st != 747){
    printf("exec(echo, BIG) succeeded, should have failed\n");
    exit(1);
  }
}

// what if a string argument crosses over the end of last user page?

void
copyinstr3(char *s)
{
  sbrk(8192);
  uint64 top = (uint64) sbrk(0);
  if((top % PGSIZE) != 0){
    sbrk(PGSIZE - (top % PGSIZE));
  }
  top = (uint64) sbrk(0);
  if(top % PGSIZE){
    printf("oops\n");
    exit(1);
  }

  char *b = (char *) (top - 1);
  *b = 'x';

  int ret = unlink(b);
  if(ret != -1){
    printf("unlink(%s) returned %d, not -1\n", b, ret);
    exit(1);
  }

  int fd = open(b, O_CREATE | O_WRONLY);
  if(fd != -1){
    printf("open(%s) returned %d, not -1\n", b, fd);
    exit(1);
  }

  ret = link(b, b);
  if(ret != -1){
    printf("link(%s, %s) returned %d, not -1\n", b, b, ret);
    exit(1);
  }

  char *args[] = { "xx", 0 };
  ret = exec(b, args);
  if(ret != -1){
    printf("exec(%s) returned %d, not -1\n", b, fd);
    exit(1);
  }
}

// See if the kernel refuses to read/write user memory that the
// application doesn't have anymore, because it returned it.

void
validatetest(char *s)
{
  int hi;
  uint64 p;

  hi = 1100*1024;
  for(p = 0; p <= (uint)hi; p += PGSIZE){
    // try to crash the kernel by passing in a bad string pointer
    if(link("nosuchfile", (char*)p) != -1){
      printf("%s: link should not succeed\n", s);
      exit(1);
    }
  }
}

void *big = (void*) 0xeaeb0b5b00002f5e;

void
pgbug(char *s)
{
  char *argv[1];
  argv[0] = 0;
  exec(big, argv);
  pipe(big);

  exit(0);
}

// regression test. does the kernel panic if a process sbrk()s its
// size to be less than a page, or zero, or reduces the break by an
// amount too small to cause a page to be freed?

void
badarg(char *s)
{
  for(int i = 0; i < 50000; i++){
    char *argv[2];
    argv[0] = (char*)0xffffffff;
    argv[1] = 0;
    exec("echo", argv);
  }

  exit(0);
}

struct test syscall_quicktests[] = {
  {copyin, "copyin"},
  {copyout, "copyout"},
  {copyinstr1, "copyinstr1"},
  {copyinstr2, "copyinstr2"},
  {copyinstr3, "copyinstr3"},
  {validatetest, "validatetest"},
  {pgbug, "pgbug"},
  {badarg, "badarg"},
  { 0, 0},
};
struct test syscall_slowtests[] = {
  { 0, 0},
};
