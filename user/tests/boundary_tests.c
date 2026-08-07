// 系统调用与内核接口的边界测试。
//
// 每个测试函数运行在独立子进程中，只验证非法参数返回 -1，
// 不依赖时序，也不应触发内核 panic。
#include "tests.h"
#include "kernel/sem.h"
#include "kernel/futex.h"

extern void clone_stub(void);

void
clone_badargs(char *s)
{
  char *stack = sbrk(PGSIZE);

  if(stack == SBRK_ERROR){
    printf("%s: sbrk failed\n", s);
    exit(1);
  }
  if(clone(0xdead, (uint64)0, 0, (uint64)(stack + PGSIZE),
           (uint64)clone_stub) != -1){
    printf("%s: unsupported clone flags accepted\n", s);
    exit(1);
  }
  if(clone(CLONE_THREAD, (uint64)0, 0, (uint64)(stack + PGSIZE),
           (uint64)clone_stub) != -1){
    printf("%s: CLONE_THREAD without CLONE_VM accepted\n", s);
    exit(1);
  }
  if(clone(CLONE_VM, (uint64)0, 0, (uint64)(stack + 1),
           (uint64)clone_stub) != -1){
    printf("%s: misaligned clone stack accepted\n", s);
    exit(1);
  }
  sbrk(-PGSIZE);
  exit(0);
}

void
fd_badargs(char *s)
{
  struct iovec iov;
  struct pollfd pfd;
  int fds[2];

  if(dup2(-1, 0) != -1 || dup2(0, -1) != -1 ||
     dup2(0, NOFILE) != -1){
    printf("%s: dup2 badargs accepted\n", s);
    exit(1);
  }
  if(pipe2(fds, 0xdead) != -1){
    printf("%s: pipe2 bad flags accepted\n", s);
    exit(1);
  }
  iov.iov_base = 0;
  iov.iov_len = 1;
  if(readv(-1, &iov, 1) != -1 || writev(-1, &iov, 1) != -1 ||
     readv(0, &iov, 0) != -1 || writev(0, &iov, 17) != -1){
    printf("%s: readv/writev badargs accepted\n", s);
    exit(1);
  }
  pfd.fd = 0;
  pfd.events = POLLIN;
  if(poll(&pfd, -1, 0) != -1 || poll(&pfd, NOFILE + 1, 0) != -1 ||
     poll(&pfd, 1, -2) != -1){
    printf("%s: poll badargs accepted\n", s);
    exit(1);
  }
  if(select(-1, 0, 0, 0, 0) != -1 ||
     select(NOFILE + 1, 0, 0, 0, 0) != -1){
    printf("%s: select badargs accepted\n", s);
    exit(1);
  }
  exit(0);
}

void
sem_badargs(char *s)
{
  struct sembuf op = { 0, -1, IPC_NOWAIT };
  int semid;

  if(semget(1, 0, IPC_CREAT) != -1 ||
     semget(1, SEM_NSEMS_MAX + 1, IPC_CREAT) != -1){
    printf("%s: semget bad nsems accepted\n", s);
    exit(1);
  }
  semid = semget(4243, 1, IPC_CREAT);
  if(semid < 0){
    printf("%s: semget failed\n", s);
    exit(1);
  }
  if(semctl(semid, 0, SETVAL, 0) < 0 ||
     semop(semid, &op, 1) != -1){
    printf("%s: IPC_NOWAIT acquire accepted\n", s);
    exit(1);
  }
  if(semop(-1, &op, 1) != -1 || semctl(-1, 0, GETVAL, 0) != -1 ||
     semctl(semid, 0, 999, 0) != -1){
    printf("%s: sem bad ids accepted\n", s);
    exit(1);
  }
  semctl(semid, 0, IPC_RMID, 0);
  exit(0);
}

void
futex_badargs(char *s)
{
  if(futex_wait(0, 0) != -1 ||
     futex_wait_timeout(0, 0, 0) != -1 ||
     futex_wait_timeout((uint64)&s, 0, -1) != -1 ||
     futex_set_owner(0, 1) != -1 ||
     futex_clear_owner(0) != -1){
    printf("%s: futex badargs accepted\n", s);
    exit(1);
  }
  exit(0);
}

void
mprotect_badargs(char *s)
{
  char *p = mmap(0, PGSIZE, PROT_READ|PROT_WRITE,
                 MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);

  if(p == MAP_FAILED){
    printf("%s: mmap failed\n", s);
    exit(1);
  }
  if(mprotect(p + 1, PGSIZE, PROT_READ) != -1 ||
     mprotect(p, 0, PROT_READ) != -1 ||
     mprotect(p, PGSIZE, PROT_READ|0x80) != -1){
    printf("%s: mprotect badargs accepted\n", s);
    exit(1);
  }
  munmap(p, PGSIZE);
  exit(0);
}

void
module_badargs(char *s)
{
  if(module_unload(-1) != -1 || module_unload(99) != -1 ||
     module_load(0, 0) != -1 || module_load(0, 40000) != -1){
    printf("%s: module badargs accepted\n", s);
    exit(1);
  }
  exit(0);
}

struct test boundary_quicktests[] = {
  {clone_badargs, "clone_badargs"},
  {fd_badargs, "fd_badargs"},
  {sem_badargs, "sem_badargs"},
  {futex_badargs, "futex_badargs"},
  {mprotect_badargs, "mprotect_badargs"},
  {module_badargs, "module_badargs"},
  { 0, 0},
};

struct test boundary_slowtests[] = {
  { 0, 0},
};
