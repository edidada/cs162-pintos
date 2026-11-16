#include "tests/lib.h"
#include "tests/main.h"
#include <syscall.h>

/* A 16MiB test fixture leaves room for an exact 8MiB file plus metadata.
   The course's <=8MiB partitions cannot hold an exact 8MiB data file. */
void test_main(void) {
  const unsigned limit = 8 * 1024 * 1024;
  const unsigned sectors[] = {122, 123, 250, 251, 378, 379, 16383};
  unsigned initial = fs_free_sectors();
  ASSERT(create("indexed", 0));
  int fd = open("indexed");
  ASSERT(fd >= 2);
  int number = inumber(fd);
  ASSERT(number > 0);
  unsigned char byte = 0x7b;
  seek(fd, limit - 1);
  ASSERT(write(fd, &byte, 1) == 1 && filesize(fd) == (int)limit);
  ASSERT(write(fd, &byte, 1) == 0 && filesize(fd) == (int)limit);
  for (unsigned i = 0; i < sizeof sectors / sizeof *sectors; ++i) {
    seek(fd, sectors[i] * 512);
    ASSERT(read(fd, &byte, 1) == 1 && byte == 0);
    byte = i + 1;
    seek(fd, sectors[i] * 512);
    ASSERT(write(fd, &byte, 1) == 1);
  }
  close(fd);
  fd = open("indexed");
  ASSERT(fd >= 2 && inumber(fd) == number);
  for (unsigned i = 0; i < sizeof sectors / sizeof *sectors; ++i) {
    seek(fd, sectors[i] * 512);
    ASSERT(read(fd, &byte, 1) == 1 && byte == i + 1);
  }
  seek(fd, limit - 1);
  ASSERT(read(fd, &byte, 1) == 1 && byte == 0x7b);
  ASSERT(read(fd, &byte, 1) == 0);
  ASSERT(create("other", 0));
  int other = open("other");
  ASSERT(other >= 2 && inumber(other) != number);
  close(other);
  ASSERT(remove("other"));
  close(fd);
  ASSERT(remove("indexed"));
  ASSERT(fs_free_sectors() == initial);
  ASSERT(!create("beyond-limit", limit + 1));
  ASSERT(fs_free_sectors() == initial);
  msg("INDEX 8MiB boundaries and full reclamation PASS");
}
