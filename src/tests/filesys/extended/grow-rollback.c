#include "tests/lib.h"
#include "tests/main.h"
#include <string.h>
#include <syscall.h>

static unsigned char block[512], large[65536];
void test_main(void) {
  unsigned initial = fs_free_sectors();
  ASSERT(!create("oversized", 8 * 1024 * 1024));
  ASSERT(fs_free_sectors() == initial);
  ASSERT(create("dupe", 512));
  unsigned before = fs_free_sectors();
  for (int i = 0; i < 5; ++i) {
    ASSERT(!create("dupe", 300000));
    ASSERT(fs_free_sectors() == before);
  }
  ASSERT(remove("dupe"));
  ASSERT(fs_free_sectors() == initial);
  ASSERT(create("fill", 0));
  int fd = open("fill");
  ASSERT(fd >= 2);
  memset(block, 0x5a, sizeof block);
  while (fs_free_sectors() > 20)
    ASSERT(write(fd, block, sizeof block) == sizeof block);
  int length = filesize(fd);
  before = fs_free_sectors();
  for (int i = 0; i < 5; ++i) {
    ASSERT(write(fd, large, sizeof large) == 0);
    ASSERT(filesize(fd) == length && fs_free_sectors() == before);
  }
  seek(fd, length - sizeof block);
  ASSERT(read(fd, block, sizeof block) == sizeof block);
  for (unsigned i = 0; i < sizeof block; ++i)
    ASSERT(block[i] == 0x5a);
  seek(fd, length);
  ASSERT(write(fd, block, sizeof block) == sizeof block);
  close(fd);
  ASSERT(remove("fill"));
  ASSERT(fs_free_sectors() == initial);
  msg("ROLLBACK free=%u recovered=%u", initial, fs_free_sectors());
  msg("PASS");
}
