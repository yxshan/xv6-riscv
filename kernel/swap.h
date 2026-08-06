#ifndef XV6_SWAP_H
#define XV6_SWAP_H

#include "types.h"

#define SWAP_PAGES 2048
#define SWAP_BLOCKS_PER_PAGE 4

struct swapinfo {
  uint64 total_pages;
  uint64 free_pages;
  uint64 swapouts;
  uint64 swapins;
};

#endif
