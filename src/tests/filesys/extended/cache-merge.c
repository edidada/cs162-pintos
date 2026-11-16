#include "tests/lib.h"
#include "tests/main.h"
#include <syscall.h>

void test_main(void) {
  ASSERT(create("merge-file", 65536));
  cache_reset();
  int fd = open("merge-file");
  ASSERT(fd >= 2);
  for (unsigned i = 0; i < 65536; ++i) {
    unsigned char value = i % 251;
    ASSERT(write(fd, &value, 1) == 1);
  }
  cache_flush();
  struct cache_stats result;
  cache_stats(&result);
  ASSERT(result.writes >= 128 && result.writes <= 140);
  seek(fd, 0);
  for (unsigned i = 0; i < 65536; ++i) {
    unsigned char value;
    ASSERT(read(fd, &value, 1) == 1 && value == i % 251);
  }
  close(fd);
  msg("CACHE MERGE writes=%llu", result.writes);
  msg("PASS");
}
