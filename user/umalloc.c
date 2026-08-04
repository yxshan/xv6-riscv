// 用户态堆分配器，采用 K&R《C 程序设计语言》8.7 节中的
// 空闲链表算法：每个分配块前有一个 Header 记录大小和下一块地址，
// 空闲块按地址顺序串成循环链表，malloc 首次适配，free 时合并相邻块。
// 向内核申请内存通过 sbrk() 系统调用完成。
#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"
#include "kernel/param.h"

// 分配器设计来自 Kernighan 和 Ritchie
// 《C 程序设计语言》第二版 8.7 节。

typedef long Align;

// Header 中的 Align 字段只用于对齐，使每个块都按 long 对齐。
union header {
  struct {
    union header *ptr;
    uint size;
  } s;
  Align x;
};

typedef union header Header;

static Header base;
static Header *freep;

void
free(void *ap)
{
  Header *bp, *p;

  // 定位块头，并在循环链表中寻找合适的插入位置。
  bp = (Header*)ap - 1;
  for(p = freep; !(bp > p && bp < p->s.ptr); p = p->s.ptr)
    if(p >= p->s.ptr && (bp > p || bp < p->s.ptr))
      break;
  if(bp + bp->s.size == p->s.ptr){
    // 与下一个空闲块相邻：合并。
    bp->s.size += p->s.ptr->s.size;
    bp->s.ptr = p->s.ptr->s.ptr;
  } else
    bp->s.ptr = p->s.ptr;
  if(p + p->s.size == bp){
    // 与上一个空闲块相邻：合并。
    p->s.size += bp->s.size;
    p->s.ptr = bp->s.ptr;
  } else
    p->s.ptr = bp;
  freep = p;
}

static Header*
morecore(uint nu)
{
  char *p;
  Header *hp;

  if(nu < 4096)  // 一次至少向内核申请 4096 个 Header 单位。
    nu = 4096;
  p = sbrk(nu * sizeof(Header));  // 通过 sbrk 扩大进程堆
  if(p == SBRK_ERROR)
    return 0;
  hp = (Header*)p;
  hp->s.size = nu;
  free((void*)(hp + 1));
  return freep;
}

void*
malloc(uint nbytes)
{
  Header *p, *prevp;
  uint nunits;

  // 把字节数换算成 Header 单位，并额外多算一个 Header 自身。
  nunits = (nbytes + sizeof(Header) - 1)/sizeof(Header) + 1;
  if((prevp = freep) == 0){
    // 首次调用：初始化循环链表。
    base.s.ptr = freep = prevp = &base;
    base.s.size = 0;
  }
  for(p = prevp->s.ptr; ; prevp = p, p = p->s.ptr){
    if(p->s.size >= nunits){
      if(p->s.size == nunits)
        // 恰好相等：直接摘除该块。
        prevp->s.ptr = p->s.ptr;
      else {
        // 块更大：从尾部切出所需大小，剩余部分留在链表中。
        p->s.size -= nunits;
        p += p->s.size;
        p->s.size = nunits;
      }
      freep = prevp;
      return (void*)(p + 1);
    }
    if(p == freep)  // 绕了一圈都没找到，向内核申请更多内存。
      if((p = morecore(nunits)) == 0)
        return 0;
  }
}
