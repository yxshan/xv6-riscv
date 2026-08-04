#ifndef SHM_H
#define SHM_H

#include "types.h"

#define NSHM 8
#define SHM_MAX_PAGES 32

struct shmseg {
  int used;
  int id;
  int key;
  int ref;
  int npages;
  uint64 va;
  uint64 pages[SHM_MAX_PAGES];
};

#endif
