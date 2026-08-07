// P3-K6 memory and filesystem tests.
//
// Each test function runs in its own child process under the usertests driver.
#include "tests.h"

void
mprotect_basic(char *s)
{
  char *p = mmap(0, PGSIZE, PROT_READ|PROT_WRITE,
                 MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);

  if(p == MAP_FAILED){
    printf("%s: mmap failed\n", s);
    exit(1);
  }
  p[0] = 0x5a;
  if(mprotect(p, PGSIZE, PROT_READ) < 0 || p[0] != 0x5a){
    printf("%s: mprotect read failed\n", s);
    exit(1);
  }
  if(mprotect((char*)PGSIZE, 0, PROT_READ) == 0 ||
     mprotect(p, PGSIZE, PROT_READ|0x80) == 0){
    printf("%s: mprotect badargs failed\n", s);
    exit(1);
  }
  if(munmap(p, PGSIZE) < 0){
    printf("%s: munmap failed\n", s);
    exit(1);
  }
  exit(0);
}

void
mprotect_none(char *s)
{
  int pid = fork();
  int st;

  if(pid < 0){
    printf("%s: fork failed\n", s);
    exit(1);
  }
  if(pid == 0){
    char *p = mmap(0, PGSIZE, PROT_READ|PROT_WRITE,
                   MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);

    if(p == MAP_FAILED)
      exit(1);
    p[0] = 1;
    if(mprotect(p, PGSIZE, PROT_NONE) < 0)
      exit(1);
    if(p[0] == 1)
      exit(1);
    exit(0);
  }
  if(wait(&st) != pid || st == 0){
    printf("%s: PROT_NONE did not fault\n", s);
    exit(1);
  }
  exit(0);
}

void
pseudo_devices(char *s)
{
  char buf[4];
  int fd;

  fd = open("zero", O_RDONLY);
  if(fd < 0 || read(fd, buf, 4) != 4 ||
     buf[0] != 0 || buf[1] != 0 || buf[2] != 0 || buf[3] != 0){
    printf("%s: /dev/zero failed\n", s);
    exit(1);
  }
  close(fd);

  fd = open("null", O_WRONLY);
  if(fd < 0 || write(fd, "x", 1) != 1){
    printf("%s: /dev/null write failed\n", s);
    exit(1);
  }
  close(fd);
  fd = open("null", O_RDONLY);
  if(fd < 0 || read(fd, buf, 1) != 0){
    printf("%s: /dev/null read failed\n", s);
    exit(1);
  }
  close(fd);
  exit(0);
}

void
flock_basic(char *s)
{
  int fd1, fd2, pid, st;

  unlink("flockfile");
  fd1 = open("flockfile", O_CREATE|O_RDWR);
  fd2 = open("flockfile", O_RDWR);
  if(fd1 < 0 || fd2 < 0){
    printf("%s: open failed\n", s);
    exit(1);
  }
  if(flock(fd1, LOCK_SH) < 0 || flock(fd2, LOCK_SH|LOCK_NB) < 0){
    printf("%s: shared flock failed\n", s);
    exit(1);
  }
  flock(fd1, LOCK_UN);
  flock(fd2, LOCK_UN);

  if(flock(fd1, LOCK_EX) < 0){
    printf("%s: exclusive flock failed\n", s);
    exit(1);
  }
  pid = fork();
  if(pid < 0){
    printf("%s: fork failed\n", s);
    exit(1);
  }
  if(pid == 0){
    int r = flock(fd2, LOCK_EX|LOCK_NB);
    exit(r == 0 ? 1 : 0);
  }
  if(wait(&st) != pid || st != 0){
    printf("%s: lock was not exclusive\n", s);
    exit(1);
  }
  if(flock(fd1, LOCK_UN) < 0 || flock(fd2, LOCK_EX|LOCK_NB) < 0){
    printf("%s: unlock/reacquire failed\n", s);
    exit(1);
  }
  flock(fd2, LOCK_UN);
  close(fd1);
  close(fd2);
  unlink("flockfile");
  exit(0);
}

struct test k6_quicktests[] = {
  {mprotect_basic, "mprotect_basic"},
  {mprotect_none, "mprotect_none"},
  {pseudo_devices, "pseudo_devices"},
  {flock_basic, "flock_basic"},
  { 0, 0},
};

struct test k6_slowtests[] = {
  { 0, 0},
};
