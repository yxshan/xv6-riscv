// signaltest：验证用户态信号处理。

#include "kernel/types.h"
#include "kernel/signal.h"
#include "user/user.h"

static void
handler(int sig)
{
  printf("handler sig=%d\n", sig);
  sigreturn();
}

int
main(void)
{
  int pid = getpid();

  if(signal(SIGUSR1, (uint64)handler) < 0){
    printf("signaltest: signal failed\n");
    exit(1);
  }
  if(sigkill(pid, SIGUSR1) < 0){
    printf("signaltest: sigkill failed\n");
    exit(1);
  }

  printf("signaltest: after\n");
  exit(0);
}
