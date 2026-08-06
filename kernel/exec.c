// exec 系统调用的实现。
//
// exec 用一个全新的用户程序镜像替换当前进程：
// 解析 ELF 文件，把其中的可加载段复制到新建的用户页表，
// 在用户栈上布置 argc/argv，最后提交新页表并更新 PC 和 SP。
// exec 成功后，旧程序的所有用户内存都会被释放。
#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "proc.h"
#include "defs.h"
#include "elf.h"
#include "stat.h"
#include "fs.h"
#include "file.h"

static int loadseg(pde_t *, uint64, struct inode *, uint, uint);

#define ASLR_STACK_PAGES 16

// 简单线性同余伪随机数生成器，供 exec 随机化用户栈位置。
static uint64 aslr_seed;

static uint64
aslr_rand(void)
{
  if(aslr_seed == 0)
    aslr_seed = r_time() ^ ((uint64)myproc()->pid << 32) ^ 0x9e3779b97f4a7c15UL;
  aslr_seed = aslr_seed * 6364136223846793005UL + 1442695040888963407UL;
  return aslr_seed >> 33;
}

// 把 ELF 段权限标志映射为 RISC-V PTE 权限位。
int flags2perm(int flags)
{
    int perm = 0;
    if(flags & 0x1)
      perm = PTE_X;
    if(flags & 0x2)
      perm |= PTE_W;
    return perm;
}

//
// exec 的内核实现。
//
int
kexec(char *path, char **argv)
{
  char *s, *last;
  int i, off;
  uint64 argc, sz = 0, sp, ustack[MAXARG], stackbase;
  struct elfhdr elf;
  struct inode *ip;
  struct proghdr ph;
  pagetable_t pagetable = 0, oldpagetable;
  struct proc *p = myproc();

  begin_op();

  // 打开并锁定可执行文件。
  if((ip = namei(path)) == 0){
    end_op();
    return -1;
  }
  ilock(ip);

  if(ip->type != T_FILE || iaccess(ip, 1) < 0)
    goto bad;

  // 读取 ELF 头，并验证魔数，确认是可执行文件。
  if(readi(ip, 0, (uint64)&elf, 0, sizeof(elf)) != sizeof(elf))
    goto bad;

  // 是否为合法 ELF 文件。
  if(elf.magic != ELF_MAGIC)
    goto bad;

  // 先在新页表中构建用户镜像，成功后原子替换当前页表。
  if((pagetable = proc_pagetable(p)) == 0)
    goto bad;

  // 遍历 ELF 程序头，把每个 PT_LOAD 段加载到新页表。
  for(i=0, off=elf.phoff; i<elf.phnum; i++, off+=sizeof(ph)){
    if(readi(ip, 0, (uint64)&ph, off, sizeof(ph)) != sizeof(ph))
      goto bad;
    if(ph.type != ELF_PROG_LOAD)
      continue;
    // 校验段大小与地址的合法性，防止恶意 ELF 造成越界。
    if(ph.memsz < ph.filesz)
      goto bad;
    if(ph.vaddr + ph.memsz < ph.vaddr)
      goto bad;
    if(ph.vaddr % PGSIZE != 0)
      goto bad;
    uint64 sz1;
    if((sz1 = uvmalloc(pagetable, sz, ph.vaddr + ph.memsz, flags2perm(ph.flags))) == 0)
      goto bad;
    sz = sz1;
    if(loadseg(pagetable, ph.vaddr, ip, ph.off, ph.filesz) < 0)
      goto bad;
  }
  iunlockput(ip);
  end_op();
  ip = 0;

  p = myproc();
  uint64 oldsz = p->sz;

  // exec 成功提交前释放旧进程的 mmap 区域。
  vma_clear(p);

  // 在段结束处分配用户栈页。
  // 最上面一页不设置 PTE_U，作为保护页捕获栈溢出；
  // 其余页作为用户栈。
  sz = PGROUNDUP(sz);
  uint64 stackgap = (aslr_rand() % (ASLR_STACK_PAGES + 1)) * PGSIZE;
  uint64 sz1;
  if((sz1 = uvmalloc(pagetable, sz + stackgap,
                     sz + stackgap + (USERSTACK+1)*PGSIZE, PTE_W)) == 0)
    goto bad;
  sz = sz1;
  uvmclear(pagetable, sz-(USERSTACK+1)*PGSIZE);
  sp = sz;
  stackbase = sp - USERSTACK*PGSIZE;

  // 从高地址向低地址复制 argv 字符串，并记录每个字符串地址。
  for(argc = 0; argv[argc]; argc++) {
    if(argc >= MAXARG)
      goto bad;
    sp -= strlen(argv[argc]) + 1;
    sp -= sp % 16; // RISC-V 要求 sp 16 字节对齐
    if(sp < stackbase)
      goto bad;
    if(copyout(pagetable, sp, argv[argc], strlen(argv[argc]) + 1) < 0)
      goto bad;
    ustack[argc] = sp;
  }
  ustack[argc] = 0;

  // 把 argv 指针数组本身也压入栈，供用户 main(argc, argv) 使用。
  sp -= (argc+1) * sizeof(uint64);
  sp -= sp % 16;
  if(sp < stackbase)
    goto bad;
  if(copyout(pagetable, sp, (char *)ustack, (argc+1)*sizeof(uint64)) < 0)
    goto bad;

  // 用户 main(argc, argv) 的参数约定：
  // argc 通过系统调用返回值（a0）传入，argv 指针数组地址放 a1。
  p->trapframe->a1 = sp;

  // 记录不含路径的程序名，用于进程列表调试。
  for(last=s=path; *s; s++)
    if(*s == '/')
      last = s+1;
  safestrcpy(p->name, last, sizeof(p->name));
    
  // 原子提交：换上新的用户页表、大小、入口 PC 和栈指针，
  // 然后释放旧用户镜像。
  oldpagetable = p->pagetable;
  p->pagetable = pagetable;
  p->sz = sz;
  p->trapframe->epc = elf.entry;  // initial program counter = ulib.c:start()
  p->trapframe->sp = sp; // initial stack pointer
  proc_freepagetable(oldpagetable, oldsz);

  return argc; // 返回值进入 a0，作为用户 main 的 argc

 bad:
  if(pagetable)
    proc_freepagetable(pagetable, sz);
  if(ip){
    iunlockput(ip);
    end_op();
  }
  return -1;
}

// 把 ELF 程序段从文件复制到页表指定的虚拟地址。
// va 必须页对齐，且 va..va+sz 的页必须已经建立映射。
// 成功返回 0，失败返回 -1。
static int
loadseg(pagetable_t pagetable, uint64 va, struct inode *ip, uint offset, uint sz)
{
  uint i, n;
  uint64 pa;

  for(i = 0; i < sz; i += PGSIZE){
    pa = walkaddr(pagetable, va + i);
    if(pa == 0)
      panic("loadseg: address should exist");
    if(sz - i < PGSIZE)
      n = sz - i;
    else
      n = PGSIZE;
    if(readi(ip, 0, (uint64)pa, offset+i, n) != n)
      return -1;
  }
  
  return 0;
}
