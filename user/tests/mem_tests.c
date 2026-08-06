// 内存管理、sbrk 与惰性分配测试。
//
// 每个测试函数由 usertests 驱动在独立子进程中运行。
#include "tests.h"
#include "kernel/swap.h"

#define REGION_SZ (1024 * 1024 * 1024)

void
rwsbrk(char *s)
{
  int fd, n;

  uint64 a = (uint64) sbrk(8192);

  if(a == (uint64) SBRK_ERROR) {
    printf("sbrk(rwsbrk) failed\n");
    exit(1);
  }

  if (sbrk(-8192) == SBRK_ERROR) {
    printf("sbrk(rwsbrk) shrink failed\n");
    exit(1);
  }

  fd = open("rwsbrk", O_CREATE|O_WRONLY);
  if(fd < 0){
    printf("open(rwsbrk) failed\n");
    exit(1);
  }
  n = write(fd, (void*)(a+PGSIZE), 1024);
  if(n >= 0){
    printf("write(fd, %p, 1024) returned %d, not -1\n", (void*)a+PGSIZE, n);
    exit(1);
  }
  close(fd);
  unlink("rwsbrk");

  fd = open("README.md", O_RDONLY);
  if(fd < 0){
    printf("open(README) failed\n");
    exit(1);
  }
  n = read(fd, (void*)(a+PGSIZE), 10);
  if(n >= 0){
    printf("read(fd, %p, 10) returned %d, not -1\n", (void*)a+PGSIZE, n);
    exit(1);
  }
  close(fd);

  exit(0);
}

// test O_TRUNC.

void
mem(char *s)
{
  void *m1, *m2;
  int pid;

  if((pid = fork()) == 0){
    m1 = 0;
    while((m2 = malloc(10001)) != 0){
      *(char**)m2 = m1;
      m1 = m2;
    }
    while(m1){
      m2 = *(char**)m1;
      free(m1);
      m1 = m2;
    }
    m1 = malloc(1024*20);
    if(m1 == 0){
      printf("%s: couldn't allocate mem?!!\n", s);
      exit(1);
    }
    free(m1);
    exit(0);
  } else {
    int xstatus;
    wait(&xstatus);
    if(xstatus == -1){
      // probably page fault, so might be lazy lab,
      // so OK.
      exit(0);
    }
    exit(xstatus);
  }
}

// More file system tests

// two processes write to the same file descriptor
// is the offset shared? does inode locking work?

void
sbrkbasic(char *s)
{
  enum { TOOMUCH=1024*1024*1024};
  int i, pid, xstatus;
  char *c, *a, *b;

  // does sbrk() return the expected failure value?
  pid = fork();
  if(pid < 0){
    printf("fork failed in sbrkbasic\n");
    exit(1);
  }
  if(pid == 0){
    a = sbrk(TOOMUCH);
    if(a == (char*)SBRK_ERROR){
      // it's OK if this fails.
      exit(0);
    }

    for(b = a; b < a+TOOMUCH; b += PGSIZE){
      *b = 99;
    }

    // we should not get here! either sbrk(TOOMUCH)
    // should have failed, or (with lazy allocation)
    // a pagefault should have killed this process.
    exit(1);
  }

  wait(&xstatus);
  if(xstatus == 1){
    printf("%s: too much memory allocated!\n", s);
    exit(1);
  }

  // can one sbrk() less than a page?
  a = sbrk(0);
  for(i = 0; i < 5000; i++){
    b = sbrk(1);
    if(b != a){
      printf("%s: sbrk test failed %d %p %p\n", s, i, a, b);
      exit(1);
    }
    *b = 1;
    a = b + 1;
  }
  pid = fork();
  if(pid < 0){
    printf("%s: sbrk test fork failed\n", s);
    exit(1);
  }
  c = sbrk(1);
  c = sbrk(1);
  if(c != a + 1){
    printf("%s: sbrk test failed post-fork\n", s);
    exit(1);
  }
  if(pid == 0)
    exit(0);
  wait(&xstatus);
  exit(xstatus);
}


void
sbrkmuch(char *s)
{
  enum { BIG=100*1024*1024 };
  char *c, *oldbrk, *a, *lastaddr, *p;
  uint64 amt;

  oldbrk = sbrk(0);

  // can one grow address space to something big?
  a = sbrk(0);
  amt = BIG - (uint64)a;
  p = sbrk(amt);
  if (p != a) {
    printf("%s: sbrk test failed to grow big address space; enough phys mem?\n", s);
    exit(1);
  }

  lastaddr = (char*) (BIG-1);
  *lastaddr = 99;

  // can one de-allocate?
  a = sbrk(0);
  c = sbrk(-PGSIZE);
  if(c == (char*)SBRK_ERROR){
    printf("%s: sbrk could not deallocate\n", s);
    exit(1);
  }
  c = sbrk(0);
  if(c != a - PGSIZE){
    printf("%s: sbrk deallocation produced wrong address, a %p c %p\n", s, a, c);
    exit(1);
  }

  // can one re-allocate that page?
  a = sbrk(0);
  c = sbrk(PGSIZE);
  if(c != a || sbrk(0) != a + PGSIZE){
    printf("%s: sbrk re-allocation failed, a %p c %p\n", s, a, c);
    exit(1);
  }
  if(*lastaddr == 99){
    // should be zero
    printf("%s: sbrk de-allocation didn't really deallocate\n", s);
    exit(1);
  }

  a = sbrk(0);
  c = sbrk(-(sbrk(0) - oldbrk));
  if(c != a){
    printf("%s: sbrk downsize failed, a %p c %p\n", s, a, c);
    exit(1);
  }
}

// can we read the kernel's memory?

void
kernmem(char *s)
{
  char *a;
  int pid;

  for(a = (char*)(KERNBASE); a < (char*) (KERNBASE+2000000); a += 50000){
    pid = fork();
    if(pid < 0){
      printf("%s: fork failed\n", s);
      exit(1);
    }
    if(pid == 0){
      printf("%s: oops could read %p = %x\n", s, a, *a);
      exit(1);
    }
    int xstatus;
    wait(&xstatus);
    if(xstatus != -1)  // did kernel kill child?
      exit(1);
  }
}

// user code should not be able to write to addresses above MAXVA.

void
MAXVAplus(char *s)
{
  volatile uint64 a = MAXVA;
  for( ; a != 0; a <<= 1){
    int pid;
    pid = fork();
    if(pid < 0){
      printf("%s: fork failed\n", s);
      exit(1);
    }
    if(pid == 0){
      *(char*)a = 99;
      printf("%s: oops wrote %p\n", s, (void*)a);
      exit(1);
    }
    int xstatus;
    wait(&xstatus);
    if(xstatus != -1)  // did kernel kill child?
      exit(1);
  }
}

// if we run the system out of memory, does it clean up the last
// failed allocation?

void
sbrkfail(char *s)
{
  enum { BIG=100*1024*1024 };
  int i, xstatus;
  int fds[2];
  char scratch;
  char *c, *a;
  int pids[10];
  int pid;
  int failed;

  failed = 0;
  if(pipe(fds) != 0){
    printf("%s: pipe() failed\n", s);
    exit(1);
  }
  for(i = 0; i < sizeof(pids)/sizeof(pids[0]); i++){
    if((pids[i] = fork()) == 0){
      // allocate a lot of memory
      if (sbrk(BIG - (uint64)sbrk(0)) ==  (char*)SBRK_ERROR)
        write(fds[1], "0", 1);
      else
        write(fds[1], "1", 1);
      // sit around until killed
      for(;;) pause(1000);
    }
    if(pids[i] != -1) {
      read(fds[0], &scratch, 1);
      if(scratch == '0')
        failed = 1;
    }
  }
  if(!failed) {
    printf("%s: no allocation failed; allocate more?\n", s);
  }

  // if those failed allocations freed up the pages they did allocate,
  // we'll be able to allocate here
  c = sbrk(PGSIZE);
  for(i = 0; i < sizeof(pids)/sizeof(pids[0]); i++){
    if(pids[i] == -1)
      continue;
    kill(pids[i]);
    wait(0);
  }
  if(c == (char*)SBRK_ERROR){
    printf("%s: failed sbrk leaked memory\n", s);
    exit(1);
  }

  // test running fork with the above allocated page
  pid = fork();
  if(pid < 0){
    printf("%s: fork failed\n", s);
    exit(1);
  }
  if(pid == 0){
    // allocate a lot of memory. this should produce an error
    a = sbrk(10*BIG);
    if(a == (char*)SBRK_ERROR){
      exit(0);
    }
    printf("%s: allocate a lot of memory succeeded %d\n", s, 10*BIG);
    exit(1);
  }
  wait(&xstatus);
  if(xstatus != 0)
    exit(1);
}


// test reads/writes from/to allocated memory

void
sbrkarg(char *s)
{
  char *a;
  int fd, n;

  a = sbrk(PGSIZE);
  fd = open("sbrk", O_CREATE|O_WRONLY);
  unlink("sbrk");
  if(fd < 0)  {
    printf("%s: open sbrk failed\n", s);
    exit(1);
  }
  if ((n = write(fd, a, PGSIZE)) < 0) {
    printf("%s: write sbrk failed\n", s);
    exit(1);
  }
  close(fd);

  // test writes to allocated memory
  a = sbrk(PGSIZE);
  if(pipe((int *) a) != 0){
    printf("%s: pipe() failed\n", s);
    exit(1);
  }
}

// does uninitialized data start out zero?
char uninit[10000];

void
bsstest(char *s)
{
  int i;

  for(i = 0; i < sizeof(uninit); i++){
    if(uninit[i] != '\0'){
      printf("%s: bss test failed\n", s);
      exit(1);
    }
  }
}

// does exec return an error if the arguments
// are larger than a page? or does it write
// below the stack and wreck the instructions/data?

void
bigargtest(char *s)
{
  int pid, fd, xstatus;

  unlink("bigarg-ok");
  pid = fork();
  if(pid == 0){
    static char *args[MAXARG];
    int i;
    char big[400];
    memset(big, ' ', sizeof(big));
    big[sizeof(big)-1] = '\0';
    for(i = 0; i < MAXARG-1; i++)
      args[i] = big;
    args[MAXARG-1] = 0;
    // this exec() should fail (and return) because the
    // arguments are too large.
    exec("echo", args);
    fd = open("bigarg-ok", O_CREATE);
    close(fd);
    exit(0);
  } else if(pid < 0){
    printf("%s: bigargtest: fork failed\n", s);
    exit(1);
  }

  wait(&xstatus);
  if(xstatus != 0)
    exit(xstatus);
  fd = open("bigarg-ok", 0);
  if(fd < 0){
    printf("%s: bigarg test failed!\n", s);
    exit(1);
  }
  close(fd);
}

// what happens when the file system runs out of blocks?
// answer: balloc panics, so this test is not useful.

void argptest(char *s)
{
  int fd;
  fd = open("init", O_RDONLY);
  if (fd < 0) {
    printf("%s: open failed\n", s);
    exit(1);
  }
  read(fd, sbrk(0) - 1, -1);
  close(fd);
}

// check that there's an invalid page beneath
// the user stack, to catch stack overflow.

void
sbrkbugs(char *s)
{
  int pid = fork();
  if(pid < 0){
    printf("fork failed\n");
    exit(1);
  }
  if(pid == 0){
    int sz = (uint64) sbrk(0);
    // free all user memory; there used to be a bug that
    // would not adjust p->sz correctly in this case,
    // causing exit() to panic.
    sbrk(-sz);
    // user page fault here.
    exit(0);
  }
  wait(0);

  pid = fork();
  if(pid < 0){
    printf("fork failed\n");
    exit(1);
  }
  if(pid == 0){
    int sz = (uint64) sbrk(0);
    // set the break to somewhere in the very first
    // page; there used to be a bug that would incorrectly
    // free the first page.
    sbrk(-(sz - 3500));
    exit(0);
  }
  wait(0);

  pid = fork();
  if(pid < 0){
    printf("fork failed\n");
    exit(1);
  }
  if(pid == 0){
    // set the break in the middle of a page.
    sbrk((10*PGSIZE + 2048) - (uint64)sbrk(0));

    // reduce the break a bit, but not enough to
    // cause a page to be freed. this used to cause
    // a panic.
    sbrk(-10);

    exit(0);
  }
  wait(0);

  exit(0);
}

// if process size was somewhat more than a page boundary, and then
// shrunk to be somewhat less than that page boundary, can the kernel
// still copyin() from addresses in the last page?

void
sbrklast(char *s)
{
  uint64 top = (uint64) sbrk(0);
  if((top % PGSIZE) != 0)
    sbrk(PGSIZE - (top % PGSIZE));
  sbrk(PGSIZE);
  sbrk(10);
  sbrk(-20);
  top = (uint64) sbrk(0);
  char *p = (char *) (top - 64);
  p[0] = 'x';
  p[1] = '\0';
  int fd = open(p, O_RDWR|O_CREATE);
  write(fd, p, 1);
  close(fd);
  fd = open(p, O_RDWR);
  p[0] = '\0';
  read(fd, p, 1);
  if(p[0] != 'x')
    exit(1);
}


// does sbrk handle signed int32 wrap-around with
// negative arguments?

void
sbrk8000(char *s)
{
  sbrk(0x80000004);
  volatile char *top = sbrk(0);
  *(top-1) = *(top-1) + 1;
}



// regression test. test whether exec() leaks memory if one of the
// arguments is invalid. the test passes if the kernel doesn't panic.

void
lazy_alloc(char *s)
{
  char *i, *prev_end, *new_end;

  prev_end = sbrklazy(REGION_SZ);
  if (prev_end == (char *) SBRK_ERROR) {
    printf("sbrklazy() failed\n");
    exit(1);
  }
  new_end = prev_end + REGION_SZ;

  for (i = prev_end + PGSIZE; i < new_end; i += 64 * PGSIZE)
    *(char **)i = i;

  for (i = prev_end + PGSIZE; i < new_end; i += 64 * PGSIZE) {
    if (*(char **)i != i) {
      printf("failed to read value from memory\n");
      exit(1);
    }
  }

  exit(0);
}

// Touch a page every 64 pages in region, which with lazy allocation
// causes one page to be allocated. Check that freeing the region
// frees the allocated pages.

void
lazy_unmap(char *s)
{
  int pid;
  char *i, *prev_end, *new_end;

  prev_end = sbrklazy(REGION_SZ);
  if (prev_end == (char*)SBRK_ERROR) {
    printf("sbrklazy() failed\n");
    exit(1);
  }
  new_end = prev_end + REGION_SZ;

  for (i = prev_end + PGSIZE; i < new_end; i += PGSIZE * PGSIZE)
    *(char **)i = i;

  for (i = prev_end + PGSIZE; i < new_end; i += PGSIZE * PGSIZE) {
    pid = fork();
    if (pid < 0) {
      printf("error forking\n");
      exit(1);
    } else if (pid == 0) {
      sbrklazy(-1L * REGION_SZ);
      *(char **)i = i;
      exit(0);
    } else {
      int status;
      wait(&status);
      if (status == 0) {
        printf("memory not unmapped\n");
        exit(1);
      }
    }
  }

  exit(0);
}


void
lazy_copy(char *s)
{
  // copyinstr on lazy page
  {
    char *p = sbrk(0);
    sbrklazy(4*PGSIZE);
    open(p + 8192, 0);
  }

  {
    void *xx = sbrk(0);
    void *ret = sbrk(-(((uint64) xx)+1));
    if(ret != xx){
      printf("sbrk(sbrk(0)+1) returned %p, not old sz\n", ret);
      exit(1);
    }
  }


  // read() and write() to these addresses should fail.
  unsigned long bad[] = {
    0x3fffffc000,
    0x3fffffd000,
    0x3fffffe000,
    0x3ffffff000,
    0x4000000000,
    0x8000000000,
  };
  for(int i = 0; i < sizeof(bad)/sizeof(bad[0]); i++){
    int fd = open("README.md", 0);
    if(fd < 0) { printf("cannot open README\n"); exit(1); }
    if(read(fd, (char*)bad[i], 512) >= 0) { printf("read succeeded\n");  exit(1); }
    close(fd);
    fd = open("junk", O_CREATE|O_RDWR|O_TRUNC);
    if(fd < 0) { printf("cannot open junk\n"); exit(1); }
    if(write(fd, (char*)bad[i], 512) >= 0) { printf("write succeeded\n"); exit(1); }
    close(fd);
  }

  exit(0);
}


void
lazy_sbrk(char *s)
{
  // sbrk() takes just int, so take 2^30-sized steps towards MAXVA
  char *p = sbrk(0);
  while ((uint64)p < MAXVA-(1<<30)) {
    p = sbrklazy(1<<30);
    if (p < 0) {
      printf("sbrklazy(%d) returned %p\n", 1<<30, p);
      exit(1);
    }

    p = sbrklazy(0);
  }

  int n = MMAP_BASE-PGSIZE-(uint64)p;

  char *p1 = sbrklazy(n);
  if (p1 < 0 || p1 != p) {
    printf("sbrklazy(%d) returned %p, not expected %p\n", n, p1, p);
    exit(1);
  }

  p = sbrk(PGSIZE);
  if (p < 0 || (uint64)p != MMAP_BASE-PGSIZE) {
    printf("sbrk(%d) returned %p, not expected MMAP_BASE-PGSIZE\n", PGSIZE, p);
    exit(1);
  }

  p[0] = 1;
  if (p[1] != 0) {
    printf("sbrk() returned non-zero-filled memory\n");
    exit(1);
  }

  p = sbrk(1);
  if ((uint64)p != -1) {
    printf("sbrk(1) returned %p, expected error\n", p);
    exit(1);
  }

  p = sbrklazy(1);
  if ((uint64)p != -1) {
    printf("sbrklazy(1) returned %p, expected error\n", p);
    exit(1);
  }

  exit(0);
}

// 交换空间：强制换出一页后仍能通过缺页换入并读到原数据。
void
swap_basic(char *s)
{
  struct swapinfo si;
  char *p = sbrk(PGSIZE);

  if(p == SBRK_ERROR){
    printf("%s: sbrk failed\n", s);
    exit(1);
  }
  p[0] = 0x5a;
  p[PGSIZE - 1] = 0xa5;

  if(swapout() < 0){
    printf("%s: swapout failed\n", s);
    exit(1);
  }
  if(swapinfo(&si) < 0 || si.swapouts == 0){
    printf("%s: swapout not counted\n", s);
    exit(1);
  }
  if(p[0] != 0x5a || p[PGSIZE - 1] != 0xa5){
    printf("%s: swap in data mismatch\n", s);
    exit(1);
  }
  if(swapinfo(&si) < 0 || si.swapins == 0){
    printf("%s: swapin not counted\n", s);
    exit(1);
  }

  // 再次换出验证第二次换入。
  if(swapout() < 0){
    printf("%s: second swapout failed\n", s);
    exit(1);
  }
  if(p[0] != 0x5a || p[PGSIZE - 1] != 0xa5){
    printf("%s: second swap in data mismatch\n", s);
    exit(1);
  }
  exit(0);
}

// FIFO O_RDWR：同一描述符可先写后读，阻塞读端也能被 O_RDWR 写端唤醒。

struct test mem_quicktests[] = {
  {rwsbrk, "rwsbrk"},
  {mem, "mem"},
  {sbrkbasic, "sbrkbasic"},
  {sbrkmuch, "sbrkmuch"},
  {kernmem, "kernmem"},
  {MAXVAplus, "MAXVAplus"},
  {sbrkfail, "sbrkfail"},
  {sbrkarg, "sbrkarg"},
  {bsstest, "bsstest"},
  {bigargtest, "bigargtest"},
  {argptest, "argptest"},
  {sbrkbugs, "sbrkbugs"},
  {sbrklast, "sbrklast"},
  {sbrk8000, "sbrk8000"},
  {lazy_alloc, "lazy_alloc"},
  {lazy_unmap, "lazy_unmap"},
  {lazy_copy, "lazy_copy"},
  {lazy_sbrk, "lazy_sbrk"},
  {swap_basic, "swap_basic"},
  { 0, 0},
};
struct test mem_slowtests[] = {
  { 0, 0},
};
