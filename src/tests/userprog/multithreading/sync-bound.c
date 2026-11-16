/* Registration must distinguish addresses with identical initial bytes,
   reject invalid storage, and preserve existing objects at capacity. */
#include "tests/lib.h"
#include "tests/main.h"
#include <syscall.h>

static lock_t locks[256];
static sema_t semaphores[257];

void test_main(void) {
  ASSERT(get_tid() > 0 && get_tid() == get_tid());
  ASSERT(!lock_init((lock_t*)0xc0000000));
  ASSERT(!lock_init((lock_t*)test_main));
  ASSERT(!sema_init((sema_t*)test_main, 0));
  ASSERT(!sema_init(&semaphores[0], -1));
  /* tests/main.c already initialized the console lock. */
  for (int i = 0; i < 255; ++i)
    lock_check_init(&locks[i]);
  ASSERT(!lock_init(&locks[255]));
  for (int i = 0; i < 256; ++i)
    sema_check_init(&semaphores[i], 0);
  ASSERT(!sema_init(&semaphores[256], 0));
  for (int i = 0; i < 255; ++i)
    lock_acquire(&locks[i]);
  for (int i = 254; i >= 0; --i)
    lock_release(&locks[i]);
  for (int i = 0; i < 256; ++i) {
    sema_up(&semaphores[i]);
    sema_down(&semaphores[i]);
  }
  /* Exit while owning a user lock: teardown must remove the TCB link before
     freeing its kernel registration, including under priority scheduling. */
  lock_acquire(&locks[0]);
  msg("PASS");
}
