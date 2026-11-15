#include <syscall.h>
#include <string.h>
#include <debug.h>
int main(int argc, char** argv) {
  ASSERT(argc == 2);
  if (!strcmp(argv[1], "middle")) {
    int child = exec("life-child leaf");
    ASSERT(child > 0);
    return child;
  }
  if (!strcmp(argv[1], "leaf")) {
    for (int i = 0; i < 500000; ++i)
      ASSERT(practice(5) == 6);
    return 9999;
  }
  return 73;
}
