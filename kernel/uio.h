#ifndef UIO_H
#define UIO_H

#include "types.h"

struct iovec {
  void *iov_base;
  uint64 iov_len;
};

#endif
