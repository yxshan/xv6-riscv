// 内核各模块的函数声明汇总。
// xv6 不使用按模块组织的头文件系统，而是把所有跨模块接口集中在这里；
// 新增内核 API 时，应同时更新对应模块的声明。
struct buf;
struct context;
struct file;
struct inode;
struct pipe;
struct proc;
struct proc_sig;
struct proc_vmas;
struct spinlock;
struct sleeplock;
struct stat;
struct superblock;
struct kmod_device;

// bio.c
void            binit(void);
struct buf*     bread(uint, uint);
void            brelse(struct buf*);
void            bwrite(struct buf*);
void            bpin(struct buf*);
void            bunpin(struct buf*);

// console.c
void            consoleinit(void);
void            consoleintr(int);
void            consputc(int);

// exec.c
int             kexec(char*, char**);

// file.c
struct file*    filealloc(void);
void            fileclose(struct file*);
struct file*    filedup(struct file*);
void            fileinit(void);
struct proc_files* proc_files_alloc(void);
void            proc_files_share(struct proc_files*);
void            proc_files_copy(struct proc_files*, struct proc_files*);
void            proc_files_release(struct proc_files*);
struct proc_vmas* proc_vmas_alloc(void);
void            proc_vmas_share(struct proc_vmas*);
void            proc_vmas_release(struct proc*);
struct proc_sig* proc_sig_alloc(void);
void            proc_sig_share(struct proc_sig*);
void            proc_sig_copy(struct proc_sig*, struct proc_sig*);
void            proc_sig_release(struct proc_sig*);
int             fileread(struct file*, uint64, int n);
int             filestat(struct file*, uint64 addr);
int             filewrite(struct file*, uint64, int n);

// fs.c
void            fsinit(int);
int             dirlink(struct inode*, char*, uint);
struct inode*   dirlookup(struct inode*, char*, uint*);
struct inode*   iget(uint, uint);
struct inode*   ialloc(uint, short);
struct inode*   idup(struct inode*);
void            iinit();
void            ilock(struct inode*);
void            iput(struct inode*);
void            iunlock(struct inode*);
void            iunlockput(struct inode*);
void            iupdate(struct inode*);
int             namecmp(const char*, const char*);
struct inode*   namei(char*);
struct inode*   nameiparent(char*, char*);
int             readi(struct inode*, int, uint64, uint, uint);
void            stati(struct inode*, struct stat*);
int             writei(struct inode*, int, uint64, uint, uint);
void            itrunc(struct inode*);
void            ireclaim(int);
int             iaccess(struct inode*, int);
int             fsvalid(int);

// mount.c
void            mountinit(void);
void            mount_acquire(void);
void            mount_release(void);
int             mount_enter(struct inode**);
int             mount_dotdot(struct inode*, struct inode**);
int             mount_add(int, struct inode*, char*);
int             mount_remove(char*);
void            mount_default(void);
uint64          sys_mount(void);
uint64          sys_umount(void);

// kalloc.c
void*           kalloc(void);
void            kfree(void *);
void            kinit(void);
uint64          freemem(void);

// log.c
void            initlog(int, struct superblock*);
void            log_write(struct buf*);
void            begin_op(void);
void            end_op(void);

// pipe.c
int             pipealloc(struct file**, struct file**);
struct pipe*    fifoalloc(void);
void            fifo_open(struct pipe*, int, int);
void            fifo_close(struct pipe*, int);
void            pipeclose(struct pipe*, int);
int             piperead(struct pipe*, uint64, int);
int             pipewrite(struct pipe*, uint64, int);

// printf.c
int             printf(char*, ...) __attribute__ ((format (printf, 1, 2)));
void            panic(char*) __attribute__((noreturn));
void            printfinit(void);
void            backtrace(void);

// proc.c
int             cpuid(void);
int             proccount(void);
int             ksetpriority(int, int);
void            mlfq_boost(void);
int             ksignal(int, uint64);
int             ksigkill(int, int);
uint64          sys_signal(void);
uint64          sys_sigkill(void);
uint64          sys_sigreturn(void);
uint64          sys_sigprocmask(void);
void            kexit(int);
int             kfork(void);
int             kclone(uint64, uint64, uint64, uint64);
int             kthread_create(void (*)(void*), void*);
int             growproc(int);
void            proc_mapstacks(pagetable_t);
pagetable_t     proc_pagetable(struct proc *);
void            proc_freepagetable(pagetable_t, uint64);
int             kkill(int);
int             ktgkill(int, int, int);
int             killed(struct proc*);
void            setkilled(struct proc*);
struct cpu*     mycpu(void);
struct proc*    myproc();
void            procinit(void);
void            scheduler(void) __attribute__((noreturn));
void            sched(void);
void            sleep(void*, struct spinlock*);
void            userinit(void);
int             kwait(uint64);
int             kwaitpid(int, uint64);
void            wakeup(void*);
void            yield(void);
int             either_copyout(int user_dst, uint64 dst, void *src, uint64 len);
int             either_copyin(void *dst, int user_src, uint64 src, uint64 len);
void            procdump(void);

// swtch.S
void            swtch(struct context*, struct context*);

// spinlock.c
void            acquire(struct spinlock*);
int             holding(struct spinlock*);
void            initlock(struct spinlock*, char*);
void            release(struct spinlock*);
void            push_off(void);
void            pop_off(void);

// sleeplock.c
void            acquiresleep(struct sleeplock*);
void            releasesleep(struct sleeplock*);
int             holdingsleep(struct sleeplock*);
void            initsleeplock(struct sleeplock*, char*);

// string.c
int             memcmp(const void*, const void*, uint);
void*           memmove(void*, const void*, uint);
void*           memset(void*, int, uint);
char*           safestrcpy(char*, const char*, int);
int             strlen(const char*);
int             strncmp(const char*, const char*, uint);
char*           strncpy(char*, const char*, int);

// syscall.c
void            argint(int, int*);
int             argstr(int, char*, int);
void            argaddr(int, uint64 *);
int             fetchstr(uint64, char*, int);
int             fetchaddr(uint64, uint64*);
void            syscall();

// module.c
void            module_init_all(void);
uint64          module_dispatch(int, int, uint64, uint64);
uint64          sys_module_call(void);
uint64          sys_dumpstate(void);

// shm.c
int             shmget(int, int);
uint64          shmat(int);
int             shmdt(int);
int             shmrm(int);
void            shm_release_pagetable(pagetable_t);
void            shm_addref_pa(uint64);
void            shm_subref_pa(uint64);
int             shm_selftest(void);
uint64          sys_shmget(void);
uint64          sys_shmat(void);
uint64          sys_shmdt(void);
uint64          sys_shmrm(void);

// cow.c
int             cow_add(uint64);
void            cow_release(uint64);
int             cow_handle(pagetable_t, uint64);
int             cow_selftest(void);

// swap.c
void            swapinit(void);
int             swap_write(uint64, int);
int             swap_read(uint64, int);
void            swap_free(int);
uint64          swap_flags(int);
int             swap_evict(void);
uint64          sys_swapout(void);
uint64          sys_swapinfo(void);

// futex.c
void            futexinit(void);
uint64          sys_futex_wait(void);
uint64          sys_futex_wake(void);

void            module_notify_tick(void);
void            module_notify_syscall_enter(int);
void            module_notify_syscall_exit(int, uint64);
void            module_notify_proc_fork(struct proc*);
void            module_notify_proc_exit(struct proc*);
int             module_device_register(struct kmod_device*);
int             module_device_unregister(int);

// trap.c
extern uint     ticks;
void            trapinit(void);
void            trapinithart(void);
extern struct spinlock tickslock;
void            prepare_return(void);

// uart.c
void            uartinit(void);
void            uartintr(void);
void            uartwrite(char [], int);
void            uartputc_sync(int);
int             uartgetc(void);

// vm.c
void            kvminit(void);
void            kvminithart(void);
void            kvmmap(pagetable_t, uint64, uint64, uint64, int);
int             mappages(pagetable_t, uint64, uint64, uint64, int);
pagetable_t     uvmcreate(void);
uint64          uvmalloc(pagetable_t, uint64, uint64, int);
uint64          uvmdealloc(pagetable_t, uint64, uint64);
int             uvmcopy(pagetable_t, pagetable_t, uint64);
int             uvmshare(pagetable_t, pagetable_t, uint64);
void            uvmfree(pagetable_t, uint64);
void            uvmunmap(pagetable_t, uint64, uint64, int);
void            uvmclear(pagetable_t, uint64);
pte_t *         walk(pagetable_t, uint64, int);
uint64          walkaddr(pagetable_t, uint64);
int             copyout(pagetable_t, uint64, char *, uint64);
int             copyin(pagetable_t, char *, uint64, uint64);
int             copyinstr(pagetable_t, char *, uint64, uint64);
int             ismapped(pagetable_t, uint64);
uint64          vmfault(pagetable_t, uint64, int);

// vma.c
uint64          vma_alloc(struct proc*, uint64);
int             vma_add(struct proc*, uint64, uint64, int, int, struct inode*, uint);
uint64          vma_mmap(struct proc*, uint64, int, int, struct inode*, uint);
int             vma_remove(struct proc*, uint64, uint64);
void            vma_clear(struct proc*);
int             vma_copy(struct proc*, struct proc*);
uint64          vma_fault(struct proc*, uint64);

// plic.c
void            plicinit(void);
void            plicinithart(void);
int             plic_claim(void);
void            plic_complete(int);

// virtio_disk.c
void            virtio_disk_init(void);
void            virtio_disk_rw(struct buf *, int);
void            virtio_disk_intr(int);

// number of elements in fixed-size array
#define NELEM(x) (sizeof(x)/sizeof((x)[0]))
