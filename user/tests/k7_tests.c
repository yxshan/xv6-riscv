// P3-K7 IPC tests: semaphores and futex timeout.
//
// Each test function runs in its own child process under the usertests driver.
#include "tests.h"
#include "kernel/sem.h"

void
sem_basic(char *s)
{
  int *shared = (int*)mmap(0, PGSIZE, PROT_READ|PROT_WRITE,
                           MAP_SHARED|MAP_ANONYMOUS, -1, 0);
  struct sembuf acq = { 0, -1, 0 };
  struct sembuf rel = { 0, 1, 0 };
  int semid, pid, st;

  if(shared == (int*)MAP_FAILED){
    printf("%s: mmap failed\n", s);
    exit(1);
  }
  semid = semget(4242, 1, IPC_CREAT);
  if(semid < 0 || semctl(semid, 0, SETVAL, 0) < 0){
    printf("%s: semget/setval failed\n", s);
    exit(1);
  }
  shared[0] = 0;
  pid = fork();
  if(pid < 0){
    printf("%s: fork failed\n", s);
    exit(1);
  }
  if(pid == 0){
    shared[0] = 1;
    if(semop(semid, &acq, 1) < 0)
      exit(1);
    exit(0);
  }
  for(int i = 0; i < 100 && shared[0] == 0; i++)
    pause(1);
  pause(5);
  if(semop(semid, &rel, 1) < 0){
    printf("%s: semop release failed\n", s);
    exit(1);
  }
  if(wait(&st) != pid || st != 0){
    printf("%s: semaphore child failed\n", s);
    exit(1);
  }
  if(semctl(semid, 0, GETVAL, 0) != 0){
    printf("%s: semaphore value mismatch\n", s);
    exit(1);
  }
  semctl(semid, 0, IPC_RMID, 0);
  munmap((char*)shared, PGSIZE);
  exit(0);
}

void
futex_timeout(char *s)
{
  int *v = (int*)mmap(0, PGSIZE, PROT_READ|PROT_WRITE,
                      MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);

  if(v == (int*)MAP_FAILED){
    printf("%s: mmap failed\n", s);
    exit(1);
  }
  *v = 1;
  if(futex_wait_timeout((uint64)v, 1, 200) != -1){
    printf("%s: futex timeout did not fire\n", s);
    exit(1);
  }
  *v = 2;
  if(futex_wait_timeout((uint64)v, 1, 200) != 0){
    printf("%s: futex mismatch did not return\n", s);
    exit(1);
  }
  munmap((char*)v, PGSIZE);
  exit(0);
}

void
futex_robust(char *s)
{
  int *v = (int*)mmap(0, PGSIZE, PROT_READ|PROT_WRITE,
                      MAP_SHARED|MAP_ANONYMOUS, -1, 0);
  int pid, st;

  if(v == (int*)MAP_FAILED){
    printf("%s: mmap failed\n", s);
    exit(1);
  }
  *v = 1;
  pid = fork();
  if(pid < 0){
    printf("%s: fork failed\n", s);
    exit(1);
  }
  if(pid == 0){
    if(futex_set_owner((uint64)v, gettid()) < 0)
      exit(1);
    exit(0);
  }
  if(wait(&st) != pid || st != 0){
    printf("%s: owner child failed\n", s);
    exit(1);
  }
  if((*v & FUTEX_OWNER_DIED) == 0){
    printf("%s: owner died flag missing\n", s);
    exit(1);
  }
  if(futex_wait((uint64)v, 1) != 0){
    printf("%s: robust waiter not woken\n", s);
    exit(1);
  }
  munmap((char*)v, PGSIZE);
  exit(0);
}

struct test k7_quicktests[] = {
  {sem_basic, "sem_basic"},
  {futex_timeout, "futex_timeout"},
  {futex_robust, "futex_robust"},
  { 0, 0},
};

struct test k7_slowtests[] = {
  { 0, 0},
};
