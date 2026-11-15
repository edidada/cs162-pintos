#include <syscall.h>
#include <stdio.h>
#include <debug.h>
int main(void) {
  ASSERT(!create("too-large",0xffffffffu));
  ASSERT(create("readonly",1));
  int fd=open("readonly");ASSERT(fd>=2);
  puts("ro-target: before read into code");
  read(fd,(void *)main,1);
  puts("ro-target: invalid read returned");return 1;
}
