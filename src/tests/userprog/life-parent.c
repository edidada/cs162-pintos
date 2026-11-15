#include <syscall.h>
#include <stdio.h>
#include <debug.h>
int main(void) {
  for (int round = 0; round < 20; ++round) {
    int children[8];
    for (int i = 0; i < 8; ++i) {
      children[i] = exec("life-child normal");
      ASSERT(children[i] > 0);
    }
    for (int i = 7; i >= 0; --i) {
      ASSERT(wait(children[i]) == 73);
      ASSERT(wait(children[i]) == -1);
    }
    int parent = exec("life-child middle");
    ASSERT(parent > 0);
    int grandchild = wait(parent);
    ASSERT(grandchild > 0);
    ASSERT(wait(grandchild) == -1);
    for (int i = 0; i < 1500000; ++i)
      ASSERT(practice(161) == 162);
    ASSERT(exec("missing-life") == -1);
  }
  int last = exec("life-child normal");
  ASSERT(last > 0);
  ASSERT(wait(last) == 73);
  puts(
      "LIFECYCLE STRESS PASSED: 20 rounds, 160 reverse waits, 20 orphaned grandchildren, recovery");
  return 0;
}
