//
// 16550a UART 串口驱动。
//
// UART 是 xv6 与 QEMU 终端交互的通道：内核 printf、shell 输入输出
// 都通过它完成。其控制寄存器通过内存映射方式访问，地址由 UART0 指定。
//

#include "types.h"
#include "param.h"
#include "memlayout.h"
#include "riscv.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

// UART 寄存器是内存映射的：Reg(reg) 返回 UART0 + reg 的地址。
#define Reg(reg) ((volatile unsigned char *)(UART0 + (reg)))

#define ReadReg(reg) (*(Reg(reg)))
#define WriteReg(reg, v) (*(Reg(reg)) = (v))

// UART 控制寄存器。同一偏移地址读写含义可能不同，
// 例如 RHR 用于读取输入字节，THR 用于写入输出字节。
#define RHR 0                 // receive holding register (for input bytes)
#define THR 0                 // transmit holding register (for output bytes)
#define IER 1                 // interrupt enable register
#define IER_RX_ENABLE (1<<0)
#define IER_TX_ENABLE (1<<1)
#define FCR 2                 // FIFO control register
#define FCR_FIFO_ENABLE (1<<0)
#define FCR_FIFO_CLEAR (3<<1) // clear the content of the two FIFOs
#define ISR 2                 // interrupt status register
#define LCR 3                 // line control register
#define LCR_EIGHT_BITS (3<<0)
#define LCR_BAUD_LATCH (1<<7) // special mode to set baud rate
#define LSR 5                 // line status register
#define LSR_RX_READY (1<<0)   // input is waiting to be read from RHR
#define LSR_TX_IDLE (1<<5)    // THR can accept another character to send

// for sending threads to synchronize with uart "ready" interrupts.
static struct spinlock tx_lock;
static int tx_busy;           // is the UART busy sending?
static int tx_chan;           // &tx_chan is the "wait channel"

extern volatile int panicking; // from printf.c
extern volatile int panicked; // from printf.c

void
uartinit(void)
{
  // 先关闭所有 UART 中断，再逐步配置。
  WriteReg(IER, 0x00);

  // 进入波特率锁存模式，设置 38.4K 波特率。
  WriteReg(LCR, LCR_BAUD_LATCH);

  // 波特率低字节。
  WriteReg(0, 0x03);

  // 波特率高字节。
  WriteReg(1, 0x00);

  // 退出波特率模式，设置为 8 数据位、无校验。
  WriteReg(LCR, LCR_EIGHT_BITS);

  // 复位并启用发送/接收 FIFO。
  WriteReg(FCR, FCR_FIFO_ENABLE | FCR_FIFO_CLEAR);

  // 启用发送和接收中断。
  WriteReg(IER, IER_TX_ENABLE | IER_RX_ENABLE);

  initlock(&tx_lock, "uart");
}

// 向 UART 发送 buf[] 中的 n 个字节。
// 若 UART 正忙则睡眠等待发送完成中断，因此只能在进程上下文
// （write 系统调用）中调用，不能从中断上下文调用。
void
uartwrite(char buf[], int n)
{
  acquire(&tx_lock);

  int i = 0;
  while(i < n){ 
    while(tx_busy != 0){
      // 等待 UART 发送完成中断把 tx_busy 清 0。
      sleep(&tx_chan, &tx_lock);
    }   
    
    WriteReg(THR, buf[i]);
    i += 1;
    // 置忙：硬件发送完当前字节后会产生中断来复位。
    tx_busy = 1;
  }

  release(&tx_lock);
}


// 不使用中断、以自旋等待方式向 UART 写一个字节。
// 供内核 printf() 和输入回显使用，可以在中断上下文中调用。
void
uartputc_sync(int c)
{
  if(panicking == 0)
    push_off();

  if(panicked){
    for(;;)
      ;
  }

  // 轮询等待 UART 可以接收下一个字符。
  while((ReadReg(LSR) & LSR_TX_IDLE) == 0)
    ;
  WriteReg(THR, c);

  if(panicking == 0)
    pop_off();
}

// 尝试读取一个输入字符；没有输入时返回 -1。
int
uartgetc(void)
{
  if(ReadReg(LSR) & LSR_RX_READY){
    // 输入数据已就绪。
    return ReadReg(RHR);
  } else {
    return -1;
  }
}

// UART 中断处理：输入到达或输出就绪时触发，由 devintr() 调用。
void
uartintr(void)
{
  ReadReg(ISR); // 读取中断状态，确认中断

  acquire(&tx_lock);
  if(ReadReg(LSR) & LSR_TX_IDLE){
    // UART 已完成发送，唤醒等待中的写线程。
    tx_busy = 0;
    wakeup(&tx_chan);
  }
  release(&tx_lock);

  // 读取并处理所有到达的输入字符。
  while(1){
    int c = uartgetc();
    if(c == -1)
      break;
    consoleintr(c);
  }
}
