//
// 控制台输入输出。
// 它把 UART 串口包装成用户可读写的文件（设备 CONSOLE）。
// 输入按“行”为单位：中断处理器把字符放入环形缓冲区，
// 用户 read() 系统调用睡眠等待一整行到来。
// 支持特殊输入字符：
//   换行     -- 行结束
//   Ctrl-H   -- 退格
//   Ctrl-U   -- 删除整行
//   Ctrl-D   -- 文件结束
//   Ctrl-P   -- 打印进程列表
//

#include <stdarg.h>

#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "file.h"
#include "memlayout.h"
#include "riscv.h"
#include "defs.h"
#include "proc.h"

#define BACKSPACE 0x100  // erase the last output character
#define C(x)  ((x)-'@')  // Control-x

//
// 向串口发送一个字符，不使用中断也不睡眠，
// 因此可以从中断上下文安全调用（printf 和输入回显都在用）。
//
void
consputc(int c)
{
  if(c == BACKSPACE){
    // 退格键：先回退、打印空格、再回退，把屏幕上的字符擦掉。
    uartputc_sync('\b'); uartputc_sync(' '); uartputc_sync('\b');
  } else {
    uartputc_sync(c);
  }
}

struct {
  struct spinlock lock;
  int fg_pgid;  // 当前终端前台进程组
  
  // input circular buffer
#define INPUT_BUF_SIZE 128
  char buf[INPUT_BUF_SIZE];
  uint r;  // Read index
  uint w;  // Write index
  uint e;  // Edit index
  int esc;           // 正在接收终端转义序列
  int esc_bracket;   // 已收到 ESC [
} cons;

int
console_set_fg(int pgid)
{
  if(pgid <= 0)
    return -1;
  acquire(&cons.lock);
  cons.fg_pgid = pgid;
  release(&cons.lock);
  return 0;
}

int
console_get_fg(void)
{
  int pgid;

  acquire(&cons.lock);
  pgid = cons.fg_pgid;
  release(&cons.lock);
  return pgid;
}

//
// 用户 write() 到控制台的入口。
// 使用 uartwrite()，它依赖睡眠和 UART 发送完成中断。
//
int
consolewrite(struct file *f, int user_src, uint64 src, int n)
{
  char buf[32]; // move batches from user space to uart.
  int i = 0;

  (void)f;

  while(i < n){
    int nn = sizeof(buf);
    if(nn > n - i)
      nn = n - i;
    if(either_copyin(buf, user_src, src+i, nn) == -1)
      break;
    uartwrite(buf, nn);
    i += nn;
  }

  return i;
}

//
// 用户 read() 从控制台读取的入口。
// 通常复制一整行到用户缓冲区；用户/内核地址由 user_dst 区分。
//
int
consoleread(struct file *f, int user_dst, uint64 dst, int n)
{
  uint target;
  int c;
  char cbuf;

  (void)f;

  target = n;
  acquire(&cons.lock);
  while(n > 0){
    // 等待中断处理器把输入放入 cons.buf。
    while(cons.r == cons.w){
      if(killed(myproc())){
        release(&cons.lock);
        return -1;
      }
      sleep(&cons.r, &cons.lock);
      if(myproc()->stop_pending){
        release(&cons.lock);
        return -1;
      }
    }

    c = cons.buf[cons.r++ % INPUT_BUF_SIZE];

    if(c == C('D')){  // end-of-file
      if(n < target){
        // 把 ^D 留到下一次，确保本次调用能返回 0 字节。
        cons.r--;
      }
      break;
    }

    // 复制输入字节到用户缓冲区。
    cbuf = c;
    if(either_copyout(user_dst, dst, &cbuf, 1) == -1)
      break;

    dst++;
    --n;

    if(c == '\n'){
      // a whole line has arrived, return to
      // the user-level read().
      break;
    }
  }
  release(&cons.lock);

  return target - n;
}

//
// 控制台输入中断处理器，uartintr() 对每个输入字符调用。
// 处理退格/删行等编辑操作，把字符存入 cons.buf，
// 并在一整行到达时唤醒 consoleread()。
//
static void
cons_push_char(int c)
{
  if(cons.e - cons.r < INPUT_BUF_SIZE){
    cons.buf[cons.e++ % INPUT_BUF_SIZE] = c;
    cons.w = cons.e;
    wakeup(&cons.r);
  }
}

void
consoleintr(int c)
{
  acquire(&cons.lock);

  // 吞掉方向键/Delete 等终端转义序列，避免被当作普通字符回显和写入缓冲区。
  if(cons.esc){
    if(c == '['){
      cons.esc_bracket = 1;
      release(&cons.lock);
      return;
    }
    if(cons.esc_bracket &&
       ((c >= '0' && c <= '9') || c == ';' || c == '?')){
      release(&cons.lock);
      return;
    }
    int key = 0;
    if(c == 'A')
      key = 1;  // Up
    else if(c == 'B')
      key = 2;  // Down
    cons.esc = 0;
    cons.esc_bracket = 0;
    if(key)
      cons_push_char(key);
    release(&cons.lock);
    return;
  }
  if(c == 0x1b){
    cons.esc = 1;
    cons.esc_bracket = 0;
    release(&cons.lock);
    return;
  }

  switch(c){
  case C('C'):  // Ctrl-C: 中断前台进程组。
    if(cons.fg_pgid > 0)
      kkillpg(cons.fg_pgid, SIGINT);
    break;
  case C('Z'):  // Ctrl-Z: 停止前台进程组。
    if(cons.fg_pgid > 0)
      kkillpg(cons.fg_pgid, SIGTSTP);
    break;
  case C('P'):  // Print process list.
    procdump();
    break;
  case C('U'):  // Kill line.
    cons_push_char(0x15);
    break;
  case C('H'): // Backspace
  case '\x7f': // Delete key
    cons_push_char(0x08);
    break;
  default:
    if(c != 0 && cons.e-cons.r < INPUT_BUF_SIZE){
      c = (c == '\r') ? '\n' : c;

      // 回显字符。
      consputc(c);

      // 实时交付给用户态，由 shell 完成行编辑与历史切换。
      cons_push_char(c);
    }
    break;
  }
  
  release(&cons.lock);
}

void
consoleinit(void)
{
  initlock(&cons.lock, "cons");
  cons.fg_pgid = 0;
  cons.esc = 0;
  cons.esc_bracket = 0;

  uartinit();

  // 把控制台设备挂到 devsw 表，read/write 系统调用由此分派。
  devsw[CONSOLE].read = consoleread;
  devsw[CONSOLE].write = consolewrite;
}
