#include "filesys/cache.h"
#include <debug.h>
#include <string.h>
#include "filesys/filesys.h"
#include "threads/synch.h"

#define CACHE_SIZE 64
struct cache_entry {
  uint8_t data[BLOCK_SECTOR_SIZE];
  struct lock data_lock;
  block_sector_t sector, pending;
  unsigned users;
  bool valid, busy, accessed, dirty;
};
static struct cache_entry entries[CACHE_SIZE];
static struct lock directory_lock;
static struct condition changed;
static unsigned hand;
static bool maintenance;
static struct cache_stats stats;
static uint64_t read_base, write_base;

void cache_init(void) {
  lock_init(&directory_lock);
  cond_init(&changed);
  block_get_stats(fs_device, &read_base, &write_base);
  for (unsigned i = 0; i < CACHE_SIZE; ++i)
    lock_init(&entries[i].data_lock);
}

/* The directory lock protects identities, reservations and pins, never IO.
   A busy victim reserves BOTH its old sector and its pending replacement. */
struct cache_entry* cache_get(block_sector_t sector, bool overwrite) {
  ASSERT(sector < block_size(fs_device));
  lock_acquire(&directory_lock);
  for (;;) {
    if (maintenance) {
      cond_wait(&changed, &directory_lock);
      continue;
    }
    struct cache_entry* found = NULL;
    for (unsigned i = 0; i < CACHE_SIZE; ++i) {
      struct cache_entry* e = &entries[i];
      if ((e->valid && e->sector == sector) || (e->busy && e->pending == sector)) {
        found = e;
        break;
      }
    }
    if (found != NULL) {
      if (found->busy) {
        cond_wait(&changed, &directory_lock);
        continue;
      }
      ++found->users;
      found->accessed = true;
      ++stats.hits;
      lock_release(&directory_lock);
      lock_acquire(&found->data_lock);
      return found;
    }
    struct cache_entry* victim = NULL;
    bool second_chance = false;
    for (unsigned i = 0; i < CACHE_SIZE; ++i) {
      struct cache_entry* e = &entries[hand];
      hand = (hand + 1) % CACHE_SIZE;
      if (e->users || e->busy)
        continue;
      if (e->valid && e->accessed) {
        e->accessed = false;
        second_chance = true;
        continue;
      }
      victim = e;
      break;
    }
    if (victim == NULL) {
      if (!second_chance)
        cond_wait(&changed, &directory_lock);
      continue;
    }
    victim->busy = true;
    victim->pending = sector;
    victim->users = 1;
    ++stats.misses;
    lock_release(&directory_lock);
    lock_acquire(&victim->data_lock);
    bool wrote = victim->valid && victim->dirty;
    if (wrote)
      block_write(fs_device, victim->sector, victim->data);
    if (!overwrite)
      block_read(fs_device, sector, victim->data);
    else
      memset(victim->data, 0, BLOCK_SECTOR_SIZE);
    lock_acquire(&directory_lock);
    victim->sector = sector;
    victim->valid = true;
    victim->busy = false;
    victim->dirty = false;
    victim->accessed = true;
    cond_broadcast(&changed, &directory_lock);
    lock_release(&directory_lock);
    return victim;
  }
}

void* cache_data(struct cache_entry* e) {
  ASSERT(lock_held_by_current_thread(&e->data_lock));
  return e->data;
}

void cache_put(struct cache_entry* e, bool dirty) {
  ASSERT(lock_held_by_current_thread(&e->data_lock));
  e->dirty |= dirty;
  lock_release(&e->data_lock);
  lock_acquire(&directory_lock);
  ASSERT(e->users > 0);
  --e->users;
  cond_broadcast(&changed, &directory_lock);
  lock_release(&directory_lock);
}

void cache_read(block_sector_t sector, void* dst, size_t offset, size_t size) {
  ASSERT(offset <= BLOCK_SECTOR_SIZE && size <= BLOCK_SECTOR_SIZE - offset);
  struct cache_entry* e = cache_get(sector, false);
  memcpy(dst, (uint8_t*)cache_data(e) + offset, size);
  cache_put(e, false);
}

void cache_write(block_sector_t sector, const void* src, size_t offset, size_t size) {
  ASSERT(offset <= BLOCK_SECTOR_SIZE && size <= BLOCK_SECTOR_SIZE - offset);
  struct cache_entry* e = cache_get(sector, offset == 0 && size == BLOCK_SECTOR_SIZE);
  memcpy((uint8_t*)cache_data(e) + offset, src, size);
  cache_put(e, true);
}

void cache_zero(block_sector_t sector) {
  struct cache_entry* e = cache_get(sector, true);
  memset(cache_data(e), 0, BLOCK_SECTOR_SIZE);
  cache_put(e, true);
}

/* Maintenance gates new users while draining old pins. No global lock is
   held during disk writes. Reset flushes dirty data before invalidation. */
static void maintain(bool invalidate) {
  lock_acquire(&directory_lock);
  while (maintenance)
    cond_wait(&changed, &directory_lock);
  maintenance = true;
  for (;;) {
    bool active = false;
    for (unsigned i = 0; i < CACHE_SIZE; ++i)
      active |= entries[i].users != 0 || entries[i].busy;
    if (!active)
      break;
    cond_wait(&changed, &directory_lock);
  }
  lock_release(&directory_lock);
  for (unsigned i = 0; i < CACHE_SIZE; ++i) {
    struct cache_entry* e = &entries[i];
    if (e->valid && e->dirty) {
      block_write(fs_device, e->sector, e->data);
      e->dirty = false;
    }
    if (invalidate)
      e->valid = false;
  }
  lock_acquire(&directory_lock);
  if (invalidate) {
    memset(&stats, 0, sizeof stats);
    block_get_stats(fs_device, &read_base, &write_base);
  }
  maintenance = false;
  cond_broadcast(&changed, &directory_lock);
  lock_release(&directory_lock);
}
void cache_flush(void) { maintain(false); }
void cache_reset(void) { maintain(true); }
void cache_stats(struct cache_stats* result) {
  lock_acquire(&directory_lock);
  *result = stats;
  block_get_stats(fs_device, &result->reads, &result->writes);
  result->reads -= read_base;
  result->writes -= write_base;
  lock_release(&directory_lock);
}
