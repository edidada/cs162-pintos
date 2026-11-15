#include <syscall.h>
#include <stdio.h>
#include <string.h>
#include <debug.h>
int main(void) {
  ASSERT(create("shared",8));int fd=open("shared");ASSERT(fd>=2);
  ASSERT(write(fd,"ABCDEFGH",8)==8);seek(fd,0);
  volatile int private_value=11;
  int pid=fork();ASSERT(pid>=0);
  if(!pid) {
    char bytes[3];ASSERT(read(fd,bytes,3)==3);ASSERT(!memcmp(bytes,"ABC",3));
    private_value=99;close(fd);return 7;
  }
  ASSERT(wait(pid)==7);ASSERT(private_value==11);ASSERT(tell(fd)==3);
  char bytes[2];ASSERT(read(fd,bytes,2)==2);ASSERT(!memcmp(bytes,"DE",2));close(fd);
  puts("fork-keep: private memory and shared offset survive child close");return 0;
}
