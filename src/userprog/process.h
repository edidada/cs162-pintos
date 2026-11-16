#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include <stdint.h>
#include "threads/synch.h"
#include <list.h>

// At most 8MB can be allocated to the stack
// These defines will be used in Project 2: Multithreading
#define MAX_STACK_PAGES (1 << 11)
#define MAX_THREADS 127

/* PIDs and TIDs are the same type. PID should be
   the TID of the main thread of the process */
typedef tid_t pid_t;

/* Thread functions (Project 2: Multithreading) */
typedef void (*pthread_fun)(void*);
typedef void (*stub_fun)(pthread_fun, void*);

/* The process control block for a given process. Since
   there can be multiple threads per process, we need a separate
   PCB from the TCB. All TCBs in a process will have a pointer
   to the PCB, and the PCB will have a pointer to the main thread
   of the process, which is `special`. */
struct descriptor {
  int fd;
  struct file* file;
  struct list_elem elem;
};
struct child_status;
extern struct lock process_fs_lock;

struct user_thread {
  struct list_elem elem;
  tid_t tid;
  struct thread* thread;
  struct semaphore done;
  bool joined, finished, detached;
  unsigned slot;
  void* stack;
};

struct process {
  struct lock state_lock;
  struct list threads;
  struct user_thread main_state;
  struct semaphore changed;
  unsigned live_threads;
  bool stack_slots[MAX_THREADS];
  bool exiting;
  struct thread* exit_owner;
  int exit_rank;
  pid_t pid;
  struct child_status* parent_status;
  struct list user_sync;
  struct lock user_sync_lock;
  unsigned user_lock_count, user_sema_count;
  struct list descriptors;
  int next_fd;
  struct file* executable;
  struct list children;
  int exit_status;
  /* Owned by process.c. */
  uint32_t* pagedir;          /* Page directory. */
  char process_name[16];      /* Name of the main thread */
  struct thread* main_thread; /* Pointer to main thread */
};

void userprog_init(void);

struct intr_frame;
pid_t process_fork(struct intr_frame*);
pid_t process_execute(const char* file_name);
int process_wait(pid_t);
void process_exit(void);
void process_terminate(int, bool) NO_RETURN;
void process_check_exit(void);
void process_activate(void);
bool process_user_sync(unsigned, void*, int);

bool is_main_thread(struct thread*, struct process*);
pid_t get_pid(struct process*);

tid_t pthread_execute(stub_fun, pthread_fun, void*);
tid_t pthread_join(tid_t);
void pthread_exit(void);
void pthread_exit_main(void);

#endif /* userprog/process.h */
