// P3-K5 syscall surface tests.
//
// Each test function runs in its own child process under the usertests driver.
#include "tests.h"

void
dup2_basic(char *s)
{
  int fd;
  char buf[3];

  unlink("dup2file");
  fd = open("dup2file", O_CREATE|O_RDWR);
  if(fd < 0){
    printf("%s: open failed\n", s);
    exit(1);
  }
  if(dup2(fd, 7) != 7 || dup2(7, 7) != 7){
    printf("%s: dup2 failed\n", s);
    exit(1);
  }
  if(write(7, "OK", 2) != 2){
    printf("%s: dup2 write failed\n", s);
    exit(1);
  }
  close(fd);
  close(7);
  fd = open("dup2file", O_RDONLY);
  if(fd < 0 || read(fd, buf, 2) != 2 ||
     buf[0] != 'O' || buf[1] != 'K'){
    printf("%s: dup2 content failed\n", s);
    exit(1);
  }
  close(fd);
  unlink("dup2file");
  exit(0);
}

void
getcwd_basic(char *s)
{
  char buf[MAXPATH];

  if(getcwd(buf, sizeof(buf)) < 0 || strcmp(buf, "/") != 0){
    printf("%s: root cwd %s\n", s, buf);
    exit(1);
  }
  unlink("/cdir");
  if(mkdir("/cdir") < 0){
    printf("%s: mkdir failed\n", s);
    exit(1);
  }
  if(chdir("/cdir") < 0 || getcwd(buf, sizeof(buf)) < 0 ||
     strcmp(buf, "/cdir") != 0){
    printf("%s: cwd path %s\n", s, buf);
    exit(1);
  }
  if(chdir("/") < 0){
    printf("%s: chdir root failed\n", s);
    exit(1);
  }
  unlink("/cdir");
  exit(0);
}

void
chroot_basic(char *s)
{
  int pid = fork();
  int st;

  if(pid < 0){
    printf("%s: fork failed\n", s);
    exit(1);
  }
  unlink("/jail/jailfile");
  unlink("/jail");
  if(mkdir("/jail") < 0){
    printf("%s: mkdir jail failed\n", s);
    exit(1);
  }
  if(pid == 0){
    char buf[MAXPATH];
    int fd;

    if(chroot("/jail") < 0 || chdir("/") < 0 ||
       getcwd(buf, sizeof(buf)) < 0 || strcmp(buf, "/") != 0)
      exit(1);
    fd = open("jailfile", O_CREATE|O_WRONLY);
    if(fd < 0 || write(fd, "x", 1) != 1)
      exit(1);
    close(fd);
    exit(0);
  }
  if(wait(&st) != pid || st != 0){
    printf("%s: chroot child failed\n", s);
    exit(1);
  }
  unlink("/jail/jailfile");
  unlink("/jail");
  exit(0);
}

void
time_basic(char *s)
{
  struct timespec t1, t2, req;
  uint64 n1, n2;

  req.tv_sec = 0;
  req.tv_nsec = 200000000;
  if(clock_gettime(CLOCK_MONOTONIC, &t1) < 0 ||
     nanosleep(&req, 0) < 0 ||
     clock_gettime(CLOCK_MONOTONIC, &t2) < 0){
    printf("%s: time syscalls failed\n", s);
    exit(1);
  }
  n1 = (uint64)t1.tv_sec * 1000000000UL + t1.tv_nsec;
  n2 = (uint64)t2.tv_sec * 1000000000UL + t2.tv_nsec;
  if(n2 < n1 || n2 - n1 < 100000000){
    printf("%s: monotonic clock did not advance\n", s);
    exit(1);
  }
  exit(0);
}

void
readv_writev_basic(char *s)
{
  struct iovec wv[2], rv[2];
  char buf[5];
  int fd;

  unlink("iovfile");
  fd = open("iovfile", O_CREATE|O_RDWR);
  if(fd < 0){
    printf("%s: open failed\n", s);
    exit(1);
  }
  wv[0].iov_base = (void*)"AB";
  wv[0].iov_len = 2;
  wv[1].iov_base = (void*)"CD";
  wv[1].iov_len = 2;
  if(writev(fd, wv, 2) != 4 || fsync(fd) < 0){
    printf("%s: writev failed\n", s);
    exit(1);
  }
  close(fd);

  fd = open("iovfile", O_RDONLY);
  if(fd < 0){
    printf("%s: reopen failed\n", s);
    exit(1);
  }
  rv[0].iov_base = buf;
  rv[0].iov_len = 2;
  rv[1].iov_base = buf + 2;
  rv[1].iov_len = 2;
  if(readv(fd, rv, 2) != 4 || buf[0] != 'A' || buf[1] != 'B' ||
     buf[2] != 'C' || buf[3] != 'D'){
    printf("%s: readv failed\n", s);
    exit(1);
  }
  close(fd);
  unlink("iovfile");
  exit(0);
}

void
pipe2_poll_select(char *s)
{
  struct pollfd pfd;
  fd_set rf;
  int fds[2];
  char c;

  if(pipe2(fds, O_CLOEXEC|O_NONBLOCK) < 0){
    printf("%s: pipe2 failed\n", s);
    exit(1);
  }
  pfd.fd = fds[0];
  pfd.events = POLLIN;
  pfd.revents = 0;
  if(poll(&pfd, 1, 0) != 0 || pfd.revents != 0){
    printf("%s: empty pipe poll ready\n", s);
    exit(1);
  }
  if(read(fds[0], &c, 1) != 0){
    printf("%s: nonblocking read blocked\n", s);
    exit(1);
  }
  if(write(fds[1], "x", 1) != 1){
    printf("%s: pipe write failed\n", s);
    exit(1);
  }
  pfd.revents = 0;
  if(poll(&pfd, 1, 0) != 1 || !(pfd.revents & POLLIN)){
    printf("%s: pipe poll not ready\n", s);
    exit(1);
  }

  FD_ZERO(&rf);
  FD_SET(fds[0], &rf);
  if(select(fds[0] + 1, &rf, 0, 0, 0) != 1 ||
     !FD_ISSET(fds[0], &rf)){
    printf("%s: select failed\n", s);
    exit(1);
  }
  if(read(fds[0], &c, 1) != 1 || c != 'x'){
    printf("%s: pipe read failed\n", s);
    exit(1);
  }

  pfd.fd = 999;
  pfd.events = POLLIN;
  pfd.revents = 0;
  if(poll(&pfd, 1, 0) != 1 || !(pfd.revents & POLLNVAL)){
    printf("%s: invalid fd poll failed\n", s);
    exit(1);
  }
  close(fds[0]);
  close(fds[1]);
  exit(0);
}

struct test k5_quicktests[] = {
  {dup2_basic, "dup2_basic"},
  {getcwd_basic, "getcwd_basic"},
  {chroot_basic, "chroot_basic"},
  {time_basic, "time_basic"},
  {readv_writev_basic, "readv_writev_basic"},
  {pipe2_poll_select, "pipe2_poll_select"},
  { 0, 0},
};

struct test k5_slowtests[] = {
  { 0, 0},
};
