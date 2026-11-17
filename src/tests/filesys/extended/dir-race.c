#include "tests/lib.h"
#include "tests/main.h"
#include <stdio.h>
#include <string.h>
#include <syscall.h>

static unsigned char data[512];
static int worker(unsigned id) {
  int gate = open("race-gate");
  ASSERT(gate >= 2);
  unsigned char ready = 1, go = 0;
  seek(gate, id - 1);
  ASSERT(write(gate, &ready, 1) == 1);
  do {
    seek(gate, 2);
    ASSERT(read(gate, &go, 1) == 1);
  } while (!go);
  close(gate);
  int made = 0;
  for (unsigned i = 0; i < 50; ++i) {
    char path[32];
    snprintf(path, sizeof path, "race/file-%u", i);
    if (create(path, sizeof data)) {
      int fd = open(path);
      ASSERT(fd >= 2);
      memset(data, 0xa5, sizeof data);
      data[0] = id;
      data[1] = i;
      ASSERT(write(fd, data, sizeof data) == sizeof data);
      close(fd);
      ++made;
    }
  }
  return made;
}
void test_main(void) {
  unsigned initial = fs_free_sectors();
  for (unsigned round = 0; round < 5; ++round) {
    ASSERT(mkdir("race") && create("race-gate", 3));
    int gate = open("race-gate");
    ASSERT(gate >= 2);
    int a = fork();
    ASSERT(a >= 0);
    if (!a)
      exit(worker(1));
    int b = fork();
    ASSERT(b >= 0);
    if (!b)
      exit(worker(2));
    unsigned char flags[3];
    do {
      seek(gate, 0);
      ASSERT(read(gate, flags, 3) == 3);
    } while (!flags[0] || !flags[1]);
    flags[2] = 1;
    seek(gate, 2);
    ASSERT(write(gate, flags + 2, 1) == 1);
    int successes = wait(a);
    ASSERT(successes >= 0);
    int other = wait(b);
    ASSERT(other >= 0 && successes + other == 50);
    close(gate);
    for (unsigned i = 0; i < 50; ++i) {
      char path[32];
      snprintf(path, sizeof path, "race/file-%u", i);
      int fd = open(path);
      ASSERT(fd >= 2 && read(fd, data, sizeof data) == sizeof data);
      ASSERT((data[0] == 1 || data[0] == 2) && data[1] == i);
      for (unsigned j = 2; j < sizeof data; ++j)
        ASSERT(data[j] == 0xa5);
      close(fd);
      ASSERT(remove(path));
    }
    ASSERT(remove("race") && remove("race-gate"));
    ASSERT(fs_free_sectors() == initial);
  }
  msg("RACE unique names and space recovery PASS");
}
