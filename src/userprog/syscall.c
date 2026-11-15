#include "userprog/syscall.h"
#include <stdio.h>
#include <limits.h>
#include "threads/malloc.h"
#include "filesys/file.h"
#include "filesys/filesys.h"
#include "devices/input.h"
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
static void check_writable(void *data, size_t size) {
  check_buffer(data, size);
  uintptr_t start = (uintptr_t)data;
  for (uintptr_t p = start; size && p < start + size;) {
    if (!pagedir_is_writable(thread_current()->pcb->pagedir, (void *)p)) terminate(-1);
    uintptr_t next = (p & ~PGMASK) + PGSIZE;
    if (next >= start + size) break;
    p = next;
  }
}
static void check_string(const char *text) {
  for (;;) {
    check_buffer(text, 1);
    if (!*text++) return;
  }
}
static struct descriptor *descriptor(int fd) {
  struct list *list = &thread_current()->pcb->descriptors;
  for (struct list_elem *e = list_begin(list); e != list_end(list); e = list_next(e)) {
    struct descriptor *d = list_entry(e, struct descriptor, elem);
    if (d->fd == fd) return d;
  }
  return NULL;
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
    case SYS_FORK: f->eax = process_fork(f); break;
    case SYS_WAIT: check_buffer(args, 8); f->eax = process_wait((pid_t)args[1]); break;
    case SYS_PRACTICE: check_buffer(args, 8); f->eax = args[1] + 1u; break;
    case SYS_CREATE:
    case SYS_REMOVE:
    case SYS_OPEN: {
      unsigned code = args[0];
      check_buffer(args, code == SYS_CREATE ? 12 : 8);
      const char *name = (const char *)args[1]; check_string(name);
      lock_acquire(&process_fs_lock);
      if (code == SYS_CREATE) f->eax = filesys_create(name, args[2]);
      else if (code == SYS_REMOVE) f->eax = filesys_remove(name);
      else {
        struct file *file = filesys_open(name);
        struct descriptor *d = file ? malloc(sizeof *d) : NULL;
        struct process *pcb = thread_current()->pcb;
        if (!d || pcb->next_fd == INT_MAX) {
          file_close(file); free(d); f->eax = -1;
        } else {
          d->file = file; d->fd = pcb->next_fd++;
          list_push_back(&pcb->descriptors, &d->elem); f->eax = d->fd;
        }
      }
      lock_release(&process_fs_lock); break;
    }
    case SYS_READ:
    case SYS_WRITE: {
      bool reading = args[0] == SYS_READ;
      check_buffer(args, 16);
      int fd = (int)args[1]; void *buffer = (void *)args[2]; unsigned size = args[3];
      if (reading) check_writable(buffer, size); else check_buffer(buffer, size);
      if (fd == 0 && reading) {
        for (unsigned i = 0; i < size; ++i) ((uint8_t *)buffer)[i] = input_getc();
        f->eax = size;
      } else if (fd == 1 && !reading) { putbuf(buffer, size); f->eax = size; }
      else {
        lock_acquire(&process_fs_lock);
        struct descriptor *d = descriptor(fd);
        if (!d) f->eax = -1;
        else f->eax = reading ? file_read(d->file, buffer, size) : file_write(d->file, buffer, size);
        lock_release(&process_fs_lock);
      }
      break;
    }
    case SYS_FILESIZE:
    case SYS_TELL:
    case SYS_SEEK:
    case SYS_CLOSE: {
      unsigned code = args[0]; check_buffer(args, code == SYS_SEEK ? 12 : 8);
      lock_acquire(&process_fs_lock);
      struct descriptor *d = descriptor((int)args[1]); f->eax = -1;
      if (d) {
        if (code == SYS_FILESIZE) f->eax = file_length(d->file);
        else if (code == SYS_TELL) f->eax = file_tell(d->file);
        else if (code == SYS_SEEK) file_seek(d->file, args[2]);
        else { list_remove(&d->elem); file_close(d->file); free(d); }
      }
      lock_release(&process_fs_lock); break;
    }
    default: terminate(-1);
  }
}
