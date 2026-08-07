#ifndef SEM_H
#define SEM_H

#include "types.h"

#define SEM_NSEMS_MAX 32
#define SEM_SET_MAX 64

#define IPC_PRIVATE 0
#define IPC_CREAT 0x1000
#define IPC_NOWAIT 0x2000

#define IPC_RMID 0
#define GETVAL 12
#define SETVAL 16

struct sembuf {
  ushort sem_num;
  short sem_op;
  short sem_flg;
};

#endif
