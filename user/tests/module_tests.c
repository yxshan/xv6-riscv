// 内核模块功能测试，例如 FIFO 与 COW。
//
// 每个测试函数由 usertests 驱动在独立子进程中运行。
#include "tests.h"
#include "kernel/module/module_ids.h"
#include "kernel/signal.h"

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

// /dev/stats 写入后应重置全局统计，随后 module_call 只贡献一个 syscall。
void
stats_reset(char *s)
{
  int fd;
  uint64 total;

  fd = open("stats", O_RDWR);
  if(fd < 0){
    printf("%s: open stats failed\n", s);
    exit(1);
  }
  if(write(fd, "reset", 5) != 5){
    printf("%s: stats reset write failed\n", s);
    exit(1);
  }
  total = module_call(KMOD_TRACE, TRACE_CMD_TOTAL, 0, 0);
  close(fd);
  if(total > 2){
    printf("%s: stats reset did not clear counters: %d\n", s, (int)total);
    exit(1);
  }
  exit(0);
}

// /proc 使用小缓冲多次 read，应能完整读取且不产生错误。
void
proc_chunked(char *s)
{
  char buf[128];
  int fd, n, total = 0;

  fd = open("proc", O_RDONLY);
  if(fd < 0){
    printf("%s: open proc failed\n", s);
    exit(1);
  }
  while((n = read(fd, buf, sizeof(buf))) > 0)
    total += n;
  close(fd);
  if(n < 0 || total <= 0){
    printf("%s: proc chunked read failed\n", s);
    exit(1);
  }
  exit(0);
}

static int
count_pid_lines(char *buf, int n)
{
  int count = 0;

  for(int i = 0; i + 4 <= n; i++){
    if(buf[i] == 'p' && buf[i+1] == 'i' && buf[i+2] == 'd' && buf[i+3] == ' ')
      count++;
  }
  return count;
}

// 创建多个子进程后，procinfo 应列出新增进程，而不是在旧缓冲处截断。
void
procinfo_full(char *s)
{
  enum { NP = 12 };
  int pids[NP];
  int nkids = 0;
  char *buf = malloc(4096);
  int n, listed, st;
  int before = (int)module_call(KMOD_SYSINFO, SYSINFO_CMD_PROC, 0, 0);

  if(buf == 0){
    printf("%s: malloc failed\n", s);
    exit(1);
  }

  for(int i = 0; i < NP; i++){
    int pid = fork();
    if(pid < 0){
      for(int j = 0; j < nkids; j++)
        kill(pids[j]);
      for(int j = 0; j < nkids; j++)
        wait(&st);
      printf("%s: fork failed\n", s);
      exit(1);
    }
    if(pid == 0){
      pause(1000);
      exit(0);
    }
    pids[nkids++] = pid;
  }

  n = (int)module_call(KMOD_SYSINFO, SYSINFO_CMD_DUMP, (uint64)buf, 4096);
  if(n < 0){
    printf("%s: procinfo dump failed\n", s);
    free(buf);
    exit(1);
  }
  listed = count_pid_lines(buf, n);
  if(listed < before + NP){
    printf("%s: procinfo truncated: before=%d listed=%d\n", s, before, listed);
    free(buf);
    exit(1);
  }

  for(int i = 0; i < nkids; i++)
    kill(pids[i]);
  for(int i = 0; i < nkids; i++)
    wait(&st);
  free(buf);
  exit(0);
}

static int in_handler;
static int bad_reenter;
static int masked_delivered;
static int clone_sig_count;
static int clone_sig_seen;

static void
reenter_handler(int sig)
{
  if(in_handler)
    bad_reenter = 1;
  in_handler = 1;
  if(sig == SIGUSR1)
    sigkill(getpid(), SIGUSR2);
  in_handler = 0;
  sigreturn();
}

// 信号处理期间不应重入；处理函数主动发新信号时应留到 sigreturn 后交付。
void
signal_no_reenter(char *s)
{
  if(signal(SIGUSR1, (uint64)reenter_handler) < 0 ||
     signal(SIGUSR2, (uint64)reenter_handler) < 0){
    printf("%s: signal failed\n", s);
    exit(1);
  }
  if(sigkill(getpid(), SIGUSR1) < 0){
    printf("%s: sigkill failed\n", s);
    exit(1);
  }
  if(bad_reenter){
    printf("%s: signal reentered handler\n", s);
    exit(1);
  }
  exit(0);
}

// SIG_IGN 应清除待处理信号，进程继续正常运行。
void
signal_ignore(char *s)
{
  if(signal(SIGUSR1, SIG_IGN) < 0){
    printf("%s: signal failed\n", s);
    exit(1);
  }
  if(sigkill(getpid(), SIGUSR1) < 0){
    printf("%s: sigkill failed\n", s);
    exit(1);
  }
  pause(5);
  exit(0);
}

// SIG_DFL 的默认动作是终止进程，子进程不应以 0 状态退出。
void
signal_default(char *s)
{
  int pid, st;

  pid = fork();
  if(pid < 0){
    printf("%s: fork failed\n", s);
    exit(1);
  }
  if(pid == 0){
    if(signal(SIGUSR2, SIG_DFL) < 0)
      exit(1);
    if(sigkill(getpid(), SIGUSR2) < 0)
      exit(1);
    pause(100);
    exit(0);
  }
  if(wait(&st) != pid || st == 0){
    printf("%s: SIG_DFL did not terminate child\n", s);
    exit(1);
  }
  exit(0);
}

static void
mask_handler(int sig)
{
  masked_delivered++;
  sigreturn();
}

// sigprocmask：阻塞期间信号保持待处理，解除阻塞后在下一次返回用户态时投递。
void
sig_mask(char *s)
{
  uint64 set = 1UL << SIGUSR1;
  uint64 old;

  masked_delivered = 0;
  if(signal(SIGUSR1, (uint64)mask_handler) < 0){
    printf("%s: signal failed\n", s);
    exit(1);
  }
  if(sigprocmask(SIG_BLOCK, &set, 0) < 0){
    printf("%s: sigprocmask block failed\n", s);
    exit(1);
  }
  if(sigkill(getpid(), SIGUSR1) < 0){
    printf("%s: sigkill failed\n", s);
    exit(1);
  }
  pause(5);
  if(masked_delivered != 0){
    printf("%s: blocked signal delivered early\n", s);
    exit(1);
  }
  if(sigprocmask(SIG_UNBLOCK, &set, &old) < 0){
    printf("%s: sigprocmask unblock failed\n", s);
    exit(1);
  }
  if(masked_delivered != 1){
    printf("%s: unblocked signal not delivered\n", s);
    exit(1);
  }
  exit(0);
}

static void
clone_sig_handler(int sig)
{
  clone_sig_count++;
  clone_sig_seen = 1;
  sigreturn();
}

static void
clone_sig_worker(void *arg)
{
  while(!clone_sig_seen)
    pause(1);
}

// SIGSTOP / SIGCONT：子进程停止后 waitpid 返回停止状态，继续后可正常退出。
void
sig_stop_cont(char *s)
{
  int *shared = (int*)mmap(0, PGSIZE, PROT_READ|PROT_WRITE,
                           MAP_SHARED|MAP_ANONYMOUS, -1, 0);
  int pid, st;

  if(shared == (int*)MAP_FAILED){
    printf("%s: mmap failed\n", s);
    exit(1);
  }
  shared[0] = 0;
  shared[1] = 0;
  shared[2] = 0;
  pid = fork();
  if(pid < 0){
    printf("%s: fork failed\n", s);
    exit(1);
  }
  if(pid == 0){
    if(setpgid(0, 0) < 0)
      exit(1);
    shared[1] = 1;
    while(shared[2] == 0)
      pause(1);
    exit(0);
  }
  if(setpgid(pid, pid) < 0){
    kill(pid);
    wait(&st);
    printf("%s: setpgid failed\n", s);
    exit(1);
  }
  for(int i = 0; i < 100 && shared[1] == 0; i++)
    pause(1);
  if(shared[1] == 0){
    printf("%s: child not ready\n", s);
    exit(1);
  }
  if(killpg(pid, SIGSTOP) < 0){
    printf("%s: SIGSTOP failed\n", s);
    exit(1);
  }
  if(waitpid(pid, &st) != pid || !XV6_WIFSTOPPED(st)){
    printf("%s: waitpid did not report stopped\n", s);
    exit(1);
  }
  pause(5);
  shared[2] = 1;
  if(killpg(pid, SIGCONT) < 0){
    printf("%s: SIGCONT failed\n", s);
    exit(1);
  }
  if(waitpid(pid, &st) != pid){
    printf("%s: waitpid after cont failed\n", s);
    exit(1);
  }
  if(munmap((char*)shared, PGSIZE) < 0){
    printf("%s: munmap failed\n", s);
    exit(1);
  }
  exit(0);
}

// clone 线程共享信号处理表：tgkill 精确投递后子线程执行处理器。
void
clone_signal(char *s)
{
  char *stack = sbrk(PGSIZE);
  int pid;

  if(stack == SBRK_ERROR){
    printf("%s: sbrk stack failed\n", s);
    exit(1);
  }
  clone_sig_count = 0;
  clone_sig_seen = 0;
  if(signal(SIGUSR1, (uint64)clone_sig_handler) < 0){
    printf("%s: signal failed\n", s);
    exit(1);
  }
  pid = thread_create(clone_sig_worker, 0, stack + PGSIZE);
  if(pid < 0){
    printf("%s: clone failed\n", s);
    exit(1);
  }
  if(tgkill(getpid(), pid, SIGUSR1) < 0){
    printf("%s: tgkill failed\n", s);
    exit(1);
  }
  if(waitpid(pid, 0) != pid){
    printf("%s: clone_signal wait failed\n", s);
    exit(1);
  }
  if(clone_sig_count != 1){
    printf("%s: clone signal handler count %d\n", s, clone_sig_count);
    exit(1);
  }
  sbrk(-PGSIZE);
  exit(0);
}

// 动态模块应能在加载时注册卸载回调，并在 module_unload 时执行。
void
dynmod_lifecycle(char *s)
{
  struct stat st;
  char *buf;
  int fd, n;

  fd = open("dynmod", O_RDONLY);
  if(fd < 0 || fstat(fd, &st) < 0){
    printf("%s: open dynmod failed\n", s);
    exit(1);
  }
  if(st.size <= 0 || st.size > 32768){
    printf("%s: bad dynmod size\n", s);
    exit(1);
  }
  buf = malloc(st.size);
  if(buf == 0){
    printf("%s: malloc failed\n", s);
    exit(1);
  }
  n = read(fd, buf, st.size);
  close(fd);
  if(n != st.size){
    printf("%s: read dynmod failed\n", s);
    free(buf);
    exit(1);
  }
  if(n < 4 || buf[0] != 0x7f || buf[1] != 'E' ||
     buf[2] != 'L' || buf[3] != 'F'){
    printf("%s: dynmod is not an ELF file\n", s);
    free(buf);
    exit(1);
  }
  if(module_load((uint64)buf, st.size) != 0){
    printf("%s: module_load failed\n", s);
    free(buf);
    exit(1);
  }
  if(module_unload(0) != 0){
    printf("%s: module_unload failed\n", s);
    free(buf);
    exit(1);
  }
  free(buf);
  exit(0);
}

// 多个动态模块应能同时驻留，并分别卸载。
void
dynmod_multi(char *s)
{
  char *buf;
  struct stat st;
  int fd, n;

  fd = open("dynmod", O_RDONLY);
  if(fd < 0 || fstat(fd, &st) < 0){
    printf("%s: open dynmod failed\n", s);
    exit(1);
  }
  buf = malloc(st.size);
  if(buf == 0){
    printf("%s: malloc dynmod failed\n", s);
    exit(1);
  }
  n = read(fd, buf, st.size);
  close(fd);
  if(n != st.size || module_load((uint64)buf, n) != 0){
    printf("%s: load dynmod slot 0 failed\n", s);
    free(buf);
    exit(1);
  }
  free(buf);

  fd = open("dynmod2", O_RDONLY);
  if(fd < 0 || fstat(fd, &st) < 0){
    printf("%s: open dynmod2 failed\n", s);
    exit(1);
  }
  buf = malloc(st.size);
  if(buf == 0){
    printf("%s: malloc dynmod2 failed\n", s);
    exit(1);
  }
  n = read(fd, buf, st.size);
  close(fd);
  if(n != st.size || module_load((uint64)buf, n) != 1){
    printf("%s: load dynmod2 slot 1 failed\n", s);
    free(buf);
    exit(1);
  }
  free(buf);

  if(module_call(KMOD_DYN_SAMPLE, 1, 0, 0) != 0x1234 ||
     module_call(KMOD_DYN_TWO, 1, 0, 0) != 0xABCD ||
     module_call(KMOD_DYN_TWO, 2, 0, 0) != (uint64)'d'){
    printf("%s: multi module call failed\n", s);
    exit(1);
  }
  if(module_unload(0) != 0 || module_unload(1) != 0){
    printf("%s: multi module unload failed\n", s);
    exit(1);
  }
  exit(0);
}

// 非 ELF 文件应被加载器拒绝，且不影响后续正常加载。
void
dynmod_badelf(char *s)
{
  char *buf;
  int fd, n;

  unlink("baddynmod");
  fd = open("baddynmod", O_CREATE|O_WRONLY);
  if(fd < 0 || write(fd, "not-an-elf", 10) != 10){
    printf("%s: create baddynmod failed\n", s);
    exit(1);
  }
  close(fd);

  fd = open("baddynmod", O_RDONLY);
  if(fd < 0){
    printf("%s: open baddynmod failed\n", s);
    exit(1);
  }
  buf = malloc(32);
  if(buf == 0){
    printf("%s: malloc failed\n", s);
    exit(1);
  }
  n = read(fd, buf, 32);
  close(fd);
  unlink("baddynmod");
  if(n <= 0 || module_load((uint64)buf, n) != -1){
    printf("%s: bad ELF accepted\n", s);
    free(buf);
    exit(1);
  }
  free(buf);
  exit(0);
}

// 内核线程：通过 selftest 模块创建，用户态 wait 回收并检查执行计数。
void
kernel_kthread(char *s)
{
  uint64 pid;
  int status;

  pid = module_call(KMOD_SELFTEST, SELFTEST_CMD_KTHREAD, 0, 0);
  if(pid == (uint64)-1){
    printf("%s: kthread create failed\n", s);
    exit(1);
  }
  if(wait(&status) != (int)pid || status != 0){
    printf("%s: kthread wait failed\n", s);
    exit(1);
  }
  if(module_call(KMOD_SELFTEST, SELFTEST_CMD_KTHREAD_COUNT, 0, 0) != 1){
    printf("%s: kthread did not run\n", s);
    exit(1);
  }
  exit(0);
}

// 内核自测模块应能验证进程表与内存基本不变量。
void
kernel_selftest(char *s)
{
  int cmds[] = {
    SELFTEST_CMD_BASIC,
    SELFTEST_CMD_REGISTRY,
    SELFTEST_CMD_SIGNAL,
    SELFTEST_CMD_MEMORY,
  };

  for(int i = 0; i < (int)(sizeof(cmds)/sizeof(cmds[0])); i++){
    if(module_call(KMOD_SELFTEST, cmds[i], 0, 0) != 0){
      printf("%s: kernel selftest cmd %d failed\n", s, cmds[i]);
      exit(1);
    }
  }
  exit(0);
}


struct test module_quicktests[] = {
  {fifo_rdwr, "fifo_rdwr"},
  {cowfork, "cowfork"},
  {stats_reset, "stats_reset"},
  {proc_chunked, "proc_chunked"},
  {procinfo_full, "procinfo_full"},
  {signal_no_reenter, "signal_no_reenter"},
  {signal_ignore, "signal_ignore"},
  {signal_default, "signal_default"},
  {sig_stop_cont, "sig_stop_cont"},
  {sig_mask, "sig_mask"},
  {clone_signal, "clone_signal"},
  {dynmod_lifecycle, "dynmod_lifecycle"},
  {dynmod_multi, "dynmod_multi"},
  {dynmod_badelf, "dynmod_badelf"},
  {kernel_kthread, "kernel_kthread"},
  {kernel_selftest, "kernel_selftest"},
  { 0, 0},
};
struct test module_slowtests[] = {
  { 0, 0},
};
