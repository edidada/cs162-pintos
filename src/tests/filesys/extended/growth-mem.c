/* Used only by the isolated CI build which injects one allocation failure. */
#include "tests/lib.h"
#include "tests/main.h"
#include <string.h>
#include <syscall.h>
static unsigned char buffer[192 * 1024];
void test_main(void) {
  unsigned initial = fs_free_sectors();
  ASSERT(create("memory", 0));
  int fd = open("memory");
  ASSERT(fd >= 2);
  unsigned before = fs_free_sectors();
  memset(buffer, 0x63, sizeof buffer);
  ASSERT(write(fd, buffer, sizeof buffer) == 0);
  ASSERT(filesize(fd) == 0 && fs_free_sectors() == before);
  ASSERT(write(fd, buffer, sizeof buffer) == sizeof buffer);
  ASSERT(filesize(fd) == sizeof buffer);
  seek(fd, 0);
  memset(buffer, 0, sizeof buffer);
  ASSERT(read(fd, buffer, sizeof buffer) == sizeof buffer);
  for (unsigned i = 0; i < sizeof buffer; ++i)
    ASSERT(buffer[i] == 0x63);
  close(fd);
  ASSERT(remove("memory"));
  ASSERT(fs_free_sectors() == initial);
  msg("MEMORY FAILURE rollback and retry PASS");
}
