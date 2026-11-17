/* Diagnostic helper: practice is patched into an IO gate arming call only
   in the isolated CI build. Ordinary kernel behavior is unchanged. */
#include "tests/lib.h"
#include <string.h>
#include <syscall.h>
int main(int argc, char** argv) {
  test_name = "io-gate";
  ASSERT(argc == 2);
  bool different = !strcmp(argv[1], "different");
  ASSERT(different || !strcmp(argv[1], "same"));
  ASSERT(create("io-a", 1024));
  if (different)
    ASSERT(create("io-b", 1024));
  int a = open("io-a"), b = open(different ? "io-b" : "io-a");
  ASSERT(a >= 2 && b >= 2);
  cache_reset();
  practice((b << 16) | a);
  int first = fork();
  ASSERT(first >= 0);
  if (!first) {
    close(a);
    close(b);
    int fd = open("io-a");
    unsigned char byte = 1;
    msg("worker A ready to read");
    ASSERT(fd >= 2 && read(fd, &byte, 1) == 1 && byte == 0);
    close(fd);
    return 0;
  }
  int second = fork();
  ASSERT(second >= 0);
  if (!second) {
    close(a);
    close(b);
    int fd = open(different ? "io-b" : "io-a");
    unsigned char byte = 1;
    ASSERT(fd >= 2);
    seek(fd, 512);
    msg("worker B ready to read");
    ASSERT(read(fd, &byte, 1) == 1 && byte == 0);
    close(fd);
    return 0;
  }
  close(a);
  close(b);
  ASSERT(wait(first) == 0 && wait(second) == 0);
  ASSERT(remove("io-a"));
  if (different)
    ASSERT(remove("io-b"));
  msg("IO gate %s PASS", argv[1]);
  return 0;
}
