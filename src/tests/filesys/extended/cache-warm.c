#include "tests/lib.h"
#include "tests/main.h"
#include <syscall.h>

static unsigned char buffer[512];
static void read_file(void) {
  int fd = open("warm-file");
  ASSERT(fd >= 2);
  for (unsigned pos = 0; pos < 8192; pos += sizeof buffer) {
    ASSERT(read(fd, buffer, sizeof buffer) == sizeof buffer);
    for (unsigned j = 0; j < sizeof buffer; ++j)
      ASSERT(buffer[j] == (pos + j) % 251);
  }
  close(fd);
}
void test_main(void) {
  ASSERT(create("warm-file", 8192));
  int fd = open("warm-file");
  ASSERT(fd >= 2);
  for (unsigned pos = 0; pos < 8192; pos += sizeof buffer) {
    for (unsigned j = 0; j < sizeof buffer; ++j)
      buffer[j] = (pos + j) % 251;
    ASSERT(write(fd, buffer, sizeof buffer) == sizeof buffer);
  }
  close(fd);
  cache_reset();
  read_file();
  struct cache_stats cold, warm;
  cache_stats(&cold);
  read_file();
  cache_stats(&warm);
  ASSERT(cold.misses > 0 && warm.misses == cold.misses && warm.hits > cold.hits);
  msg("CACHE WARM cold=%llu warm=%llu", cold.misses, warm.misses - cold.misses);
  msg("PASS");
}
