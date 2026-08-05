// 文件系统、目录与日志相关测试。
//
// 每个测试函数由 usertests 驱动在独立子进程中运行。
#include "tests.h"
void
truncate1(char *s)
{
  char buf[32];

  unlink("truncfile");
  int fd1 = open("truncfile", O_CREATE|O_WRONLY|O_TRUNC);
  write(fd1, "abcd", 4);
  close(fd1);

  int fd2 = open("truncfile", O_RDONLY);
  int n = read(fd2, buf, sizeof(buf));
  if(n != 4){
    printf("%s: read %d bytes, wanted 4\n", s, n);
    exit(1);
  }

  fd1 = open("truncfile", O_WRONLY|O_TRUNC);

  int fd3 = open("truncfile", O_RDONLY);
  n = read(fd3, buf, sizeof(buf));
  if(n != 0){
    printf("aaa fd3=%d\n", fd3);
    printf("%s: read %d bytes, wanted 0\n", s, n);
    exit(1);
  }

  n = read(fd2, buf, sizeof(buf));
  if(n != 0){
    printf("bbb fd2=%d\n", fd2);
    printf("%s: read %d bytes, wanted 0\n", s, n);
    exit(1);
  }

  write(fd1, "abcdef", 6);

  n = read(fd3, buf, sizeof(buf));
  if(n != 6){
    printf("%s: read %d bytes, wanted 6\n", s, n);
    exit(1);
  }

  n = read(fd2, buf, sizeof(buf));
  if(n != 2){
    printf("%s: read %d bytes, wanted 2\n", s, n);
    exit(1);
  }

  unlink("truncfile");

  close(fd1);
  close(fd2);
  close(fd3);
}

// write to an open FD whose file has just been truncated.
// this causes a write at an offset beyond the end of the file.
// such writes fail on xv6 (unlike POSIX) but at least
// they don't crash.

void
truncate2(char *s)
{
  unlink("truncfile");

  int fd1 = open("truncfile", O_CREATE|O_TRUNC|O_WRONLY);
  write(fd1, "abcd", 4);

  int fd2 = open("truncfile", O_TRUNC|O_WRONLY);

  int n = write(fd1, "x", 1);
  if(n != -1){
    printf("%s: write returned %d, expected -1\n", s, n);
    exit(1);
  }

  unlink("truncfile");
  close(fd1);
  close(fd2);
}


void
truncate3(char *s)
{
  int pid, xstatus;

  close(open("truncfile", O_CREATE|O_TRUNC|O_WRONLY));

  pid = fork();
  if(pid < 0){
    printf("%s: fork failed\n", s);
    exit(1);
  }

  if(pid == 0){
    for(int i = 0; i < 100; i++){
      char buf[32];
      int fd = open("truncfile", O_WRONLY);
      if(fd < 0){
        printf("%s: open failed\n", s);
        exit(1);
      }
      int n = write(fd, "1234567890", 10);
      if(n != 10){
        printf("%s: write got %d, expected 10\n", s, n);
        exit(1);
      }
      close(fd);
      fd = open("truncfile", O_RDONLY);
      read(fd, buf, sizeof(buf));
      close(fd);
    }
    exit(0);
  }

  for(int i = 0; i < 150; i++){
    int fd = open("truncfile", O_CREATE|O_WRONLY|O_TRUNC);
    if(fd < 0){
      printf("%s: open failed\n", s);
      exit(1);
    }
    int n = write(fd, "xxx", 3);
    if(n != 3){
      printf("%s: write got %d, expected 3\n", s, n);
      exit(1);
    }
    close(fd);
  }

  wait(&xstatus);
  unlink("truncfile");
  exit(xstatus);
}


// does chdir() call iput(p->cwd) in a transaction?

void
iputtest(char *s)
{
  if(mkdir("iputdir") < 0){
    printf("%s: mkdir failed\n", s);
    exit(1);
  }
  if(chdir("iputdir") < 0){
    printf("%s: chdir iputdir failed\n", s);
    exit(1);
  }
  if(unlink("../iputdir") < 0){
    printf("%s: unlink ../iputdir failed\n", s);
    exit(1);
  }
  if(chdir("/") < 0){
    printf("%s: chdir / failed\n", s);
    exit(1);
  }
}

// does exit() call iput(p->cwd) in a transaction?

void
exitiputtest(char *s)
{
  int pid, xstatus;

  pid = fork();
  if(pid < 0){
    printf("%s: fork failed\n", s);
    exit(1);
  }
  if(pid == 0){
    if(mkdir("iputdir") < 0){
      printf("%s: mkdir failed\n", s);
      exit(1);
    }
    if(chdir("iputdir") < 0){
      printf("%s: child chdir failed\n", s);
      exit(1);
    }
    if(unlink("../iputdir") < 0){
      printf("%s: unlink ../iputdir failed\n", s);
      exit(1);
    }
    exit(0);
  }
  wait(&xstatus);
  exit(xstatus);
}

// does the error path in open() for attempt to write a
// directory call iput() in a transaction?
// needs a hacked kernel that pauses just after the namei()
// call in sys_open():
//    if((ip = namei(path)) == 0)
//      return -1;
//    {
//      int i;
//      for(i = 0; i < 10000; i++)
//        yield();
//    }

void
openiputtest(char *s)
{
  int pid, xstatus;

  if(mkdir("oidir") < 0){
    printf("%s: mkdir oidir failed\n", s);
    exit(1);
  }
  pid = fork();
  if(pid < 0){
    printf("%s: fork failed\n", s);
    exit(1);
  }
  if(pid == 0){
    int fd = open("oidir", O_RDWR);
    if(fd >= 0){
      printf("%s: open directory for write succeeded\n", s);
      exit(1);
    }
    exit(0);
  }
  pause(1);
  if(unlink("oidir") != 0){
    printf("%s: unlink failed\n", s);
    exit(1);
  }
  wait(&xstatus);
  exit(xstatus);
}

// simple file system tests


void
opentest(char *s)
{
  int fd;

  fd = open("echo", 0);
  if(fd < 0){
    printf("%s: open echo failed!\n", s);
    exit(1);
  }
  close(fd);
  fd = open("doesnotexist", 0);
  if(fd >= 0){
    printf("%s: open doesnotexist succeeded!\n", s);
    exit(1);
  }
}


void
writetest(char *s)
{
  int fd;
  int i;
  enum { N=100, SZ=10 };

  fd = open("small", O_CREATE|O_RDWR);
  if(fd < 0){
    printf("%s: error: creat small failed!\n", s);
    exit(1);
  }
  for(i = 0; i < N; i++){
    if(write(fd, "aaaaaaaaaa", SZ) != SZ){
      printf("%s: error: write aa %d new file failed\n", s, i);
      exit(1);
    }
    if(write(fd, "bbbbbbbbbb", SZ) != SZ){
      printf("%s: error: write bb %d new file failed\n", s, i);
      exit(1);
    }
  }
  close(fd);
  fd = open("small", O_RDONLY);
  if(fd < 0){
    printf("%s: error: open small failed!\n", s);
    exit(1);
  }
  i = read(fd, buf, N*SZ*2);
  if(i != N*SZ*2){
    printf("%s: read failed\n", s);
    exit(1);
  }
  close(fd);

  if(unlink("small") < 0){
    printf("%s: unlink small failed\n", s);
    exit(1);
  }
}


void
writebig(char *s)
{
  int i, fd, n;

  fd = open("big", O_CREATE|O_RDWR);
  if(fd < 0){
    printf("%s: error: creat big failed!\n", s);
    exit(1);
  }

  for(i = 0; i < MAXFILE; i++){
    ((int*)buf)[0] = i;
    if(write(fd, buf, BSIZE) != BSIZE){
      printf("%s: error: write big file failed i=%d\n", s, i);
      exit(1);
    }
  }

  close(fd);

  fd = open("big", O_RDONLY);
  if(fd < 0){
    printf("%s: error: open big failed!\n", s);
    exit(1);
  }

  n = 0;
  for(;;){
    i = read(fd, buf, BSIZE);
    if(i == 0){
      if(n != MAXFILE){
        printf("%s: read only %d blocks from big", s, n);
        exit(1);
      }
      break;
    } else if(i != BSIZE){
      printf("%s: read failed %d\n", s, i);
      exit(1);
    }
    if(((int*)buf)[0] != n){
      printf("%s: read content of block %d is %d\n", s,
             n, ((int*)buf)[0]);
      exit(1);
    }
    n++;
  }
  close(fd);
  if(unlink("big") < 0){
    printf("%s: unlink big failed\n", s);
    exit(1);
  }
}

// many creates, followed by unlink test

void
createtest(char *s)
{
  int i, fd;
  enum { N=52 };

  char name[3];
  name[0] = 'a';
  name[2] = '\0';
  for(i = 0; i < N; i++){
    name[1] = '0' + i;
    fd = open(name, O_CREATE|O_RDWR);
    close(fd);
  }
  name[0] = 'a';
  name[2] = '\0';
  for(i = 0; i < N; i++){
    name[1] = '0' + i;
    unlink(name);
  }
}


void dirtest(char *s)
{
  if(mkdir("dir0") < 0){
    printf("%s: mkdir failed\n", s);
    exit(1);
  }

  if(chdir("dir0") < 0){
    printf("%s: chdir dir0 failed\n", s);
    exit(1);
  }

  if(chdir("..") < 0){
    printf("%s: chdir .. failed\n", s);
    exit(1);
  }

  if(unlink("dir0") < 0){
    printf("%s: unlink dir0 failed\n", s);
    exit(1);
  }
}


void
sharedfd(char *s)
{
  int fd, pid, i, n, nc, np;
  enum { N = 1000, SZ=10};
  char buf[SZ];

  unlink("sharedfd");
  fd = open("sharedfd", O_CREATE|O_RDWR);
  if(fd < 0){
    printf("%s: cannot open sharedfd for writing", s);
    exit(1);
  }
  pid = fork();
  memset(buf, pid==0?'c':'p', sizeof(buf));
  for(i = 0; i < N; i++){
    if(write(fd, buf, sizeof(buf)) != sizeof(buf)){
      printf("%s: write sharedfd failed\n", s);
      exit(1);
    }
  }
  if(pid == 0) {
    exit(0);
  } else {
    int xstatus;
    wait(&xstatus);
    if(xstatus != 0)
      exit(xstatus);
  }

  close(fd);
  fd = open("sharedfd", 0);
  if(fd < 0){
    printf("%s: cannot open sharedfd for reading\n", s);
    exit(1);
  }
  nc = np = 0;
  while((n = read(fd, buf, sizeof(buf))) > 0){
    for(i = 0; i < sizeof(buf); i++){
      if(buf[i] == 'c')
        nc++;
      if(buf[i] == 'p')
        np++;
    }
  }
  close(fd);
  unlink("sharedfd");
  if(nc == N*SZ && np == N*SZ){
    exit(0);
  } else {
    printf("%s: nc/np test fails\n", s);
    exit(1);
  }
}

// four processes write different files at the same
// time, to test block allocation.

void
fourfiles(char *s)
{
  int fd, pid, i, j, n, total, pi;
  char *names[] = { "f0", "f1", "f2", "f3" };
  char *fname;
  enum { N=12, NCHILD=4, SZ=500 };

  for(pi = 0; pi < NCHILD; pi++){
    fname = names[pi];
    unlink(fname);

    pid = fork();
    if(pid < 0){
      printf("%s: fork failed\n", s);
      exit(1);
    }

    if(pid == 0){
      fd = open(fname, O_CREATE | O_RDWR);
      if(fd < 0){
        printf("%s: create failed\n", s);
        exit(1);
      }

      memset(buf, '0'+pi, SZ);
      for(i = 0; i < N; i++){
        if((n = write(fd, buf, SZ)) != SZ){
          printf("write failed %d\n", n);
          exit(1);
        }
      }
      exit(0);
    }
  }

  int xstatus;
  for(pi = 0; pi < NCHILD; pi++){
    wait(&xstatus);
    if(xstatus != 0)
      exit(xstatus);
  }

  for(i = 0; i < NCHILD; i++){
    fname = names[i];
    fd = open(fname, 0);
    total = 0;
    while((n = read(fd, buf, sizeof(buf))) > 0){
      for(j = 0; j < n; j++){
        if(buf[j] != '0'+i){
          printf("%s: wrong char\n", s);
          exit(1);
        }
      }
      total += n;
    }
    close(fd);
    if(total != N*SZ){
      printf("wrong length %d\n", total);
      exit(1);
    }
    unlink(fname);
  }
}

// four processes create and delete different files in same directory

void
createdelete(char *s)
{
  enum { N = 20, NCHILD=4 };
  int pid, i, fd, pi;
  char name[32];

  for(pi = 0; pi < NCHILD; pi++){
    pid = fork();
    if(pid < 0){
      printf("%s: fork failed\n", s);
      exit(1);
    }

    if(pid == 0){
      name[0] = 'p' + pi;
      name[2] = '\0';
      for(i = 0; i < N; i++){
        name[1] = '0' + i;
        fd = open(name, O_CREATE | O_RDWR);
        if(fd < 0){
          printf("%s: create failed\n", s);
          exit(1);
        }
        close(fd);
        if(i > 0 && (i % 2 ) == 0){
          name[1] = '0' + (i / 2);
          if(unlink(name) < 0){
            printf("%s: unlink failed\n", s);
            exit(1);
          }
        }
      }
      exit(0);
    }
  }

  int xstatus;
  for(pi = 0; pi < NCHILD; pi++){
    wait(&xstatus);
    if(xstatus != 0)
      exit(1);
  }

  name[0] = name[1] = name[2] = 0;
  for(i = 0; i < N; i++){
    for(pi = 0; pi < NCHILD; pi++){
      name[0] = 'p' + pi;
      name[1] = '0' + i;
      fd = open(name, 0);
      if((i == 0 || i >= N/2) && fd < 0){
        printf("%s: oops createdelete %s didn't exist\n", s, name);
        exit(1);
      } else if((i >= 1 && i < N/2) && fd >= 0){
        printf("%s: oops createdelete %s did exist\n", s, name);
        exit(1);
      }
      if(fd >= 0)
        close(fd);
    }
  }

  for(i = 0; i < N; i++){
    for(pi = 0; pi < NCHILD; pi++){
      name[0] = 'p' + pi;
      name[1] = '0' + i;
      unlink(name);
    }
  }
}

// can I unlink a file and still read it?

void
unlinkread(char *s)
{
  enum { SZ = 5 };
  int fd, fd1;

  fd = open("unlinkread", O_CREATE | O_RDWR);
  if(fd < 0){
    printf("%s: create unlinkread failed\n", s);
    exit(1);
  }
  write(fd, "hello", SZ);
  close(fd);

  fd = open("unlinkread", O_RDWR);
  if(fd < 0){
    printf("%s: open unlinkread failed\n", s);
    exit(1);
  }
  if(unlink("unlinkread") != 0){
    printf("%s: unlink unlinkread failed\n", s);
    exit(1);
  }

  fd1 = open("unlinkread", O_CREATE | O_RDWR);
  write(fd1, "yyy", 3);
  close(fd1);

  if(read(fd, buf, sizeof(buf)) != SZ){
    printf("%s: unlinkread read failed", s);
    exit(1);
  }
  if(buf[0] != 'h'){
    printf("%s: unlinkread wrong data\n", s);
    exit(1);
  }
  if(write(fd, buf, 10) != 10){
    printf("%s: unlinkread write failed\n", s);
    exit(1);
  }
  close(fd);
  unlink("unlinkread");
}


void
linktest(char *s)
{
  enum { SZ = 5 };
  int fd;

  unlink("lf1");
  unlink("lf2");

  fd = open("lf1", O_CREATE|O_RDWR);
  if(fd < 0){
    printf("%s: create lf1 failed\n", s);
    exit(1);
  }
  if(write(fd, "hello", SZ) != SZ){
    printf("%s: write lf1 failed\n", s);
    exit(1);
  }
  close(fd);

  if(link("lf1", "lf2") < 0){
    printf("%s: link lf1 lf2 failed\n", s);
    exit(1);
  }
  unlink("lf1");

  if(open("lf1", 0) >= 0){
    printf("%s: unlinked lf1 but it is still there!\n", s);
    exit(1);
  }

  fd = open("lf2", 0);
  if(fd < 0){
    printf("%s: open lf2 failed\n", s);
    exit(1);
  }
  if(read(fd, buf, sizeof(buf)) != SZ){
    printf("%s: read lf2 failed\n", s);
    exit(1);
  }
  close(fd);

  if(link("lf2", "lf2") >= 0){
    printf("%s: link lf2 lf2 succeeded! oops\n", s);
    exit(1);
  }

  unlink("lf2");
  if(link("lf2", "lf1") >= 0){
    printf("%s: link non-existent succeeded! oops\n", s);
    exit(1);
  }

  if(link(".", "lf1") >= 0){
    printf("%s: link . lf1 succeeded! oops\n", s);
    exit(1);
  }
}

// test concurrent create/link/unlink of the same file

void
concreate(char *s)
{
  enum { N = 40 };
  char file[3];
  int i, pid, n, fd;
  char fa[N];
  struct {
    ushort inum;
    char name[DIRSIZ];
  } de;

  file[0] = 'C';
  file[2] = '\0';
  for(i = 0; i < N; i++){
    file[1] = '0' + i;
    unlink(file);
    pid = fork();
    if(pid && (i % 3) == 1){
      link("C0", file);
    } else if(pid == 0 && (i % 5) == 1){
      link("C0", file);
    } else {
      fd = open(file, O_CREATE | O_RDWR);
      if(fd < 0){
        printf("concreate create %s failed\n", file);
        exit(1);
      }
      close(fd);
    }
    if(pid == 0) {
      exit(0);
    } else {
      int xstatus;
      wait(&xstatus);
      if(xstatus != 0)
        exit(1);
    }
  }

  memset(fa, 0, sizeof(fa));
  fd = open(".", 0);
  n = 0;
  while(read(fd, &de, sizeof(de)) > 0){
    if(de.inum == 0)
      continue;
    if(de.name[0] == 'C' && de.name[2] == '\0'){
      i = de.name[1] - '0';
      if(i < 0 || i >= sizeof(fa)){
        printf("%s: concreate weird file %s\n", s, de.name);
        exit(1);
      }
      if(fa[i]){
        printf("%s: concreate duplicate file %s\n", s, de.name);
        exit(1);
      }
      fa[i] = 1;
      n++;
    }
  }
  close(fd);

  if(n != N){
    printf("%s: concreate not enough files in directory listing\n", s);
    exit(1);
  }

  for(i = 0; i < N; i++){
    file[1] = '0' + i;
    pid = fork();
    if(pid < 0){
      printf("%s: fork failed\n", s);
      exit(1);
    }
    if(((i % 3) == 0 && pid == 0) ||
       ((i % 3) == 1 && pid != 0)){
      close(open(file, 0));
      close(open(file, 0));
      close(open(file, 0));
      close(open(file, 0));
      close(open(file, 0));
      close(open(file, 0));
    } else {
      unlink(file);
      unlink(file);
      unlink(file);
      unlink(file);
      unlink(file);
      unlink(file);
    }
    if(pid == 0)
      exit(0);
    else
      wait(0);
  }
}

// another concurrent link/unlink/create test,
// to look for deadlocks.

void
linkunlink(char *s)
{
  int pid, i;

  unlink("x");
  pid = fork();
  if(pid < 0){
    printf("%s: fork failed\n", s);
    exit(1);
  }

  unsigned int x = (pid ? 1 : 97);
  for(i = 0; i < 100; i++){
    x = x * 1103515245 + 12345;
    if((x % 3) == 0){
      close(open("x", O_RDWR | O_CREATE));
    } else if((x % 3) == 1){
      link("cat", "x");
    } else {
      unlink("x");
    }
  }

  if(pid)
    wait(0);
  else
    exit(0);
}



void
subdir(char *s)
{
  int fd, cc;

  unlink("ff");
  if(mkdir("dd") != 0){
    printf("%s: mkdir dd failed\n", s);
    exit(1);
  }

  fd = open("dd/ff", O_CREATE | O_RDWR);
  if(fd < 0){
    printf("%s: create dd/ff failed\n", s);
    exit(1);
  }
  write(fd, "ff", 2);
  close(fd);

  if(unlink("dd") >= 0){
    printf("%s: unlink dd (non-empty dir) succeeded!\n", s);
    exit(1);
  }

  if(mkdir("/dd/dd") != 0){
    printf("%s: subdir mkdir dd/dd failed\n", s);
    exit(1);
  }

  fd = open("dd/dd/ff", O_CREATE | O_RDWR);
  if(fd < 0){
    printf("%s: create dd/dd/ff failed\n", s);
    exit(1);
  }
  write(fd, "FF", 2);
  close(fd);

  fd = open("dd/dd/../ff", 0);
  if(fd < 0){
    printf("%s: open dd/dd/../ff failed\n", s);
    exit(1);
  }
  cc = read(fd, buf, sizeof(buf));
  if(cc != 2 || buf[0] != 'f'){
    printf("%s: dd/dd/../ff wrong content\n", s);
    exit(1);
  }
  close(fd);

  if(link("dd/dd/ff", "dd/dd/ffff") != 0){
    printf("%s: link dd/dd/ff dd/dd/ffff failed\n", s);
    exit(1);
  }

  if(unlink("dd/dd/ff") != 0){
    printf("%s: unlink dd/dd/ff failed\n", s);
    exit(1);
  }
  if(open("dd/dd/ff", O_RDONLY) >= 0){
    printf("%s: open (unlinked) dd/dd/ff succeeded\n", s);
    exit(1);
  }

  if(chdir("dd") != 0){
    printf("%s: chdir dd failed\n", s);
    exit(1);
  }
  if(chdir("dd/../../dd") != 0){
    printf("%s: chdir dd/../../dd failed\n", s);
    exit(1);
  }
  if(chdir("dd/../../../dd") != 0){
    printf("%s: chdir dd/../../../dd failed\n", s);
    exit(1);
  }
  if(chdir("./..") != 0){
    printf("%s: chdir ./.. failed\n", s);
    exit(1);
  }

  fd = open("dd/dd/ffff", 0);
  if(fd < 0){
    printf("%s: open dd/dd/ffff failed\n", s);
    exit(1);
  }
  if(read(fd, buf, sizeof(buf)) != 2){
    printf("%s: read dd/dd/ffff wrong len\n", s);
    exit(1);
  }
  close(fd);

  if(open("dd/dd/ff", O_RDONLY) >= 0){
    printf("%s: open (unlinked) dd/dd/ff succeeded!\n", s);
    exit(1);
  }

  if(open("dd/ff/ff", O_CREATE|O_RDWR) >= 0){
    printf("%s: create dd/ff/ff succeeded!\n", s);
    exit(1);
  }
  if(open("dd/xx/ff", O_CREATE|O_RDWR) >= 0){
    printf("%s: create dd/xx/ff succeeded!\n", s);
    exit(1);
  }
  if(open("dd", O_CREATE) >= 0){
    printf("%s: create dd succeeded!\n", s);
    exit(1);
  }
  if(open("dd", O_RDWR) >= 0){
    printf("%s: open dd rdwr succeeded!\n", s);
    exit(1);
  }
  if(open("dd", O_WRONLY) >= 0){
    printf("%s: open dd wronly succeeded!\n", s);
    exit(1);
  }
  if(link("dd/ff/ff", "dd/dd/xx") == 0){
    printf("%s: link dd/ff/ff dd/dd/xx succeeded!\n", s);
    exit(1);
  }
  if(link("dd/xx/ff", "dd/dd/xx") == 0){
    printf("%s: link dd/xx/ff dd/dd/xx succeeded!\n", s);
    exit(1);
  }
  if(link("dd/ff", "dd/dd/ffff") == 0){
    printf("%s: link dd/ff dd/dd/ffff succeeded!\n", s);
    exit(1);
  }
  if(mkdir("dd/ff/ff") == 0){
    printf("%s: mkdir dd/ff/ff succeeded!\n", s);
    exit(1);
  }
  if(mkdir("dd/xx/ff") == 0){
    printf("%s: mkdir dd/xx/ff succeeded!\n", s);
    exit(1);
  }
  if(mkdir("dd/dd/ffff") == 0){
    printf("%s: mkdir dd/dd/ffff succeeded!\n", s);
    exit(1);
  }
  if(unlink("dd/xx/ff") == 0){
    printf("%s: unlink dd/xx/ff succeeded!\n", s);
    exit(1);
  }
  if(unlink("dd/ff/ff") == 0){
    printf("%s: unlink dd/ff/ff succeeded!\n", s);
    exit(1);
  }
  if(chdir("dd/ff") == 0){
    printf("%s: chdir dd/ff succeeded!\n", s);
    exit(1);
  }
  if(chdir("dd/xx") == 0){
    printf("%s: chdir dd/xx succeeded!\n", s);
    exit(1);
  }

  if(unlink("dd/dd/ffff") != 0){
    printf("%s: unlink dd/dd/ff failed\n", s);
    exit(1);
  }
  if(unlink("dd/ff") != 0){
    printf("%s: unlink dd/ff failed\n", s);
    exit(1);
  }
  if(unlink("dd") == 0){
    printf("%s: unlink non-empty dd succeeded!\n", s);
    exit(1);
  }
  if(unlink("dd/dd") < 0){
    printf("%s: unlink dd/dd failed\n", s);
    exit(1);
  }
  if(unlink("dd") < 0){
    printf("%s: unlink dd failed\n", s);
    exit(1);
  }
}

// test writes that are larger than the log.

void
bigwrite(char *s)
{
  int fd, sz;

  unlink("bigwrite");
  for(sz = 499; sz < (MAXOPBLOCKS+2)*BSIZE; sz += 471){
    fd = open("bigwrite", O_CREATE | O_RDWR);
    if(fd < 0){
      printf("%s: cannot create bigwrite\n", s);
      exit(1);
    }
    int i;
    for(i = 0; i < 2; i++){
      int cc = write(fd, buf, sz);
      if(cc != sz){
        printf("%s: write(%d) ret %d\n", s, sz, cc);
        exit(1);
      }
    }
    close(fd);
    unlink("bigwrite");
  }
}



void
bigfile(char *s)
{
  enum { N = 20, SZ=600 };
  int fd, i, total, cc;

  unlink("bigfile.dat");
  fd = open("bigfile.dat", O_CREATE | O_RDWR);
  if(fd < 0){
    printf("%s: cannot create bigfile", s);
    exit(1);
  }
  for(i = 0; i < N; i++){
    memset(buf, i, SZ);
    if(write(fd, buf, SZ) != SZ){
      printf("%s: write bigfile failed\n", s);
      exit(1);
    }
  }
  close(fd);

  fd = open("bigfile.dat", 0);
  if(fd < 0){
    printf("%s: cannot open bigfile\n", s);
    exit(1);
  }
  total = 0;
  for(i = 0; ; i++){
    cc = read(fd, buf, SZ/2);
    if(cc < 0){
      printf("%s: read bigfile failed\n", s);
      exit(1);
    }
    if(cc == 0)
      break;
    if(cc != SZ/2){
      printf("%s: short read bigfile\n", s);
      exit(1);
    }
    if(buf[0] != i/2 || buf[SZ/2-1] != i/2){
      printf("%s: read bigfile wrong data\n", s);
      exit(1);
    }
    total += cc;
  }
  close(fd);
  if(total != N*SZ){
    printf("%s: read bigfile wrong total\n", s);
    exit(1);
  }
  unlink("bigfile.dat");
}


void
fourteen(char *s)
{
  int fd;

  // DIRSIZ is 14.

  if(mkdir("12345678901234") != 0){
    printf("%s: mkdir 12345678901234 failed\n", s);
    exit(1);
  }
  if(mkdir("12345678901234/123456789012345") != 0){
    printf("%s: mkdir 12345678901234/123456789012345 failed\n", s);
    exit(1);
  }
  fd = open("123456789012345/123456789012345/123456789012345", O_CREATE);
  if(fd < 0){
    printf("%s: create 123456789012345/123456789012345/123456789012345 failed\n", s);
    exit(1);
  }
  close(fd);
  fd = open("12345678901234/12345678901234/12345678901234", 0);
  if(fd < 0){
    printf("%s: open 12345678901234/12345678901234/12345678901234 failed\n", s);
    exit(1);
  }
  close(fd);

  if(mkdir("12345678901234/12345678901234") == 0){
    printf("%s: mkdir 12345678901234/12345678901234 succeeded!\n", s);
    exit(1);
  }
  if(mkdir("123456789012345/12345678901234") == 0){
    printf("%s: mkdir 12345678901234/123456789012345 succeeded!\n", s);
    exit(1);
  }

  // clean up
  unlink("123456789012345/12345678901234");
  unlink("12345678901234/12345678901234");
  unlink("12345678901234/12345678901234/12345678901234");
  unlink("123456789012345/123456789012345/123456789012345");
  unlink("12345678901234/123456789012345");
  unlink("12345678901234");
}


void
rmdot(char *s)
{
  if(mkdir("dots") != 0){
    printf("%s: mkdir dots failed\n", s);
    exit(1);
  }
  if(chdir("dots") != 0){
    printf("%s: chdir dots failed\n", s);
    exit(1);
  }
  if(unlink(".") == 0){
    printf("%s: rm . worked!\n", s);
    exit(1);
  }
  if(unlink("..") == 0){
    printf("%s: rm .. worked!\n", s);
    exit(1);
  }
  if(chdir("/") != 0){
    printf("%s: chdir / failed\n", s);
    exit(1);
  }
  if(unlink("dots/.") == 0){
    printf("%s: unlink dots/. worked!\n", s);
    exit(1);
  }
  if(unlink("dots/..") == 0){
    printf("%s: unlink dots/.. worked!\n", s);
    exit(1);
  }
  if(unlink("dots") != 0){
    printf("%s: unlink dots failed!\n", s);
    exit(1);
  }
}


void
dirfile(char *s)
{
  int fd;

  fd = open("dirfile", O_CREATE);
  if(fd < 0){
    printf("%s: create dirfile failed\n", s);
    exit(1);
  }
  close(fd);
  if(chdir("dirfile") == 0){
    printf("%s: chdir dirfile succeeded!\n", s);
    exit(1);
  }
  fd = open("dirfile/xx", 0);
  if(fd >= 0){
    printf("%s: create dirfile/xx succeeded!\n", s);
    exit(1);
  }
  fd = open("dirfile/xx", O_CREATE);
  if(fd >= 0){
    printf("%s: create dirfile/xx succeeded!\n", s);
    exit(1);
  }
  if(mkdir("dirfile/xx") == 0){
    printf("%s: mkdir dirfile/xx succeeded!\n", s);
    exit(1);
  }
  if(unlink("dirfile/xx") == 0){
    printf("%s: unlink dirfile/xx succeeded!\n", s);
    exit(1);
  }
  if(link("README.md", "dirfile/xx") == 0){
    printf("%s: link to dirfile/xx succeeded!\n", s);
    exit(1);
  }
  if(unlink("dirfile") != 0){
    printf("%s: unlink dirfile failed!\n", s);
    exit(1);
  }

  fd = open(".", O_RDWR);
  if(fd >= 0){
    printf("%s: open . for writing succeeded!\n", s);
    exit(1);
  }
  fd = open(".", 0);
  if(write(fd, "x", 1) > 0){
    printf("%s: write . succeeded!\n", s);
    exit(1);
  }
  close(fd);
}

// test that iput() is called at the end of _namei().
// also tests empty file names.

void
iref(char *s)
{
  int i, fd;

  for(i = 0; i < NINODE + 1; i++){
    if(mkdir("irefd") != 0){
      printf("%s: mkdir irefd failed\n", s);
      exit(1);
    }
    if(chdir("irefd") != 0){
      printf("%s: chdir irefd failed\n", s);
      exit(1);
    }

    mkdir("");
    link("README.md", "");
    fd = open("", O_CREATE);
    if(fd >= 0)
      close(fd);
    fd = open("xx", O_CREATE);
    if(fd >= 0)
      close(fd);
    unlink("xx");
  }

  // clean up
  for(i = 0; i < NINODE + 1; i++){
    chdir("..");
    unlink("irefd");
  }

  chdir("/");
}

// test that fork fails gracefully
// the forktest binary also does this, but it runs out of proc entries first.
// inside the bigger usertests binary, we run out of memory first.

void
fsfull()
{
  int nfiles;
  int fsblocks = 0;

  printf("fsfull test\n");

  for(nfiles = 0; ; nfiles++){
    char name[64];
    name[0] = 'f';
    name[1] = '0' + nfiles / 1000;
    name[2] = '0' + (nfiles % 1000) / 100;
    name[3] = '0' + (nfiles % 100) / 10;
    name[4] = '0' + (nfiles % 10);
    name[5] = '\0';
    printf("writing %s\n", name);
    int fd = open(name, O_CREATE|O_RDWR);
    if(fd < 0){
      printf("open %s failed\n", name);
      break;
    }
    int total = 0;
    while(1){
      int cc = write(fd, buf, BSIZE);
      if(cc < BSIZE)
        break;
      total += cc;
      fsblocks++;
    }
    printf("wrote %d bytes\n", total);
    close(fd);
    if(total == 0)
      break;
  }

  while(nfiles >= 0){
    char name[64];
    name[0] = 'f';
    name[1] = '0' + nfiles / 1000;
    name[2] = '0' + (nfiles % 1000) / 100;
    name[3] = '0' + (nfiles % 100) / 10;
    name[4] = '0' + (nfiles % 10);
    name[5] = '\0';
    unlink(name);
    nfiles--;
  }

  printf("fsfull test finished\n");
}


void
bigdir(char *s)
{
  enum { N = 500 };
  int i, fd;
  char name[10];

  unlink("bd");

  fd = open("bd", O_CREATE);
  if(fd < 0){
    printf("%s: bigdir create failed\n", s);
    exit(1);
  }
  close(fd);

  for(i = 0; i < N; i++){
    name[0] = 'x';
    name[1] = '0' + (i / 64);
    name[2] = '0' + (i % 64);
    name[3] = '\0';
    if(link("bd", name) != 0){
      printf("%s: bigdir i=%d link(bd, %s) failed\n", s, i, name);
      exit(1);
    }
  }

  unlink("bd");
  for(i = 0; i < N; i++){
    name[0] = 'x';
    name[1] = '0' + (i / 64);
    name[2] = '0' + (i % 64);
    name[3] = '\0';
    if(unlink(name) != 0){
      printf("%s: bigdir unlink failed", s);
      exit(1);
    }
  }
}

// concurrent writes to try to provoke deadlock in the virtio disk
// driver.

void
manywrites(char *s)
{
  int nchildren = 4;
  int howmany = 30; // increase to look for deadlock

  for(int ci = 0; ci < nchildren; ci++){
    int pid = fork();
    if(pid < 0){
      printf("fork failed\n");
      exit(1);
    }

    if(pid == 0){
      char name[3];
      name[0] = 'b';
      name[1] = 'a' + ci;
      name[2] = '\0';
      unlink(name);

      for(int iters = 0; iters < howmany; iters++){
        for(int i = 0; i < ci+1; i++){
          int fd = open(name, O_CREATE | O_RDWR);
          if(fd < 0){
            printf("%s: cannot create %s\n", s, name);
            exit(1);
          }
          int sz = sizeof(buf);
          int cc = write(fd, buf, sz);
          if(cc != sz){
            printf("%s: write(%d) ret %d\n", s, sz, cc);
            exit(1);
          }
          close(fd);
        }
        unlink(name);
      }

      unlink(name);
      exit(0);
    }
  }

  for(int ci = 0; ci < nchildren; ci++){
    int st = 0;
    wait(&st);
    if(st != 0)
      exit(st);
  }
  exit(0);
}

// regression test. does write() with an invalid buffer pointer cause
// a block to be allocated for a file that is then not freed when the
// file is deleted? if the kernel has this bug, it will panic: balloc:
// out of blocks. assumed_free may need to be raised to be more than
// the number of free blocks. this test takes a long time.

void
badwrite(char *s)
{
  int assumed_free = 600;

  unlink("junk");
  for(int i = 0; i < assumed_free; i++){
    int fd = open("junk", O_CREATE|O_WRONLY);
    if(fd < 0){
      printf("open junk failed\n");
      exit(1);
    }
    write(fd, (char*)0xffffffffffL, 1);
    close(fd);
    unlink("junk");
  }

  int fd = open("junk", O_CREATE|O_WRONLY);
  if(fd < 0){
    printf("open junk failed\n");
    exit(1);
  }
  if(write(fd, "x", 1) != 1){
    printf("write failed\n");
    exit(1);
  }
  close(fd);
  unlink("junk");

  exit(0);
}

// test the exec() code that cleans up if it runs out
// of memory. it's really a test that such a condition
// doesn't cause a panic.

void
diskfull(char *s)
{
  int fi;
  int done = 0;

  unlink("diskfulldir");

  for(fi = 0; done == 0 && '0' + fi < 0177; fi++){
    char name[32];
    name[0] = 'b';
    name[1] = 'i';
    name[2] = 'g';
    name[3] = '0' + fi;
    name[4] = '\0';
    unlink(name);
    int fd = open(name, O_CREATE|O_RDWR|O_TRUNC);
    if(fd < 0){
      // oops, ran out of inodes before running out of blocks.
      printf("%s: could not create file %s\n", s, name);
      done = 1;
      break;
    }
    for(int i = 0; i < MAXFILE; i++){
      char buf[BSIZE];
      if(write(fd, buf, BSIZE) != BSIZE){
        done = 1;
        close(fd);
        break;
      }
    }
    close(fd);
  }

  // now that there are no free blocks, test that dirlink()
  // merely fails (doesn't panic) if it can't extend
  // directory content. one of these file creations
  // is expected to fail.
  int nzz = 128;
  for(int i = 0; i < nzz; i++){
    char name[32];
    name[0] = 'z';
    name[1] = 'z';
    name[2] = '0' + (i / 32);
    name[3] = '0' + (i % 32);
    name[4] = '\0';
    unlink(name);
    int fd = open(name, O_CREATE|O_RDWR|O_TRUNC);
    if(fd < 0)
      break;
    close(fd);
  }

  // this mkdir() is expected to fail.
  if(mkdir("diskfulldir") == 0)
    printf("%s: mkdir(diskfulldir) unexpectedly succeeded!\n", s);

  unlink("diskfulldir");

  for(int i = 0; i < nzz; i++){
    char name[32];
    name[0] = 'z';
    name[1] = 'z';
    name[2] = '0' + (i / 32);
    name[3] = '0' + (i % 32);
    name[4] = '\0';
    unlink(name);
  }

  for(int i = 0; '0' + i < 0177; i++){
    char name[32];
    name[0] = 'b';
    name[1] = 'i';
    name[2] = 'g';
    name[3] = '0' + i;
    name[4] = '\0';
    unlink(name);
  }
}


void
outofinodes(char *s)
{
  int nzz = 32*32;
  for(int i = 0; i < nzz; i++){
    char name[32];
    name[0] = 'z';
    name[1] = 'z';
    name[2] = '0' + (i / 32);
    name[3] = '0' + (i % 32);
    name[4] = '\0';
    unlink(name);
    int fd = open(name, O_CREATE|O_RDWR|O_TRUNC);
    if(fd < 0){
      // failure is eventually expected.
      break;
    }
    close(fd);
  }

  for(int i = 0; i < nzz; i++){
    char name[32];
    name[0] = 'z';
    name[1] = 'z';
    name[2] = '0' + (i / 32);
    name[3] = '0' + (i % 32);
    name[4] = '\0';
    unlink(name);
  }
}

// 第二磁盘通过 /disk1 挂载，可读、可执行、可写。
void
disk1_read(char *s)
{
  struct stat st;
  int fd, n;

  if(stat("/disk1/README.md", &st) < 0 || st.size <= 0){
    printf("%s: stat /disk1 failed\n", s);
    exit(1);
  }
  if((fd = open("/disk1/README.md", O_RDONLY)) < 0){
    printf("%s: open /disk1 failed\n", s);
    exit(1);
  }
  n = read(fd, buf, 32);
  close(fd);
  if(n <= 0){
    printf("%s: read /disk1 failed\n", s);
    exit(1);
  }
  int wfd = open("/disk1/newfile", O_CREATE|O_WRONLY);
  if(wfd < 0 || write(wfd, "disk1", 5) != 5){
    printf("%s: write /disk1 failed\n", s);
    exit(1);
  }
  close(wfd);
  wfd = open("/disk1/newfile", O_RDONLY);
  if(wfd < 0 || read(wfd, buf, 16) != 5 || buf[0] != 'd'){
    printf("%s: reread /disk1 failed\n", s);
    exit(1);
  }
  close(wfd);
  if(unlink("/disk1/newfile") < 0){
    printf("%s: unlink /disk1 failed\n", s);
    exit(1);
  }

  int pid = fork();
  if(pid < 0){
    printf("%s: fork failed\n", s);
    exit(1);
  }
  if(pid == 0){
    char *argv[] = { "/disk1/echo", "disk1", 0 };
    exec("/disk1/echo", argv);
    exit(1);
  }
  if(wait(&n) != pid || n != 0){
    printf("%s: exec from /disk1 failed\n", s);
    exit(1);
  }
  exit(0);
}


struct test fs_quicktests[] = {
  {truncate1, "truncate1"},
  {truncate2, "truncate2"},
  {truncate3, "truncate3"},
  {iputtest, "iputtest"},
  {exitiputtest, "exitiputtest"},
  {openiputtest, "openiputtest"},
  {opentest, "opentest"},
  {writetest, "writetest"},
  {writebig, "writebig"},
  {createtest, "createtest"},
  {dirtest, "dirtest"},
  {sharedfd, "sharedfd"},
  {fourfiles, "fourfiles"},
  {createdelete, "createdelete"},
  {unlinkread, "unlinkread"},
  {linktest, "linktest"},
  {concreate, "concreate"},
  {linkunlink, "linkunlink"},
  {subdir, "subdir"},
  {bigwrite, "bigwrite"},
  {bigfile, "bigfile"},
  {fourteen, "fourteen"},
  {rmdot, "rmdot"},
  {dirfile, "dirfile"},
  {iref, "iref"},
  {disk1_read, "disk1_read"},
  { 0, 0},
};
struct test fs_slowtests[] = {
  {bigdir, "bigdir"},
  {manywrites, "manywrites"},
  {badwrite, "badwrite"},
  {diskfull, "diskfull"},
  {outofinodes, "outofinodes"},
  { 0, 0},
};
