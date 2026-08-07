// usertests 驱动。
//
// 负责解析命令行参数，并按子系统套件依次执行 quick/slow 测试。
// 测试函数本身拆分在 user/tests/ 下的 *_tests.c 中。
#include "tests.h"

char buf[BUFSZ];

struct testsuite {
  struct test *quick;
  struct test *slow;
};

static struct testsuite suites[] = {
  { syscall_quicktests, syscall_slowtests },
  { mem_quicktests, mem_slowtests },
  { fs_quicktests, fs_slowtests },
  { proc_quicktests, proc_slowtests },
  { module_quicktests, module_slowtests },
  { perm_quicktests, perm_slowtests },
  { mmap_quicktests, mmap_slowtests },
  { thread_quicktests, thread_slowtests },
  { signal_quicktests, signal_slowtests },
  { k5_quicktests, k5_slowtests },
  { k6_quicktests, k6_slowtests },
  { k7_quicktests, k7_slowtests },
  { k8_quicktests, k8_slowtests },
};

// run each test in its own process. run returns 1 if child's exit()
// indicates success.
static int
run(void f(char *), char *s)
{
  int pid;
  int xstatus;

  printf("test %s: ", s);
  if((pid = fork()) < 0) {
    printf("runtest: fork error\n");
    exit(1);
  }
  if(pid == 0) {
    f(s);
    exit(0);
  } else {
    wait(&xstatus);
    if(xstatus != 0)
      printf("FAILED\n");
    else
      printf("OK\n");
    return xstatus == 0;
  }
}

static int
runtests(struct test *tests, char *justone, int continuous)
{
  int ntests = 0;

  if(tests == 0)
    return 0;
  for(struct test *t = tests; t->s != 0; t++){
    if(justone == 0 || strcmp(t->s, justone) == 0){
      ntests++;
      if(!run(t->f, t->s)){
        if(continuous != 2){
          printf("SOME TESTS FAILED\n");
          return -1;
        }
      }
    }
  }
  return ntests;
}

// use sbrk() to count how many free physical memory pages there are.
static int
countfree()
{
  int n = 0;
  uint64 sz0 = (uint64)sbrk(0);
  while(1){
    char *a = sbrk(PGSIZE);
    if(a == SBRK_ERROR)
      break;
    n += 1;
  }
  sbrk(-((uint64)sbrk(0) - sz0));
  return n;
}

static int
drivetests(int quick, int continuous, char *justone)
{
  do {
    printf("usertests starting\n");
    int free0 = countfree();
    int free1 = 0;
    int ntests = 0;
    int n;
    int nsuites = sizeof(suites) / sizeof(suites[0]);

    for(int i = 0; i < nsuites; i++){
      n = runtests(suites[i].quick, justone, continuous);
      if(n < 0){
        if(continuous != 2)
          return 1;
      } else {
        ntests += n;
      }
    }
    if(!quick){
      if(justone == 0)
        printf("usertests slow tests starting\n");
      for(int i = 0; i < nsuites; i++){
        n = runtests(suites[i].slow, justone, continuous);
        if(n < 0){
          if(continuous != 2)
            return 1;
        } else {
          ntests += n;
        }
      }
    }
    if((free1 = countfree()) < free0){
      printf("FAILED -- lost some free pages %d (out of %d)\n", free1, free0);
      if(continuous != 2)
        return 1;
    }
    if(justone != 0 && ntests == 0){
      printf("NO TESTS EXECUTED\n");
      return 1;
    }
  } while(continuous);
  return 0;
}

int
main(int argc, char *argv[])
{
  int continuous = 0;
  int quick = 0;
  char *justone = 0;

  if(argc == 2 && strcmp(argv[1], "-q") == 0){
    quick = 1;
  } else if(argc == 2 && strcmp(argv[1], "-c") == 0){
    continuous = 1;
  } else if(argc == 2 && strcmp(argv[1], "-C") == 0){
    continuous = 2;
  } else if(argc == 2 && argv[1][0] != '-'){
    justone = argv[1];
  } else if(argc > 1){
    printf("Usage: usertests [-c] [-C] [-q] [testname]\n");
    exit(1);
  }
  if(drivetests(quick, continuous, justone))
    exit(1);
  printf("ALL TESTS PASSED\n");
  exit(0);
}
