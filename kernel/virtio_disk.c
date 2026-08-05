//
// driver for qemu's virtio disk device.
// uses qemu's mmio interface to virtio.
//
// qemu ... -drive file=fs.img,if=none,format=raw,id=x0 -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0
//

#include "types.h"
#include "riscv.h"
#include "defs.h"
#include "param.h"
#include "memlayout.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "virtio.h"

// 第 d 个 virtio mmio 设备寄存器 r 的地址。
#define R(d, r) ((volatile uint32 *)((uint64)VIRTIO0 + (uint64)(d)*0x1000 + (r)))

struct disk {
  // a set (not a ring) of DMA descriptors, with which the
  // driver tells the device where to read and write individual
  // disk operations. there are NUM descriptors.
  // most commands consist of a "chain" (a linked list) of a couple of
  // these descriptors.
  struct virtq_desc *desc;

  // a ring in which the driver writes descriptor numbers
  // that the driver would like the device to process.  it only
  // includes the head descriptor of each chain. the ring has
  // NUM elements.
  struct virtq_avail *avail;

  // a ring in which the device writes descriptor numbers that
  // the device has finished processing (just the head of each chain).
  // there are NUM used ring entries.
  struct virtq_used *used;

  // our own book-keeping.
  char free[NUM];  // is a descriptor free?
  uint16 used_idx; // we've looked this far in used[2..NUM].

  // track info about in-flight operations,
  // for use when completion interrupt arrives.
  // indexed by first descriptor index of chain.
  struct {
    struct buf *b;
    char status;
  } info[NUM];

  // disk command headers.
  // one-for-one with descriptors, for convenience.
  struct virtio_blk_req ops[NUM];
  
  struct spinlock vdisk_lock;
  
};

static struct disk disks[NDISK];

void
virtio_disk_init(void)
{
  for(int d = 0; d < NDISK; d++){
    struct disk *disk = &disks[d];
    uint32 status = 0;

    initlock(&disk->vdisk_lock, "virtio_disk");

    if(*R(d, VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976 ||
       *R(d, VIRTIO_MMIO_VERSION) != 2 ||
       *R(d, VIRTIO_MMIO_DEVICE_ID) != 2 ||
       *R(d, VIRTIO_MMIO_VENDOR_ID) != 0x554d4551){
      panic("could not find virtio disk");
    }
  
    // reset device
    *R(d, VIRTIO_MMIO_STATUS) = status;

    // set ACKNOWLEDGE status bit
    status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
    *R(d, VIRTIO_MMIO_STATUS) = status;

    // set DRIVER status bit
    status |= VIRTIO_CONFIG_S_DRIVER;
    *R(d, VIRTIO_MMIO_STATUS) = status;

    // negotiate features
    uint64 features = *R(d, VIRTIO_MMIO_DEVICE_FEATURES);
    features &= ~(1 << VIRTIO_BLK_F_RO);
    features &= ~(1 << VIRTIO_BLK_F_SCSI);
    features &= ~(1 << VIRTIO_BLK_F_CONFIG_WCE);
    features &= ~(1 << VIRTIO_BLK_F_MQ);
    features &= ~(1 << VIRTIO_F_ANY_LAYOUT);
    features &= ~(1 << VIRTIO_RING_F_EVENT_IDX);
    features &= ~(1 << VIRTIO_RING_F_INDIRECT_DESC);
    *R(d, VIRTIO_MMIO_DRIVER_FEATURES) = features;

    // tell device that feature negotiation is complete.
    status |= VIRTIO_CONFIG_S_FEATURES_OK;
    *R(d, VIRTIO_MMIO_STATUS) = status;

    // re-read status to ensure FEATURES_OK is set.
    status = *R(d, VIRTIO_MMIO_STATUS);
    if(!(status & VIRTIO_CONFIG_S_FEATURES_OK))
      panic("virtio disk FEATURES_OK unset");

    // initialize queue 0.
    *R(d, VIRTIO_MMIO_QUEUE_SEL) = 0;

    // ensure queue 0 is not in use.
    if(*R(d, VIRTIO_MMIO_QUEUE_READY))
      panic("virtio disk should not be ready");

    // check maximum queue size.
    uint32 max = *R(d, VIRTIO_MMIO_QUEUE_NUM_MAX);
    if(max == 0)
      panic("virtio disk has no queue 0");
    if(max < NUM)
      panic("virtio disk max queue too short");

    // allocate and zero queue memory.
    disk->desc = kalloc();
    disk->avail = kalloc();
    disk->used = kalloc();
    if(!disk->desc || !disk->avail || !disk->used)
      panic("virtio disk kalloc");
    memset(disk->desc, 0, PGSIZE);
    memset(disk->avail, 0, PGSIZE);
    memset(disk->used, 0, PGSIZE);

    // set queue size.
    *R(d, VIRTIO_MMIO_QUEUE_NUM) = NUM;

    // write physical addresses.
    *R(d, VIRTIO_MMIO_QUEUE_DESC_LOW) = (uint64)disk->desc;
    *R(d, VIRTIO_MMIO_QUEUE_DESC_HIGH) = (uint64)disk->desc >> 32;
    *R(d, VIRTIO_MMIO_DRIVER_DESC_LOW) = (uint64)disk->avail;
    *R(d, VIRTIO_MMIO_DRIVER_DESC_HIGH) = (uint64)disk->avail >> 32;
    *R(d, VIRTIO_MMIO_DEVICE_DESC_LOW) = (uint64)disk->used;
    *R(d, VIRTIO_MMIO_DEVICE_DESC_HIGH) = (uint64)disk->used >> 32;

    // queue is ready.
    *R(d, VIRTIO_MMIO_QUEUE_READY) = 0x1;

    // all NUM descriptors start out unused.
    for(int i = 0; i < NUM; i++)
      disk->free[i] = 1;

    // tell device we're completely ready.
    status |= VIRTIO_CONFIG_S_DRIVER_OK;
    *R(d, VIRTIO_MMIO_STATUS) = status;
  }
}

// find a free descriptor, mark it non-free, return its index.
static int
alloc_desc(struct disk *disk)
{
  for(int i = 0; i < NUM; i++){
    if(disk->free[i]){
      disk->free[i] = 0;
      return i;
    }
  }
  return -1;
}

// mark a descriptor as free.
static void
free_desc(struct disk *disk, int i)
{
  if(i >= NUM)
    panic("free_desc 1");
  if(disk->free[i])
    panic("free_desc 2");
  disk->desc[i].addr = 0;
  disk->desc[i].len = 0;
  disk->desc[i].flags = 0;
  disk->desc[i].next = 0;
  disk->free[i] = 1;
  wakeup(&disk->free[0]);
}

// free a chain of descriptors.
static void
free_chain(struct disk *disk, int i)
{
  while(1){
    int flag = disk->desc[i].flags;
    int nxt = disk->desc[i].next;
    free_desc(disk, i);
    if(flag & VRING_DESC_F_NEXT)
      i = nxt;
    else
      break;
  }
}

// allocate three descriptors (they need not be contiguous).
// disk transfers always use three descriptors.
static int
alloc3_desc(struct disk *disk, int *idx)
{
  for(int i = 0; i < 3; i++){
    idx[i] = alloc_desc(disk);
    if(idx[i] < 0){
      for(int j = 0; j < i; j++)
        free_desc(disk, idx[j]);
      return -1;
    }
  }
  return 0;
}

void
virtio_disk_rw(struct buf *b, int write)
{
  struct disk *disk;
  int d;
  uint64 sector = b->blockno * (BSIZE / 512);

  if(b->dev < ROOTDEV || b->dev >= ROOTDEV + NDISK)
    panic("virtio_disk_rw: bad dev");
  d = b->dev - ROOTDEV;
  disk = &disks[d];

  acquire(&disk->vdisk_lock);

  // allocate the three descriptors.
  int idx[3];
  while(1){
    if(alloc3_desc(disk, idx) == 0)
      break;
    sleep(&disk->free[0], &disk->vdisk_lock);
  }

  struct virtio_blk_req *buf0 = &disk->ops[idx[0]];

  if(write)
    buf0->type = VIRTIO_BLK_T_OUT;
  else
    buf0->type = VIRTIO_BLK_T_IN;
  buf0->reserved = 0;
  buf0->sector = sector;

  disk->desc[idx[0]].addr = (uint64) buf0;
  disk->desc[idx[0]].len = sizeof(struct virtio_blk_req);
  disk->desc[idx[0]].flags = VRING_DESC_F_NEXT;
  disk->desc[idx[0]].next = idx[1];

  disk->desc[idx[1]].addr = (uint64) b->data;
  disk->desc[idx[1]].len = BSIZE;
  if(write)
    disk->desc[idx[1]].flags = 0;
  else
    disk->desc[idx[1]].flags = VRING_DESC_F_WRITE;
  disk->desc[idx[1]].flags |= VRING_DESC_F_NEXT;
  disk->desc[idx[1]].next = idx[2];

  disk->info[idx[0]].status = 0xff;
  disk->desc[idx[2]].addr = (uint64) &disk->info[idx[0]].status;
  disk->desc[idx[2]].len = 1;
  disk->desc[idx[2]].flags = VRING_DESC_F_WRITE;
  disk->desc[idx[2]].next = 0;

  b->disk = 1;
  disk->info[idx[0]].b = b;
  disk->avail->ring[disk->avail->idx % NUM] = idx[0];
  __sync_synchronize();
  disk->avail->idx += 1;
  __sync_synchronize();
  *R(d, VIRTIO_MMIO_QUEUE_NOTIFY) = 0;

  while(b->disk == 1)
    sleep(b, &disk->vdisk_lock);

  disk->info[idx[0]].b = 0;
  free_chain(disk, idx[0]);

  release(&disk->vdisk_lock);
}

void
virtio_disk_intr(int diskno)
{
  struct disk *disk;

  if(diskno < 0 || diskno >= NDISK)
    panic("virtio_disk_intr: bad disk");
  disk = &disks[diskno];

  acquire(&disk->vdisk_lock);

  *R(diskno, VIRTIO_MMIO_INTERRUPT_ACK) =
    *R(diskno, VIRTIO_MMIO_INTERRUPT_STATUS) & 0x3;
  __sync_synchronize();

  while(disk->used_idx != disk->used->idx){
    __sync_synchronize();
    int id = disk->used->ring[disk->used_idx % NUM].id;

    if(disk->info[id].status != 0)
      panic("virtio_disk_intr status");

    struct buf *b = disk->info[id].b;
    b->disk = 0;
    wakeup(b);
    disk->used_idx += 1;
  }

  release(&disk->vdisk_lock);
}
