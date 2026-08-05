// 文件权限与用户/组凭证测试。
//
// 每个测试函数由 usertests 驱动在独立子进程中运行。
#include "tests.h"

// 让子进程切换为指定 uid/gid；失败时直接退出。
static void
become(int uid, int gid)
{
  if(setgid(gid) < 0 || setuid(uid) < 0)
    exit(1);
}

static void
checkwait(char *s, int pid, int want)
{
  int st = 0;

  if(pid < 0 || wait(&st) < 0){
    printf("%s: wait failed\n", s);
    exit(1);
  }
  if(st != want){
    printf("%s: child status %d, wanted %d\n", s, st, want);
    exit(1);
  }
}

void
perm_credentials(char *s)
{
  if(getuid() != 0 || geteuid() != 0 || getgid() != 0 || getegid() != 0){
    printf("%s: init credentials are not root\n", s);
    exit(1);
  }
  if(umask(077) != 022 || umask(022) != 077){
    printf("%s: umask round trip failed\n", s);
    exit(1);
  }

  int pid = fork();
  if(pid == 0){
    become(88, 7);
    if(getuid() != 88 || geteuid() != 88 ||
       getgid() != 7 || getegid() != 7){
      printf("%s: credential switch failed\n", s);
      exit(1);
    }
    if(setuid(99) != -1 || setgid(8) != -1){
      printf("%s: non-root setuid/setgid should fail\n", s);
      exit(1);
    }
    exit(0);
  }
  checkwait(s, pid, 0);
}

void
perm_basic(char *s)
{
  struct stat st;

  unlink("permfile");
  int fd = open("permfile", O_CREATE|O_RDWR);
  if(fd < 0){
    printf("%s: create permfile failed\n", s);
    exit(1);
  }
  close(fd);

  if(stat("permfile", &st) < 0 || (st.mode & PERM_MASK) != 0644 ||
     st.uid != 0 || st.gid != 0){
    printf("%s: default mode/owner incorrect\n", s);
    exit(1);
  }

  if(chmod("permfile", 0600) < 0 || stat("permfile", &st) < 0 ||
     st.mode != 0600){
    printf("%s: chmod failed\n", s);
    exit(1);
  }
  if((fd = open("permfile", O_RDONLY)) < 0){
    printf("%s: owner read should succeed\n", s);
    exit(1);
  }
  close(fd);
  if((fd = open("permfile", O_WRONLY)) < 0){
    printf("%s: root write should succeed\n", s);
    exit(1);
  }
  close(fd);

  int pid = fork();
  if(pid == 0){
    become(1, 2);
    if(open("permfile", O_RDONLY) >= 0 || open("permfile", O_WRONLY) >= 0 ||
       open("permfile", O_RDWR) >= 0){
      printf("%s: other class should be denied\n", s);
      exit(1);
    }
    exit(0);
  }
  checkwait(s, pid, 0);

  if(chmod("permfile", 0444) < 0 || (fd = open("permfile", O_RDONLY)) < 0){
    printf("%s: root should bypass read-only mode\n", s);
    exit(1);
  }
  close(fd);
  if((fd = open("permfile", O_WRONLY)) < 0){
    printf("%s: root should bypass read-only mode\n", s);
    exit(1);
  }
  close(fd);

  pid = fork();
  if(pid == 0){
    become(1, 2);
    if((fd = open("permfile", O_RDONLY)) < 0){
      printf("%s: other read on 0444 should succeed\n", s);
      exit(1);
    }
    close(fd);
    if(open("permfile", O_WRONLY) >= 0){
      printf("%s: other write should fail\n", s);
      exit(1);
    }
    exit(0);
  }
  checkwait(s, pid, 0);

  if(chown("permfile", 5, 6) < 0 || stat("permfile", &st) < 0 ||
     st.uid != 5 || st.gid != 6){
    printf("%s: chown failed\n", s);
    exit(1);
  }

  pid = fork();
  if(pid == 0){
    become(1, 2);
    if(chown("permfile", 9, 8) != -1){
      printf("%s: non-root chown should fail\n", s);
      exit(1);
    }
    exit(0);
  }
  checkwait(s, pid, 0);

  if(chown("permfile", 0, 0) < 0 || unlink("permfile") < 0){
    printf("%s: cleanup failed\n", s);
    exit(1);
  }
}

// 分别验证 owner、group、other 三种权限匹配。
void
perm_classes(char *s)
{
  unlink("ownerfile");
  unlink("groupfile");
  unlink("otherfile");

  int fd = open("ownerfile", O_CREATE|O_RDWR);
  if(fd < 0){
    printf("%s: create ownerfile failed\n", s);
    exit(1);
  }
  close(fd);
  if(chmod("ownerfile", 0600) < 0 || chown("ownerfile", 1, 9) < 0){
    printf("%s: setup ownerfile failed\n", s);
    exit(1);
  }

  fd = open("groupfile", O_CREATE|O_RDWR);
  if(fd < 0){
    printf("%s: create groupfile failed\n", s);
    exit(1);
  }
  close(fd);
  if(chmod("groupfile", 0640) < 0 || chown("groupfile", 7, 2) < 0){
    printf("%s: setup groupfile failed\n", s);
    exit(1);
  }

  fd = open("otherfile", O_CREATE|O_RDWR);
  if(fd < 0){
    printf("%s: create otherfile failed\n", s);
    exit(1);
  }
  close(fd);
  if(chmod("otherfile", 0600) < 0 || chown("otherfile", 7, 9) < 0){
    printf("%s: setup otherfile failed\n", s);
    exit(1);
  }

  int pid = fork();
  if(pid == 0){
    become(1, 2);
    if((fd = open("ownerfile", O_RDONLY)) < 0){
      printf("%s: owner class should read\n", s);
      exit(1);
    }
    close(fd);
    if((fd = open("groupfile", O_RDONLY)) < 0){
      printf("%s: group class should read\n", s);
      exit(1);
    }
    close(fd);
    if(open("otherfile", O_RDONLY) >= 0){
      printf("%s: other class should be denied\n", s);
      exit(1);
    }
    exit(0);
  }
  checkwait(s, pid, 0);

  unlink("ownerfile");
  unlink("groupfile");
  unlink("otherfile");
}

// 目录权限影响路径解析与目录内创建。
void
perm_dir(char *s)
{
  unlink("permdir/permchild");
  unlink("permdir");

  if(mkdir("permdir") < 0 || chmod("permdir", 0700) < 0 ||
     chown("permdir", 1, 1) < 0){
    printf("%s: setup permdir failed\n", s);
    exit(1);
  }

  int pid = fork();
  if(pid == 0){
    become(1, 2);
    if(chdir("permdir") < 0){
      printf("%s: owner chdir should succeed\n", s);
      exit(1);
    }
    int fd = open("permchild", O_CREATE|O_RDWR);
    if(fd < 0){
      printf("%s: owner create in dir should succeed\n", s);
      exit(1);
    }
    close(fd);
    exit(0);
  }
  checkwait(s, pid, 0);

  pid = fork();
  if(pid == 0){
    become(2, 3);
    if(chdir("permdir") == 0){
      printf("%s: other chdir should fail\n", s);
      exit(1);
    }
    if(open("permdir/permchild", O_CREATE|O_RDWR) >= 0){
      printf("%s: other create in dir should fail\n", s);
      exit(1);
    }
    exit(0);
  }
  checkwait(s, pid, 0);

  if(unlink("permdir/permchild") < 0 || unlink("permdir") < 0){
    printf("%s: cleanup permdir failed\n", s);
    exit(1);
  }
}

// O_CREATE 打开已存在文件不应要求父目录写权限。
void
perm_create_existing(char *s)
{
  unlink("permce/existing");
  unlink("permce");

  if(mkdir("permce") < 0 || chown("permce", 1, 1) < 0){
    printf("%s: setup permce failed\n", s);
    exit(1);
  }
  int fd = open("permce/existing", O_CREATE|O_RDWR);
  if(fd < 0){
    printf("%s: create existing file failed\n", s);
    exit(1);
  }
  close(fd);
  if(chmod("permce", 0500) < 0){
    printf("%s: chmod permce failed\n", s);
    exit(1);
  }

  int pid = fork();
  if(pid == 0){
    become(1, 2);
    if((fd = open("permce/existing", O_CREATE|O_RDONLY)) < 0){
      printf("%s: create on existing should ignore dir write\n", s);
      exit(1);
    }
    close(fd);
    if(open("permce/newfile", O_CREATE|O_RDWR) >= 0){
      printf("%s: create new file in no-write dir should fail\n", s);
      exit(1);
    }
    exit(0);
  }
  checkwait(s, pid, 0);

  if(unlink("permce/existing") < 0 || unlink("permce") < 0){
    printf("%s: cleanup permce failed\n", s);
    exit(1);
  }
}

// 执行权限：非 root 无执行位时 exec 失败，有 other 执行位时成功。
void
perm_exec(char *s)
{
  if(chmod("permexec", 0700) < 0){
    printf("%s: chmod permexec failed\n", s);
    exit(1);
  }

  int pid = fork();
  if(pid == 0){
    char *argv[] = { "permexec", 0 };
    become(1, 2);
    if(exec("permexec", argv) != -1){
      printf("%s: exec without permission should fail\n", s);
      exit(1);
    }
    exit(0);
  }
  checkwait(s, pid, 0);

  if(chmod("permexec", 0711) < 0){
    printf("%s: chmod permexec executable failed\n", s);
    exit(1);
  }

  pid = fork();
  if(pid == 0){
    char *argv[] = { "permexec", 0 };
    become(1, 2);
    exec("permexec", argv);
    exit(0);
  }
  checkwait(s, pid, 42);

  if(chmod("permexec", 0755) < 0){
    printf("%s: restore permexec mode failed\n", s);
    exit(1);
  }
}

struct test perm_quicktests[] = {
  {perm_credentials, "perm_credentials"},
  {perm_basic, "perm_basic"},
  {perm_classes, "perm_classes"},
  {perm_dir, "perm_dir"},
  {perm_create_existing, "perm_create_existing"},
  {perm_exec, "perm_exec"},
  { 0, 0},
};

struct test perm_slowtests[] = {
  { 0, 0},
};
