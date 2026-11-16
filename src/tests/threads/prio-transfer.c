/* Remaining lock waiters must donate to the new holder, even after the
   highest-priority acquirer lowers its base priority. */
#include "tests/threads/tests.h"
#include "threads/synch.h"
#include "threads/thread.h"

static struct lock shared;
static struct semaphore finished;
static bool medium_finished;

static void medium(void* aux UNUSED) {
  lock_acquire(&shared);
  medium_finished = true;
  lock_release(&shared);
  sema_up(&finished);
}

static void high(void* aux UNUSED) {
  lock_acquire(&shared);
  thread_set_priority(20);
  ASSERT(thread_get_priority() == 30);
  ASSERT(!medium_finished);
  lock_release(&shared);
  ASSERT(thread_get_priority() == 20);
  ASSERT(medium_finished);
  sema_up(&finished);
}

void test_prio_transfer(void) {
  ASSERT(active_sched_policy == SCHED_PRIO);
  thread_set_priority(10);
  lock_init(&shared);
  sema_init(&finished, 0);
  medium_finished = false;
  lock_acquire(&shared);
  ASSERT(thread_create("medium", 30, medium, NULL) != TID_ERROR);
  ASSERT(thread_get_priority() == 30);
  ASSERT(thread_create("high", 40, high, NULL) != TID_ERROR);
  ASSERT(thread_get_priority() == 40);
  lock_release(&shared);
  ASSERT(thread_get_priority() == 10);
  sema_down(&finished);
  sema_down(&finished);
  pass();
}
