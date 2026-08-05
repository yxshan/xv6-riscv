// mmap 私有映射与需求分页测试。
//
// 每个测试函数由 usertests 驱动在独立子进程中运行。
#include "tests.h"

void
mmap_file_demand(char *s)
{
  unlink("mmfile");
  int fd = open("mmfile", O_CREATE|O_RDWR|O_TRUNC);
  if(fd < 0){
    printf("%s: create mmfile failed\n", s);
    exit(1);
  }
  memset(buf, 'A', PGSIZE);
  memset(buf + PGSIZE, 'B', PGSIZE);
  if(write(fd, buf, 2*PGSIZE) != 2*PGSIZE){
    printf("%s: write mmfile failed\n", s);
    exit(1);
  }
  close(fd);

  fd = open("mmfile", O_RDONLY);
  if(fd < 0){
    printf("%s: reopen mmfile failed\n", s);
    exit(1);
  }
  char *p = mmap(0, 2*PGSIZE, PROT_READ|PROT_WRITE, MAP_PRIVATE, fd, 0);
  if(p == MAP_FAILED){
    printf("%s: mmap failed\n", s);
    exit(1);
  }
  if(p[0] != 'A' || p[PGSIZE] != 'B' || p[2*PGSIZE-1] != 'B'){
    printf("%s: mapped content wrong\n", s);
    exit(1);
  }
  p[0] = 'X';
  p[PGSIZE] = 'Y';

  char *q = mmap(0, PGSIZE, PROT_READ, MAP_PRIVATE, fd, PGSIZE);
  if(q == MAP_FAILED || q[0] != 'B'){
    printf("%s: offset mmap failed\n", s);
    exit(1);
  }
  if(munmap(q, PGSIZE) < 0 || munmap(p, 2*PGSIZE) < 0){
    printf("%s: munmap failed\n", s);
    exit(1);
  }

  fd = open("mmfile", O_RDONLY);
  if(fd < 0 || read(fd, buf, 2*PGSIZE) != 2*PGSIZE){
    printf("%s: reread mmfile failed\n", s);
    exit(1);
  }
  close(fd);
  if(buf[0] != 'A' || buf[PGSIZE] != 'B'){
    printf("%s: private write changed file\n", s);
    exit(1);
  }
  unlink("mmfile");
}

void
mmap_anonymous(char *s)
{
  char *p = mmap(0, PGSIZE, PROT_READ|PROT_WRITE,
                 MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);

  if(p == MAP_FAILED){
    printf("%s: anonymous mmap failed\n", s);
    exit(1);
  }
  if(p[0] != 0 || p[PGSIZE-1] != 0){
    printf("%s: anonymous map not zero\n", s);
    exit(1);
  }
  p[0] = 7;
  p[PGSIZE-1] = 9;
  if(munmap(p, PGSIZE) < 0){
    printf("%s: anonymous munmap failed\n", s);
    exit(1);
  }
}

void
mmap_fork(char *s)
{
  char *p = mmap(0, PGSIZE, PROT_READ|PROT_WRITE,
                 MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  int pid, st = 0;

  if(p == MAP_FAILED){
    printf("%s: fork mmap failed\n", s);
    exit(1);
  }
  p[0] = 'P';

  pid = fork();
  if(pid < 0){
    printf("%s: fork failed\n", s);
    exit(1);
  }
  if(pid == 0){
    if(p[0] != 'P')
      exit(1);
    p[0] = 'C';
    exit(0);
  }
  wait(&st);
  if(st != 0 || p[0] != 'P'){
    printf("%s: fork copy of mapping failed\n", s);
    exit(1);
  }
  if(munmap(p, PGSIZE) < 0){
    printf("%s: fork munmap failed\n", s);
    exit(1);
  }
}

void
mmap_badargs(char *s)
{
  if(mmap(0, 0, PROT_READ, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0) != MAP_FAILED ||
     mmap((char*)PGSIZE, PGSIZE, PROT_READ, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0) != MAP_FAILED ||
     mmap(0, PGSIZE, PROT_READ, MAP_SHARED|MAP_ANONYMOUS, -1, 0) != MAP_FAILED ||
     mmap(0, PGSIZE, PROT_READ, MAP_PRIVATE, 1, 0) != MAP_FAILED){
    printf("%s: bad mmap accepted\n", s);
    exit(1);
  }
}

void
mmap_after_unmap(char *s)
{
  char *p = mmap(0, PGSIZE, PROT_READ|PROT_WRITE,
                 MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
  int pid, st = 0;

  if(p == MAP_FAILED || munmap(p, PGSIZE) < 0){
    printf("%s: setup failed\n", s);
    exit(1);
  }
  pid = fork();
  if(pid < 0){
    printf("%s: fork failed\n", s);
    exit(1);
  }
  if(pid == 0){
    p[0] = 1;
    exit(0);
  }
  wait(&st);
  if(st != -1){
    printf("%s: access after munmap should fault\n", s);
    exit(1);
  }
}

struct test mmap_quicktests[] = {
  {mmap_file_demand, "mmap_file_demand"},
  {mmap_anonymous, "mmap_anonymous"},
  {mmap_fork, "mmap_fork"},
  {mmap_badargs, "mmap_badargs"},
  {mmap_after_unmap, "mmap_after_unmap"},
  { 0, 0},
};

struct test mmap_slowtests[] = {
  { 0, 0},
};
