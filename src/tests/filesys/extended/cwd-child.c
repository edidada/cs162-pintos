#include "tests/lib.h"
#include <string.h>
#include <syscall.h>
int main(void) {
  test_name = "cwd-child";
  char data[4];
  int fd = open("marker");
  ASSERT(fd >= 2 && read(fd, data, 4) == 4 && !memcmp(data, "data", 4));
  close(fd);
  ASSERT(create("child-a", 0));
  ASSERT(chdir("../b") && create("child-b", 0) && open("marker") == -1);
  return 32;
}
