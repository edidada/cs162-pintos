#include "tests/lib.h"
#include "tests/main.h"
#include <string.h>
#include <syscall.h>

static void marker(void) {
  char data[4];
  int fd = open("marker");
  ASSERT(fd >= 2 && read(fd, data, sizeof data) == sizeof data && !memcmp(data, "data", 4));
  close(fd);
}
void test_main(void) {
  ASSERT(mkdir("a") && mkdir("b") && chdir("/a"));
  ASSERT(create("marker", 0));
  int fd = open("marker");
  ASSERT(fd >= 2 && write(fd, "data", 4) == 4);
  close(fd);
  int child = fork();
  ASSERT(child >= 0);
  if (child == 0) {
    marker();
    ASSERT(chdir("../b") && create("forked", 0) && open("marker") == -1);
    exit(31);
  }
  ASSERT(wait(child) == 31);
  marker();
  child = exec("/cwd-child");
  ASSERT(child > 0 && wait(child) == 32);
  marker();
  fd = open("child-a");
  ASSERT(fd >= 2);
  close(fd);
  ASSERT(chdir("/"));
  fd = open("//a/./../a/marker");
  ASSERT(fd >= 2);
  close(fd);
  ASSERT(open("a/marker/") == -1 && open("a/marker/..") == -1);
  ASSERT(!create("a/marker/", 0) && !mkdir("a/marker/child"));
  msg("CWD fork/exec inheritance and independent changes PASS");
}
