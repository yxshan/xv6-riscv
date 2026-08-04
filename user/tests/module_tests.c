// 内核模块功能测试，例如 FIFO 与 COW。
//
// 每个测试函数由 usertests 驱动在独立子进程中运行。
#include "tests.h"
#include "kernel/module/module_ids.h"

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


struct test module_quicktests[] = {
  {fifo_rdwr, "fifo_rdwr"},
  {cowfork, "cowfork"},
  {stats_reset, "stats_reset"},
  {proc_chunked, "proc_chunked"},
  {procinfo_full, "procinfo_full"},
  { 0, 0},
};
struct test module_slowtests[] = {
  { 0, 0},
};
