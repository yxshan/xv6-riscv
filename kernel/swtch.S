# 内核上下文切换。
#
#   void swtch(struct context *old, struct context *new);
#
# 把当前执行流的寄存器保存到 old，再从 new 恢复另一段执行流的寄存器。
# 所谓“切换进程”，本质就是切换内核栈(sp)和返回地址(ra)，
# 再加上 callee-saved 的 s0-s11；临时寄存器无需保存，
# 因为它们是调用者保存(volatile)寄存器。
#
# 两个典型场景：
# 1. 调度器通过 swtch(&c->context, &p->context) 切入进程；
# 2. 进程让出 CPU 时通过 swtch(&p->context, &c->context) 切回调度器。


.globl swtch
swtch:
        # 先把当前执行流的 ra/sp 和 callee-saved 寄存器存到 old。
        sd ra, 0(a0)
        sd sp, 8(a0)
        sd s0, 16(a0)
        sd s1, 24(a0)
        sd s2, 32(a0)
        sd s3, 40(a0)
        sd s4, 48(a0)
        sd s5, 56(a0)
        sd s6, 64(a0)
        sd s7, 72(a0)
        sd s8, 80(a0)
        sd s9, 88(a0)
        sd s10, 96(a0)
        sd s11, 104(a0)

        # 再从 new 恢复目标执行流的寄存器，然后 ret 跳转到目标 ra。
        ld ra, 0(a1)
        ld sp, 8(a1)
        ld s0, 16(a1)
        ld s1, 24(a1)
        ld s2, 32(a1)
        ld s3, 40(a1)
        ld s4, 48(a1)
        ld s5, 56(a1)
        ld s6, 64(a1)
        ld s7, 72(a1)
        ld s8, 80(a1)
        ld s9, 88(a1)
        ld s10, 96(a1)
        ld s11, 104(a1)
        
        ret

	
