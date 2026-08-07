#ifndef POLL_H
#define POLL_H

#include "types.h"

#define FD_SETSIZE 16
typedef uint64 fd_set;

#define FD_ZERO(s) (*(s) = 0)
#define FD_SET(fd, s) (*(s) |= (1UL << (fd)))
#define FD_CLR(fd, s) (*(s) &= ~(1UL << (fd)))
#define FD_ISSET(fd, s) (*(s) & (1UL << (fd)))

struct pollfd {
  int fd;
  short events;
  short revents;
};

#define POLLIN  0x001
#define POLLOUT 0x004
#define POLLERR 0x008
#define POLLHUP 0x010
#define POLLNVAL 0x020

#endif
