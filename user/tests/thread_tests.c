// Thread-group semantics tests for clone flags, exit_group, fork and exec.
//
// Each test function runs in its own child process under the usertests driver.
#include "tests.h"

extern void clone_stub(void);

static int flags_files_fd;
static int flags_files_shared;
static int fs_only_fd;
static int vm_shared;
static int vm_child_pid;
static int mfork_result;
static int mfork_child_tgid;
static int mfork_child_tid;

static void
files_only_worker(void *arg)
{
  flags_files_shared = 0x55;
  if(close(flags_files_fd) < 0)
    exit(1);
}

// CLONE_FILES shares the fd table but not the address space.
void
clone_flags_files(char *s)
{
  char *stack = sbrk(PGSIZE);
  int pid, st;

  if(stack == SBRK_ERROR){
    printf("%s: sbrk failed\n", s);
    exit(1);
  }
  unlink("flagfile");
  flags_files_fd = open("flagfile", O_CREATE|O_RDWR);
  if(flags_files_fd < 0){
    printf("%s: open flagfile failed\n", s);
    exit(1);
  }
  flags_files_shared = 0;
  pid = clone(CLONE_FILES, (uint64)files_only_worker, 0,
              (uint64)(stack + PGSIZE), (uint64)clone_stub);
  if(pid < 0 || waitpid(pid, &st) != pid || st != 0){
    printf("%s: CLONE_FILES child failed\n", s);
    exit(1);
  }
  if(close(flags_files_fd) != -1){
    printf("%s: fd table not shared\n", s);
    exit(1);
  }
  if(flags_files_shared != 0){
    printf("%s: address space was shared\n", s);
    exit(1);
  }
  unlink("flagfile");
  sbrk(-PGSIZE);
  exit(0);
}

static void
fs_only_worker(void *arg)
{
  if(close(fs_only_fd) < 0)
    exit(1);
  if(chdir("fscdir") < 0)
    exit(1);
}

// CLONE_FS shares cwd but keeps a private fd table.
void
clone_flags_fs(char *s)
{
  char *stack = sbrk(PGSIZE);
  int pid, st, fd;
  struct stat stbuf;

  if(stack == SBRK_ERROR){
    printf("%s: sbrk failed\n", s);
    exit(1);
  }
  unlink("/fscdir");
  if(mkdir("/fscdir") < 0){
    printf("%s: mkdir fscdir failed\n", s);
    exit(1);
  }
  unlink("fsflag");
  fs_only_fd = open("fsflag", O_CREATE|O_RDWR);
  if(fs_only_fd < 0){
    printf("%s: open fsflag failed\n", s);
    exit(1);
  }
  pid = clone(CLONE_FS, (uint64)fs_only_worker, 0,
              (uint64)(stack + PGSIZE), (uint64)clone_stub);
  if(pid < 0 || waitpid(pid, &st) != pid || st != 0){
    printf("%s: CLONE_FS child failed\n", s);
    exit(1);
  }
  if(close(fs_only_fd) != 0){
    printf("%s: fd table was shared\n", s);
    exit(1);
  }
  fd = open("probe", O_CREATE|O_WRONLY);
  if(fd < 0){
    printf("%s: open probe failed\n", s);
    exit(1);
  }
  close(fd);
  if(stat("/fscdir/probe", &stbuf) < 0 || stat("/probe", &stbuf) == 0){
    printf("%s: cwd not shared\n", s);
    exit(1);
  }
  unlink("/fscdir/probe");
  chdir("/");
  unlink("/fscdir");
  unlink("fsflag");
  sbrk(-PGSIZE);
  exit(0);
}

static void
vm_worker(void *arg)
{
  vm_shared = 0x77;
  vm_child_pid = getpid();
}

// CLONE_VM shares the address space without joining the thread group.
void
clone_flags_vm(char *s)
{
  char *stack = sbrk(PGSIZE);
  int pid, st;

  if(stack == SBRK_ERROR){
    printf("%s: sbrk failed\n", s);
    exit(1);
  }
  vm_shared = 0;
  vm_child_pid = 0;
  pid = clone(CLONE_VM, (uint64)vm_worker, 0,
              (uint64)(stack + PGSIZE), (uint64)clone_stub);
  if(pid < 0 || waitpid(pid, &st) != pid || st != 0){
    printf("%s: CLONE_VM child failed\n", s);
    exit(1);
  }
  if(vm_shared != 0x77){
    printf("%s: address space not shared\n", s);
    exit(1);
  }
  if(vm_child_pid == 0 || vm_child_pid == getpid()){
    printf("%s: CLONE_VM child did not get a new tgid\n", s);
    exit(1);
  }
  sbrk(-PGSIZE);
  exit(0);
}

static void
mfork_worker(void *arg)
{
  int pid = fork();
  int st;

  if(pid < 0){
    mfork_result = -1;
    return;
  }
  if(pid == 0){
    mfork_child_tgid = getpid();
    mfork_child_tid = gettid();
    exit(0);
  }
  if(waitpid(pid, &st) != pid || st != 0){
    mfork_result = -1;
    return;
  }
  mfork_result = 1;
}

// fork from a non-leader thread copies only the calling thread.
void
clone_thread_fork(char *s)
{
  int pid = fork();
  int st;

  if(pid < 0){
    printf("%s: outer fork failed\n", s);
    exit(1);
  }
  if(pid == 0){
    char *stack = sbrk(PGSIZE);
    int tpid;

    if(stack == SBRK_ERROR)
      exit(1);
    tpid = thread_create(mfork_worker, 0, stack + PGSIZE);
    if(tpid < 0 || waitpid(tpid, &st) != tpid || st != 0)
      exit(1);
    if(mfork_result != 1 ||
       mfork_child_tgid != mfork_child_tid ||
       mfork_child_tgid == 0 ||
       mfork_child_tgid == getpid())
      exit(1);
    sbrk(-PGSIZE);
    exit(0);
  }
  if(wait(&st) != pid || st != 0){
    printf("%s: inner child failed\n", s);
    exit(1);
  }
  exit(0);
}

static void
exit_group_worker(void *arg)
{
  exit_group(7);
}

// exit_group terminates every thread and preserves the exit status.
void
exit_group_basic(char *s)
{
  int pid = fork();
  int st;

  if(pid < 0){
    printf("%s: fork failed\n", s);
    exit(1);
  }
  if(pid == 0){
    char *stack = sbrk(PGSIZE);

    if(stack == SBRK_ERROR || thread_create(exit_group_worker, 0,
                                            stack + PGSIZE) < 0)
      exit(1);
    for(;;)
      pause(1000);
  }
  if(wait(&st) < 0 || st != 7){
    printf("%s: exit_group status %d\n", s, st);
    exit(1);
  }
  exit(0);
}

static void
exec_loop_worker(void *arg)
{
  for(;;)
    pause(1000);
}

static void
exec_caller(void *arg)
{
  char *argv[] = { "echo", "OK", 0 };

  if(exec("echo", argv) < 0)
    exit(1);
  exit(0);
}

// exec from a thread terminates the other group threads, then runs echo.
void
exec_thread_cleanup(char *s)
{
  int pid, fd, st;

  unlink("exec-thread-ok");
  pid = fork();
  if(pid < 0){
    printf("%s: fork failed\n", s);
    exit(1);
  }
  if(pid == 0){
    char *s1 = sbrk(PGSIZE);
    char *s2 = sbrk(PGSIZE);

    if(s1 == SBRK_ERROR || s2 == SBRK_ERROR)
      exit(1);
    close(1);
    fd = open("exec-thread-ok", O_CREATE|O_WRONLY);
    if(fd != 1)
      exit(1);
    if(thread_create(exec_loop_worker, 0, s1 + PGSIZE) < 0 ||
       thread_create(exec_caller, 0, s2 + PGSIZE) < 0)
      exit(1);
    for(;;)
      pause(1000);
  }
  if(wait(&st) < 0 || st != 0){
    printf("%s: exec thread failed\n", s);
    exit(1);
  }
  char buf[3] = { 0 };
  fd = open("exec-thread-ok", O_RDONLY);
  if(fd < 0 || read(fd, buf, 2) != 2 || buf[0] != 'O' || buf[1] != 'K'){
    printf("%s: exec output missing\n", s);
    exit(1);
  }
  close(fd);
  unlink("exec-thread-ok");
  exit(0);
}

struct test thread_quicktests[] = {
  {clone_flags_files, "clone_flags_files"},
  {clone_flags_fs, "clone_flags_fs"},
  {clone_flags_vm, "clone_flags_vm"},
  {clone_thread_fork, "clone_thread_fork"},
  {exit_group_basic, "exit_group_basic"},
  {exec_thread_cleanup, "exec_thread_cleanup"},
  { 0, 0},
};

struct test thread_slowtests[] = {
  { 0, 0},
};
