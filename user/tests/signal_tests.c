// Signal completeness tests: sigaction, sigpending and process-group delivery.
//
// Each test function runs in its own child process under the usertests driver.
#include "tests.h"
#include "kernel/signal.h"

static int act_mask_count;
static int act_mask_seen;

static void
act_mask_handler(int sig)
{
  uint64 cur;

  act_mask_count++;
  if(sigprocmask(0, 0, &cur) == 0 && (cur & (1UL << SIGUSR2)))
    act_mask_seen = 1;
  sigreturn();
}

// sigaction stores sa_mask and restores the old mask after sigreturn.
void
sigaction_mask(char *s)
{
  struct sigaction act, old;
  uint64 cur;

  act.sa_handler = (uint64)act_mask_handler;
  act.sa_mask = 1UL << SIGUSR2;
  act.sa_flags = 0;
  if(sigaction(SIGUSR1, &act, &old) < 0 || old.sa_handler != SIG_DFL){
    printf("%s: sigaction failed\n", s);
    exit(1);
  }
  act_mask_count = 0;
  act_mask_seen = 0;
  if(sigkill(getpid(), SIGUSR1) < 0){
    printf("%s: sigkill failed\n", s);
    exit(1);
  }
  if(act_mask_count != 1 || !act_mask_seen){
    printf("%s: sa_mask not applied\n", s);
    exit(1);
  }
  if(sigprocmask(0, 0, &cur) < 0 || (cur & (1UL << SIGUSR2))){
    printf("%s: sa_mask not restored\n", s);
    exit(1);
  }
  exit(0);
}

static int pending_count;

static void
pending_handler(int sig)
{
  pending_count++;
  sigreturn();
}

// sigpending reports blocked signals; unblocking delivers them.
void
sigpending_basic(char *s)
{
  struct sigaction act;
  uint64 set = 1UL << SIGUSR1;
  uint64 pending;

  act.sa_handler = (uint64)pending_handler;
  act.sa_mask = 0;
  act.sa_flags = 0;
  pending_count = 0;
  if(sigaction(SIGUSR1, &act, 0) < 0){
    printf("%s: sigaction failed\n", s);
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
  if(sigpending(&pending) < 0 || !(pending & (1UL << SIGUSR1))){
    printf("%s: sigpending did not report signal\n", s);
    exit(1);
  }
  if(sigprocmask(SIG_UNBLOCK, &set, 0) < 0){
    printf("%s: sigprocmask unblock failed\n", s);
    exit(1);
  }
  if(pending_count != 1){
    printf("%s: pending signal not delivered\n", s);
    exit(1);
  }
  exit(0);
}

static int reset_count;

static void
reset_handler(int sig)
{
  reset_count++;
  sigreturn();
}

// SA_RESETHAND runs once, then the disposition returns to SIG_DFL.
void
sigaction_reset(char *s)
{
  int pid = fork();
  int st;

  if(pid < 0){
    printf("%s: fork failed\n", s);
    exit(1);
  }
  if(pid == 0){
    struct sigaction act;

    act.sa_handler = (uint64)reset_handler;
    act.sa_mask = 0;
    act.sa_flags = SA_RESETHAND;
    if(sigaction(SIGUSR1, &act, 0) < 0)
      exit(1);
    if(sigkill(getpid(), SIGUSR1) < 0)
      exit(1);
    if(reset_count != 1)
      exit(1);
    if(sigkill(getpid(), SIGUSR1) < 0)
      exit(1);
    pause(100);
    exit(0);
  }
  if(wait(&st) != pid || st == 0){
    printf("%s: SA_RESETHAND did not reset to default\n", s);
    exit(1);
  }
  exit(0);
}

static int pg_handler_count;

static void
pg_handler(int sig)
{
  pg_handler_count++;
  sigreturn();
}

// sigkill(-pgid, sig) delivers only to that process group.
void
kill_negative_pgid(char *s)
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
  pid = fork();
  if(pid < 0){
    printf("%s: fork failed\n", s);
    exit(1);
  }
  if(pid == 0){
    struct sigaction act;

    if(setpgid(0, 0) < 0)
      exit(1);
    act.sa_handler = (uint64)pg_handler;
    act.sa_mask = 0;
    act.sa_flags = 0;
    if(sigaction(SIGUSR1, &act, 0) < 0)
      exit(1);
    shared[0] = 1;
    while(shared[1] == 0)
      pause(1);
    if(pg_handler_count != 1)
      exit(1);
    exit(0);
  }
  for(int i = 0; i < 100 && shared[0] == 0; i++)
    pause(1);
  if(shared[0] == 0){
    printf("%s: child not ready\n", s);
    exit(1);
  }
  if(sigkill(-pid, SIGUSR1) < 0){
    printf("%s: negative pid sigkill failed\n", s);
    exit(1);
  }
  shared[1] = 1;
  if(wait(&st) != pid || st != 0){
    printf("%s: process group signal failed\n", s);
    exit(1);
  }
  if(munmap((char*)shared, PGSIZE) < 0){
    printf("%s: munmap failed\n", s);
    exit(1);
  }
  exit(0);
}

struct test signal_quicktests[] = {
  {sigaction_mask, "sigaction_mask"},
  {sigpending_basic, "sigpending_basic"},
  {sigaction_reset, "sigaction_reset"},
  {kill_negative_pgid, "kill_negative_pgid"},
  { 0, 0},
};

struct test signal_slowtests[] = {
  { 0, 0},
};
