#ifndef SIGNAL_H
#define SIGNAL_H

#define NSIG 32

#define SIG_DFL (-1UL)
#define SIG_IGN (-2UL)

#define SIG_BLOCK 0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2

#define SIGINT  2
#define SIGKILL 9
#define SIGUSR1 10
#define SIGUSR2 12
#define SIGSTOP 17
#define SIGCONT 18

#endif
