// P3-K8 device and clock tests.
//
// Each test function runs in its own child process under the usertests driver.
#include "tests.h"

void
clock_device(char *s)
{
  struct timespec a, b, rtc;
  int fd;
  uint64 na, nb;

  fd = open("clock", O_RDONLY);
  if(fd < 0 || read(fd, &a, sizeof(a)) != sizeof(a)){
    printf("%s: /dev/clock failed\n", s);
    exit(1);
  }
  close(fd);
  pause(5);
  fd = open("clock", O_RDONLY);
  if(fd < 0 || read(fd, &b, sizeof(b)) != sizeof(b)){
    printf("%s: /dev/clock second read failed\n", s);
    exit(1);
  }
  close(fd);
  na = (uint64)a.tv_sec * 1000000000UL + a.tv_nsec;
  nb = (uint64)b.tv_sec * 1000000000UL + b.tv_nsec;
  if(nb <= na || nb - na < 100000000){
    printf("%s: /dev/clock did not advance\n", s);
    exit(1);
  }

  fd = open("rtc", O_RDONLY);
  if(fd < 0 || read(fd, &rtc, sizeof(rtc)) != sizeof(rtc)){
    printf("%s: /dev/rtc failed\n", s);
    exit(1);
  }
  close(fd);
  if(rtc.tv_sec < 1767225600L){
    printf("%s: /dev/rtc epoch too small\n", s);
    exit(1);
  }
  exit(0);
}

struct test k8_quicktests[] = {
  {clock_device, "clock_device"},
  { 0, 0},
};

struct test k8_slowtests[] = {
  { 0, 0},
};
