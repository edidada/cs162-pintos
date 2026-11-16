/* The original yield loop passes timing tests but leaves sleepers READY.
   Check the blocked state while another thread owns the CPU. */
#include "tests/threads/tests.h"
#include "devices/timer.h"
#include "threads/interrupt.h"
#include "threads/synch.h"
#include "threads/thread.h"

static struct semaphore started, finished;
static struct thread* sleeping_thread;

static void sleeper(void* aux UNUSED) {
  sleeping_thread = thread_current();
  int64_t start = timer_ticks();
  sema_up(&started);
  timer_sleep(100);
  ASSERT(timer_elapsed(start) >= 100);
  sema_up(&finished);
}

void test_alarm_block(void) {
  ASSERT(active_sched_policy == SCHED_FIFO);
  sema_init(&started, 0);
  sema_init(&finished, 0);
  ASSERT(thread_create("sleeper", PRI_DEFAULT, sleeper, NULL) != TID_ERROR);
  sema_down(&started);
  enum intr_level old = intr_disable();
  ASSERT(sleeping_thread->status == THREAD_BLOCKED);
  intr_set_level(old);
  sema_down(&finished);
  pass();
}
