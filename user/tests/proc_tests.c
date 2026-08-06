// 进程、fork、wait、exec 与信号相关测试。
//
// 每个测试函数由 usertests 驱动在独立子进程中运行。
#include "tests.h"
#include "kernel/module/module_ids.h"

void
exectest(char *s)
{
  int fd, xstatus, pid;
  char *echoargv[] = { "echo", "OK", 0 };
  char buf[3];

  unlink("echo-ok");
  pid = fork();
  if(pid < 0) {
     printf("%s: fork failed\n", s);
     exit(1);
  }
  if(pid == 0) {
    close(1);
    fd = open("echo-ok", O_CREATE|O_WRONLY);
    if(fd < 0) {
      printf("%s: create failed\n", s);
      exit(1);
    }
    if(fd != 1) {
      printf("%s: wrong fd\n", s);
      exit(1);
    }
    if(exec("echo", echoargv) < 0){
      printf("%s: exec echo failed\n", s);
      exit(1);
    }
    // won't get to here
  }
  if (wait(&xstatus) != pid) {
    printf("%s: wait failed!\n", s);
  }
  if(xstatus != 0)
    exit(xstatus);

  fd = open("echo-ok", O_RDONLY);
  if(fd < 0) {
    printf("%s: open failed\n", s);
    exit(1);
  }
  if (read(fd, buf, 2) != 2) {
    printf("%s: read failed\n", s);
    exit(1);
  }
  unlink("echo-ok");
  if(buf[0] == 'O' && buf[1] == 'K')
    exit(0);
  else {
    printf("%s: wrong output\n", s);
    exit(1);
  }

}

// simple fork and pipe read/write


void
pipe1(char *s)
{
  int fds[2], pid, xstatus;
  int seq, i, n, cc, total;
  enum { N=5, SZ=1033 };

  if(pipe(fds) != 0){
    printf("%s: pipe() failed\n", s);
    exit(1);
  }
  pid = fork();
  seq = 0;
  if(pid == 0){
    close(fds[0]);
    for(n = 0; n < N; n++){
      for(i = 0; i < SZ; i++)
        buf[i] = seq++;
      if(write(fds[1], buf, SZ) != SZ){
        printf("%s: pipe1 oops 1\n", s);
        exit(1);
      }
    }
    exit(0);
  } else if(pid > 0){
    close(fds[1]);
    total = 0;
    cc = 1;
    while((n = read(fds[0], buf, cc)) > 0){
      for(i = 0; i < n; i++){
        if((buf[i] & 0xff) != (seq++ & 0xff)){
          printf("%s: pipe1 oops 2\n", s);
          return;
        }
      }
      total += n;
      cc = cc * 2;
      if(cc > sizeof(buf))
        cc = sizeof(buf);
    }
    if(total != N * SZ){
      printf("%s: pipe1 oops 3 total %d\n", s, total);
      exit(1);
    }
    close(fds[0]);
    wait(&xstatus);
    exit(xstatus);
  } else {
    printf("%s: fork() failed\n", s);
    exit(1);
  }
}


// test if child is killed (status = -1)

void
killstatus(char *s)
{
  int xst;

  for(int i = 0; i < 100; i++){
    int pid1 = fork();
    if(pid1 < 0){
      printf("%s: fork failed\n", s);
      exit(1);
    }
    if(pid1 == 0){
      while(1) {
        getpid();
      }
      exit(0);
    }
    pause(1);
    kill(pid1);
    wait(&xst);
    if(xst != -1) {
       printf("%s: status should be -1\n", s);
       exit(1);
    }
  }
  exit(0);
}

// meant to be run w/ at most two CPUs

void
preempt(char *s)
{
  int pid1, pid2, pid3;
  int pfds[2];

  pid1 = fork();
  if(pid1 < 0) {
    printf("%s: fork failed", s);
    exit(1);
  }
  if(pid1 == 0)
    for(;;)
      ;

  pid2 = fork();
  if(pid2 < 0) {
    printf("%s: fork failed\n", s);
    exit(1);
  }
  if(pid2 == 0)
    for(;;)
      ;

  pipe(pfds);
  pid3 = fork();
  if(pid3 < 0) {
     printf("%s: fork failed\n", s);
     exit(1);
  }
  if(pid3 == 0){
    close(pfds[0]);
    if(write(pfds[1], "x", 1) != 1)
      printf("%s: preempt write error", s);
    close(pfds[1]);
    for(;;)
      ;
  }

  close(pfds[1]);
  if(read(pfds[0], buf, sizeof(buf)) != 1){
    printf("%s: preempt read error", s);
    return;
  }
  close(pfds[0]);
  printf("kill... ");
  kill(pid1);
  kill(pid2);
  kill(pid3);
  printf("wait... ");
  wait(0);
  wait(0);
  wait(0);
}

// try to find any races between exit and wait

void
exitwait(char *s)
{
  int i, pid;

  for(i = 0; i < 100; i++){
    pid = fork();
    if(pid < 0){
      printf("%s: fork failed\n", s);
      exit(1);
    }
    if(pid){
      int xstate;
      if(wait(&xstate) != pid){
        printf("%s: wait wrong pid\n", s);
        exit(1);
      }
      if(i != xstate) {
        printf("%s: wait wrong exit status\n", s);
        exit(1);
      }
    } else {
      exit(i);
    }
  }
}

// try to find races in the reparenting
// code that handles a parent exiting
// when it still has live children.

void
reparent(char *s)
{
  int master_pid = getpid();
  for(int i = 0; i < 200; i++){
    int pid = fork();
    if(pid < 0){
      printf("%s: fork failed\n", s);
      exit(1);
    }
    if(pid){
      if(wait(0) != pid){
        printf("%s: wait wrong pid\n", s);
        exit(1);
      }
    } else {
      int pid2 = fork();
      if(pid2 < 0){
        kill(master_pid);
        exit(1);
      }
      exit(0);
    }
  }
  exit(0);
}

// what if two children exit() at the same time?

void
twochildren(char *s)
{
  for(int i = 0; i < 1000; i++){
    int pid1 = fork();
    if(pid1 < 0){
      printf("%s: fork failed\n", s);
      exit(1);
    }
    if(pid1 == 0){
      exit(0);
    } else {
      int pid2 = fork();
      if(pid2 < 0){
        printf("%s: fork failed\n", s);
        exit(1);
      }
      if(pid2 == 0){
        exit(0);
      } else {
        wait(0);
        wait(0);
      }
    }
  }
}

// concurrent forks to try to expose locking bugs.

void
forkfork(char *s)
{
  enum { N=2 };

  for(int i = 0; i < N; i++){
    int pid = fork();
    if(pid < 0){
      printf("%s: fork failed", s);
      exit(1);
    }
    if(pid == 0){
      for(int j = 0; j < 200; j++){
        int pid1 = fork();
        if(pid1 < 0){
          exit(1);
        }
        if(pid1 == 0){
          exit(0);
        }
        wait(0);
      }
      exit(0);
    }
  }

  int xstatus;
  for(int i = 0; i < N; i++){
    wait(&xstatus);
    if(xstatus != 0) {
      printf("%s: fork in child failed", s);
      exit(1);
    }
  }
}


void
forkforkfork(char *s)
{
  unlink("stopforking");

  int pid = fork();
  if(pid < 0){
    printf("%s: fork failed", s);
    exit(1);
  }
  if(pid == 0){
    while(1){
      int fd = open("stopforking", 0);
      if(fd >= 0){
        exit(0);
      }
      if(fork() < 0){
        close(open("stopforking", O_CREATE|O_RDWR));
      }
    }

    exit(0);
  }

  pause(20); // two seconds
  close(open("stopforking", O_CREATE|O_RDWR));
  wait(0);
  pause(10); // one second
}

// regression test. does reparent() violate the parent-then-child
// locking order when giving away a child to init, so that exit()
// deadlocks against init's wait()? also used to trigger a "panic:
// release" due to exit() releasing a different p->parent->lock than
// it acquired.

void
reparent2(char *s)
{
  for(int i = 0; i < 800; i++){
    int pid1 = fork();
    if(pid1 < 0){
      printf("fork failed\n");
      exit(1);
    }
    if(pid1 == 0){
      fork();
      fork();
      exit(0);
    }
    wait(0);
  }

  exit(0);
}

// allocate all mem, free it, and allocate again

void
forktest(char *s)
{
  enum{ N = 1000 };
  int n, pid;

  for(n=0; n<N; n++){
    pid = fork();
    if(pid < 0)
      break;
    if(pid == 0)
      exit(0);
  }

  if (n == 0) {
    printf("%s: no fork at all!\n", s);
    exit(1);
  }

  if(n == N){
    printf("%s: fork claimed to work 1000 times!\n", s);
    exit(1);
  }

  for(; n > 0; n--){
    if(wait(0) < 0){
      printf("%s: wait stopped early\n", s);
      exit(1);
    }
  }

  if(wait(0) != -1){
    printf("%s: wait got too many\n", s);
    exit(1);
  }
}


void
stacktest(char *s)
{
  int pid;
  int xstatus;

  pid = fork();
  if(pid == 0) {
    char *sp = (char *) r_sp();
    sp -= USERSTACK*PGSIZE;
    // the *sp should cause a trap.
    printf("%s: stacktest: read below stack %d\n", s, *sp);
    exit(1);
  } else if(pid < 0){
    printf("%s: fork failed\n", s);
    exit(1);
  }
  wait(&xstatus);
  if(xstatus == -1)  // kernel killed child?
    exit(0);
  else
    exit(xstatus);
}

// check that writes to a few forbidden addresses
// cause a fault, e.g. process's text and TRAMPOLINE.

void
nowrite(char *s)
{
  int pid;
  int xstatus;
  uint64 addrs[] = { 0, 0x80000000LL, 0x3fffffe000, 0x3ffffff000, 0x4000000000,
                     0xffffffffffffffff };

  for(int ai = 0; ai < sizeof(addrs)/sizeof(addrs[0]); ai++){
    pid = fork();
    if(pid == 0) {
      volatile int *addr = (int *) addrs[ai];
      *addr = 10;
      printf("%s: write to %p did not fail!\n", s, addr);
      exit(0);
    } else if(pid < 0){
      printf("%s: fork failed\n", s);
      exit(1);
    }
    wait(&xstatus);
    if(xstatus == 0){
      // kernel did not kill child!
      exit(1);
    }
  }
  exit(0);
}

void
execout(char *s)
{
  for(int avail = 0; avail < 15; avail++){
    int pid = fork();
    if(pid < 0){
      printf("fork failed\n");
      exit(1);
    } else if(pid == 0){
      // allocate all of memory.
      while(1){
        char *a = sbrk(PGSIZE);
        if(a == SBRK_ERROR)
          break;
        *(a + PGSIZE - 1) = 1;
      }

      // free a few pages, in order to let exec() make some
      // progress.
      for(int i = 0; i < avail; i++)
        sbrk(-PGSIZE);

      close(1);
      char *args[] = { "echo", "x", 0 };
      exec("echo", args);
      exit(0);
    } else {
      wait((int*)0);
    }
  }

  exit(0);
}

static int
parse_int_at(char *buf, int i, int n)
{
  int v = 0;

  while(i < n && buf[i] >= '0' && buf[i] <= '9'){
    v = v * 10 + buf[i] - '0';
    i++;
  }
  return v;
}

static int
find_proc_q(char *buf, int n, int pid, int *q)
{
  for(int i = 0; i + 4 < n; i++){
    if(buf[i] != 'p' || buf[i+1] != 'i' || buf[i+2] != 'd' || buf[i+3] != ' ')
      continue;
    int p = parse_int_at(buf, i + 4, n);
    if(p != pid)
      continue;
    for(int k = i; k + 3 < n; k++){
      if(buf[k] == ' ' && buf[k+1] == 'q' && buf[k+2] == ' '){
        *q = parse_int_at(buf, k + 3, n);
        return 1;
      }
    }
    return 0;
  }
  return 0;
}

// 周期性提升应按静态优先级重新计算队列，而不是把所有进程重置到队列 0。
void
prio_boost(char *s)
{
  int me = getpid();
  char *buf = malloc(4096);
  int n, q;

  if(buf == 0){
    printf("%s: malloc failed\n", s);
    exit(1);
  }
  if(setpriority(me, 200) < 0){
    printf("%s: setpriority failed\n", s);
    free(buf);
    exit(1);
  }
  // 跨越 100 tick 的周期性提升窗口。
  if(pause(120) < 0){
    printf("%s: pause failed\n", s);
    free(buf);
    exit(1);
  }
  n = (int)module_call(KMOD_SYSINFO, SYSINFO_CMD_DUMP, (uint64)buf, 4096);
  if(n < 0 || !find_proc_q(buf, n, me, &q)){
    printf("%s: cannot find proc qlevel\n", s);
    free(buf);
    exit(1);
  }
  if(q != 2){
    printf("%s: boost reset priority queue to %d\n", s, q);
    free(buf);
    exit(1);
  }
  free(buf);
  exit(0);
}

static int clone_shared;

static int sync_counter;
static int sync_mutex;
static int sync_tid;
static int sync_workers;
static int tgid_child_pid;
static int tgid_child_tid;
static int shared_fd;
static int join_val1;
static int join_val2;
static uint64 tls_child_val;

static void
group_worker(void *arg)
{
  for(;;)
    pause(1000);
}

static void
join_worker1(void *arg)
{
  join_val1 = 1;
}

static void
join_worker2(void *arg)
{
  join_val2 = 2;
}

static void
tls_worker(void *arg)
{
  set_tls(0x2222);
  tls_child_val = get_tls();
}

static inline int
sync_atomic_swap(int *p, int v)
{
  int prev;

  asm volatile("amoswap.w.aq %0, %1, (%2)"
               : "=r"(prev) : "r"(v), "r"(p) : "memory");
  return prev;
}

static void
sync_worker(void *arg)
{
  sync_workers++;
  sync_tid = gettid();
  for(int i = 0; i < 500; i++){
    while(sync_atomic_swap(&sync_mutex, 1) != 0)
      futex_wait((uint64)&sync_mutex, 1);
    sync_counter++;
    sync_atomic_swap(&sync_mutex, 0);
    futex_wake((uint64)&sync_mutex, 1);
  }
}

static void
tgid_worker(void *arg)
{
  tgid_child_pid = getpid();
  tgid_child_tid = gettid();
}

static void
files_worker(void *arg)
{
  close(shared_fd);
}

static void
cwd_worker(void *arg)
{
  if(chdir("cdir") < 0)
    exit(1);
}

// clone + futex：两个线程通过共享 mutex 保护计数器，最终结果必须无丢失更新。
void
clone_sync(char *s)
{
  char *s1 = sbrk(PGSIZE);
  char *s2 = sbrk(PGSIZE);
  int p1, p2;

  if(s1 == SBRK_ERROR || s2 == SBRK_ERROR){
    printf("%s: sbrk stacks failed\n", s);
    exit(1);
  }
  sync_counter = 0;
  sync_mutex = 0;
  sync_tid = 0;
  sync_workers = 0;

  p1 = thread_create(sync_worker, 0, s1 + PGSIZE);
  p2 = thread_create(sync_worker, 0, s2 + PGSIZE);
  if(p1 < 0 || p2 < 0){
    printf("%s: clone failed\n", s);
    exit(1);
  }

  if(wait(0) < 0 || wait(0) < 0){
    printf("%s: clone wait failed\n", s);
    exit(1);
  }
  if(sync_counter != 1000){
    printf("%s: counter %d workers %d, expected 1000/2\n",
           s, sync_counter, sync_workers);
    exit(1);
  }
  if(sync_tid == getpid()){
    printf("%s: gettid did not return child id\n", s);
    exit(1);
  }
  if(sync_workers != 2){
    printf("%s: workers %d, expected 2\n", s, sync_workers);
    exit(1);
  }
  sbrk(-2 * PGSIZE);
  exit(0);
}

// tgkill：精确终止同组指定 tid，不影响父线程。
void
clone_tgkill(char *s)
{
  int parent_tgid = getpid();
  char *stack = sbrk(PGSIZE);
  int pid, status;

  if(stack == SBRK_ERROR){
    printf("%s: sbrk stack failed\n", s);
    exit(1);
  }
  pid = thread_create(group_worker, 0, stack + PGSIZE);
  if(pid < 0 || tgkill(parent_tgid, pid, 9) < 0){
    printf("%s: tgkill failed\n", s);
    exit(1);
  }
  if(waitpid(pid, &status) != pid){
    printf("%s: tgkill wait failed\n", s);
    exit(1);
  }
  if(getpid() != parent_tgid){
    printf("%s: parent thread was killed\n", s);
    exit(1);
  }
  sbrk(-PGSIZE);
  exit(0);
}

// TLS：每个 clone 线程拥有独立的 tp 指针。
void
clone_tls(char *s)
{
  char *stack = sbrk(PGSIZE);
  int pid;

  if(stack == SBRK_ERROR){
    printf("%s: sbrk stack failed\n", s);
    exit(1);
  }
  if(set_tls(0x1111) < 0){
    printf("%s: set_tls failed\n", s);
    exit(1);
  }
  pid = thread_create(tls_worker, 0, stack + PGSIZE);
  if(pid < 0 || waitpid(pid, 0) != pid){
    printf("%s: clone_tls wait failed\n", s);
    exit(1);
  }
  if(get_tls() != 0x1111){
    printf("%s: parent tls changed\n", s);
    exit(1);
  }
  if(tls_child_val != 0x2222){
    printf("%s: child tls not independent\n", s);
    exit(1);
  }
  sbrk(-PGSIZE);
  exit(0);
}

// waitpid：父线程按 tid 精确等待并回收指定 clone 线程。
void
clone_join(char *s)
{
  char *s1 = sbrk(PGSIZE);
  char *s2 = sbrk(PGSIZE);
  int p1, p2, st1, st2;

  if(s1 == SBRK_ERROR || s2 == SBRK_ERROR){
    printf("%s: sbrk stacks failed\n", s);
    exit(1);
  }
  join_val1 = 0;
  join_val2 = 0;
  p1 = thread_create(join_worker1, 0, s1 + PGSIZE);
  p2 = thread_create(join_worker2, 0, s2 + PGSIZE);
  if(p1 < 0 || p2 < 0){
    printf("%s: clone failed\n", s);
    exit(1);
  }
  if(waitpid(p1, &st1) != p1 || st1 != 0 ||
     waitpid(p2, &st2) != p2 || st2 != 0){
    printf("%s: waitpid failed\n", s);
    exit(1);
  }
  if(join_val1 != 1 || join_val2 != 2){
    printf("%s: join workers did not run\n", s);
    exit(1);
  }
  sbrk(-2 * PGSIZE);
  exit(0);
}

// 线程组退出：组长 exit 时同 tgid 的线程也应被终止并回收。
void
clone_group_exit(char *s)
{
  uint64 before = module_call(KMOD_SYSINFO, SYSINFO_CMD_PROC, 0, 0);
  int pid = fork();
  int status;

  if(pid < 0){
    printf("%s: fork failed\n", s);
    exit(1);
  }
  if(pid == 0){
    char *stack = sbrk(PGSIZE);
    if(stack == SBRK_ERROR || thread_create(group_worker, 0, stack + PGSIZE) < 0)
      exit(1);
    exit(0);
  }
  if(wait(&status) != pid || status != 0){
    printf("%s: group leader wait failed\n", s);
    exit(1);
  }
  if(module_call(KMOD_SYSINFO, SYSINFO_CMD_PROC, 0, 0) > before){
    printf("%s: group threads not reclaimed\n", s);
    exit(1);
  }
  exit(0);
}

// clone 线程共享文件描述符表：子线程 close 后父进程同一 fd 也失效。
void
clone_files(char *s)
{
  char *stack = sbrk(PGSIZE);
  int pid;

  if(stack == SBRK_ERROR){
    printf("%s: sbrk stack failed\n", s);
    exit(1);
  }
  shared_fd = open("sharedfile", O_CREATE|O_RDWR);
  if(shared_fd < 0){
    printf("%s: open sharedfile failed\n", s);
    exit(1);
  }
  pid = thread_create(files_worker, 0, stack + PGSIZE);
  if(pid < 0 || wait(0) != pid){
    printf("%s: clone_files wait failed\n", s);
    exit(1);
  }
  if(close(shared_fd) != -1){
    printf("%s: file table not shared\n", s);
    exit(1);
  }
  unlink("sharedfile");
  sbrk(-PGSIZE);
  exit(0);
}

// clone 线程共享 cwd：子线程 chdir 后父进程相对路径随之改变。
void
clone_cwd(char *s)
{
  char *stack = sbrk(PGSIZE);
  struct stat st;
  int pid, fd;

  if(stack == SBRK_ERROR){
    printf("%s: sbrk stack failed\n", s);
    exit(1);
  }
  unlink("/cdir");
  if(mkdir("/cdir") < 0){
    printf("%s: mkdir cdir failed\n", s);
    exit(1);
  }
  pid = thread_create(cwd_worker, 0, stack + PGSIZE);
  if(pid < 0 || wait(0) != pid){
    printf("%s: clone_cwd wait failed\n", s);
    exit(1);
  }
  fd = open("probe", O_CREATE|O_WRONLY);
  if(fd < 0){
    printf("%s: open probe failed\n", s);
    exit(1);
  }
  close(fd);
  if(stat("/cdir/probe", &st) < 0 || stat("/probe", &st) == 0){
    printf("%s: cwd not shared\n", s);
    exit(1);
  }
  unlink("/cdir/probe");
  chdir("/");
  unlink("/cdir");
  sbrk(-PGSIZE);
  exit(0);
}

// clone 线程共享 tgid：子线程 getpid() 等于父进程 tgid，gettid() 是新 tid。
void
clone_tgid(char *s)
{
  int parent_pid = getpid();
  int parent_tid = gettid();
  char *stack = sbrk(PGSIZE);
  int pid;

  if(stack == SBRK_ERROR){
    printf("%s: sbrk stack failed\n", s);
    exit(1);
  }
  if(parent_pid != parent_tid){
    printf("%s: normal process tgid mismatch\n", s);
    exit(1);
  }
  pid = thread_create(tgid_worker, 0, stack + PGSIZE);
  if(pid < 0){
    printf("%s: clone failed\n", s);
    exit(1);
  }
  if(wait(0) != pid){
    printf("%s: clone wait failed\n", s);
    exit(1);
  }
  if(tgid_child_pid != parent_pid){
    printf("%s: child tgid %d != parent %d\n", s, tgid_child_pid, parent_pid);
    exit(1);
  }
  if(tgid_child_tid == parent_tid || tgid_child_tid == 0){
    printf("%s: child tid not distinct\n", s);
    exit(1);
  }
  sbrk(-PGSIZE);
  exit(0);
}

static void
clone_worker(void *arg)
{
  clone_shared = 0x1234;
}

// clone：新线程共享父进程地址空间，写入对父进程可见。
void
clone_basic(char *s)
{
  char *stack = sbrk(PGSIZE);
  int pid;

  if(stack == SBRK_ERROR){
    printf("%s: sbrk stack failed\n", s);
    exit(1);
  }
  clone_shared = 0;
  pid = thread_create(clone_worker, 0, stack + PGSIZE);
  if(pid < 0){
    printf("%s: clone failed\n", s);
    exit(1);
  }
  if(wait(0) != pid){
    printf("%s: clone wait failed\n", s);
    exit(1);
  }
  if(clone_shared != 0x1234){
    printf("%s: shared memory not visible\n", s);
    exit(1);
  }
  sbrk(-PGSIZE);
  exit(0);
}

// can the kernel tolerate running out of disk space?

struct test proc_quicktests[] = {
  {exectest, "exectest"},
  {pipe1, "pipe1"},
  {clone_basic, "clone_basic"},
  {clone_sync, "clone_sync"},
  {clone_tgid, "clone_tgid"},
  {clone_files, "clone_files"},
  {clone_cwd, "clone_cwd"},
  {clone_join, "clone_join"},
  {clone_tls, "clone_tls"},
  {clone_tgkill, "clone_tgkill"},
  {clone_group_exit, "clone_group_exit"},
  {killstatus, "killstatus"},
  {preempt, "preempt"},
  {exitwait, "exitwait"},
  {reparent, "reparent"},
  {twochildren, "twochildren"},
  {forkfork, "forkfork"},
  {forkforkfork, "forkforkfork"},
  {reparent2, "reparent2"},
  {forktest, "forktest"},
  {stacktest, "stacktest"},
  {nowrite, "nowrite"},
  { 0, 0},
};
struct test proc_slowtests[] = {
  {execout, "execout"},
  {prio_boost, "prio_boost"},
  { 0, 0},
};
