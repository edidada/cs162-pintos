#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "userprog/process.h"
#include "userprog/pagedir.h"
#include "threads/vaddr.h"
#include "devices/shutdown.h"

static void syscall_handler(struct intr_frame*);

void syscall_init(void) { intr_register_int(0x30, 3, INTR_ON, syscall_handler, "syscall"); }

static void terminate(int status) {
  thread_current()->pcb->exit_status = status;
  process_exit();
}
static void check_buffer(const void *data, size_t size) {
  uintptr_t start = (uintptr_t)data;
  if (!size) return;
  if (!start || start >= (uintptr_t)PHYS_BASE || size > (uintptr_t)PHYS_BASE - start)
    terminate(-1);
  for (uintptr_t p = start; p < start + size;) {
    if (!pagedir_get_page(thread_current()->pcb->pagedir, (void *)p)) terminate(-1);
    uintptr_t next = (p & ~PGMASK) + PGSIZE;
    if (next >= start + size) break;
    p = next;
  }
}
static void syscall_handler(struct intr_frame* f) {
  uint32_t *args = f->esp;
  check_buffer(args, 4);
  switch (args[0]) {
    case SYS_HALT: shutdown_power_off(); break;
    case SYS_EXIT: check_buffer(args, 8); terminate((int)args[1]); break;
    case SYS_EXEC: {
      check_buffer(args, 8);
      const char *command = (const char *)args[1];
      size_t length;
      for (length = 0; length < PGSIZE; ++length) {
        check_buffer(command + length, 1);
        if (!command[length]) break;
      }
      f->eax = length == PGSIZE ? -1 : process_execute(command);
      break;
    }
    case SYS_WAIT: check_buffer(args, 8); f->eax = process_wait((pid_t)args[1]); break;
    case SYS_PRACTICE: check_buffer(args, 8); f->eax = args[1] + 1u; break;
    case SYS_WRITE:
      check_buffer(args, 16);
      check_buffer((void *)args[2], args[3]);
      if (args[1] != 1) { f->eax = -1; break; }
      putbuf((void *)args[2], args[3]); f->eax = args[3]; break;
    default: terminate(-1);
  }
}
