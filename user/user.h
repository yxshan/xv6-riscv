#include "kernel/param.h"
#include "kernel/signal.h"
#include "kernel/time.h"
#include "kernel/uio.h"
#include "kernel/poll.h"
#include "kernel/sem.h"

#define SBRK_ERROR ((char *)-1)

struct stat;
struct swapinfo;

// system calls
int fork(void);
int exit(int) __attribute__((noreturn));
void exit_group(int) __attribute__((noreturn));
int wait(int*);
int pipe(int*);
int write(int, const void*, int);
int read(int, void*, int);
int close(int);
int kill(int);
int exec(const char*, char**);
int open(const char*, int);
int mknod(const char*, short, short);
int unlink(const char*);
int fstat(int fd, struct stat*);
int link(const char*, const char*);
int mkdir(const char*);
int chdir(const char*);
int dup(int);
int getpid(void);
int dup2(int, int);
int getcwd(char*, int);
int chroot(const char*);
int fsync(int);
int flock(int, int);
int semget(int, int, int);
int semop(int, const struct sembuf*, int);
int semctl(int, int, int, int);
int nanosleep(const struct timespec*, struct timespec*);
int clock_gettime(int, struct timespec*);
int readv(int, const struct iovec*, int);
int writev(int, const struct iovec*, int);
int pipe2(int*, int);
int poll(struct pollfd*, int, int);
int select(int, fd_set*, fd_set*, fd_set*, const struct timespec*);
char* sys_sbrk(int,int);
int pause(int);
int uptime(void);
uint64 module_call(int, int, uint64, uint64);
int module_load(uint64, int);
int module_unload(int);
int setpriority(int, int);
int symlink(const char*, const char*);
int mkfifo(const char*, int);
int dumpstate(void);
int shmget(int, int);
uint64 shmat(int);
int shmdt(int);
int shmrm(int);
int signal(int, uint64);
int sigaction(int, const struct sigaction*, struct sigaction*);
int sigpending(uint64*);
int sigkill(int, int);
void sigreturn(void);
int chmod(const char*, int);
int chown(const char*, int, int);
int getuid(void);
int geteuid(void);
int getgid(void);
int getegid(void);
int setuid(int);
int setgid(int);
int umask(int);
char* mmap(char*, uint, int, int, int, uint);
int munmap(char*, uint);
int mprotect(void*, uint, int);
int mount(int, const char*);
int umount(const char*);
int swapout(void);
int swapinfo(struct swapinfo*);
int clone(uint64, uint64, uint64, uint64, uint64);
int thread_create(void (*)(void*), void*, void*);
int futex_wait(uint64, int);
int futex_wake(uint64, int);
int futex_wait_timeout(uint64, int, int);
int gettid(void);
int waitpid(int, int*);
int waitpid_flags(int, int*, int);
int set_tls(uint64);
uint64 get_tls(void);
int tgkill(int, int, int);
int sigprocmask(int, uint64*, uint64*);
int setpgid(int, int);
int getpgid(int);
int killpg(int, int);
int tcsetpgrp(int, int);
int tcgetpgrp(int);

// ulib.c
int stat(const char*, struct stat*);
char* strcpy(char*, const char*);
void *memmove(void*, const void*, int);
char* strchr(const char*, char c);
int strcmp(const char*, const char*);
char* gets(char*, int max);
uint strlen(const char*);
void* memset(void*, int, uint);
int atoi(const char*);
int memcmp(const void *, const void *, uint);
void *memcpy(void *, const void *, uint);
char* sbrk(int);
char* sbrklazy(int);

// printf.c
void fprintf(int, const char*, ...) __attribute__ ((format (printf, 2, 3)));
void printf(const char*, ...) __attribute__ ((format (printf, 1, 2)));

// umalloc.c
void* malloc(uint);
void free(void*);
