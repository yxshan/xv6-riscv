// clone 子线程入口。
// 内核把 a0=fn、a1=arg 交给这里；stub 调用 fn(arg) 后以 0 退出。
.section .text
.globl clone_stub
clone_stub:
  mv s0, a0
  mv a0, a1
  jalr ra, s0
  li a0, 0
  tail exit
