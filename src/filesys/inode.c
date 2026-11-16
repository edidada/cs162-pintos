#include "filesys/inode.h"
#include <list.h>
#include <debug.h>
#include <round.h>
#include <stddef.h>
#include <string.h>
#include "filesys/free-map.h"
#include "filesys/cache.h"
#include "threads/malloc.h"
#include "threads/synch.h"

#define INODE_MAGIC 0x494e4f44
#define DIRECT_COUNT 123
#define INDIRECT_COUNT (BLOCK_SECTOR_SIZE / sizeof(block_sector_t))
#define MAX_FILE_SIZE (8 * 1024 * 1024)

/* Sector zero is reserved for the free-map inode, never a data pointer. */
struct inode_disk {
  off_t length;
  unsigned magic;
  uint32_t flags; /* Reserved for directory type. */
  block_sector_t direct[DIRECT_COUNT];
  block_sector_t indirect, doubly;
};
struct inode {
  struct list_elem elem;
  block_sector_t sector;
  int open_cnt, deny_write_cnt;
  bool removed, loading, directory;
  off_t length;
  struct rw_lock growth_lock;
};
struct allocation {
  struct list_elem elem;
  block_sector_t sector, parent;
  size_t offset;
};
static struct list open_inodes;
static struct lock open_lock;
static struct condition opened;

static block_sector_t pointer_at(block_sector_t parent, size_t offset) {
  block_sector_t child;
  cache_read(parent, &child, offset, sizeof child);
  return child;
}

/* Allocate and zero before publishing. The reverse-order log can undo every
   pointer change without allocating memory during rollback. */
static block_sector_t get_pointer(block_sector_t parent, size_t offset, struct list* transaction) {
  block_sector_t child = pointer_at(parent, offset);
  if (child != 0 || transaction == NULL)
    return child;
  struct allocation* record = malloc(sizeof *record);
  if (record == NULL)
    return 0;
  if (!free_map_allocate(1, &child)) {
    free(record);
    return 0;
  }
  cache_zero(child);
  record->sector = child;
  record->parent = parent;
  record->offset = offset;
  list_push_front(transaction, &record->elem);
  cache_write(parent, &child, offset, sizeof child);
  return child;
}

static block_sector_t data_sector(block_sector_t root, size_t index, struct list* transaction) {
  if (index < DIRECT_COUNT)
    return get_pointer(root, offsetof(struct inode_disk, direct) + index * sizeof(block_sector_t),
                       transaction);
  index -= DIRECT_COUNT;
  if (index < INDIRECT_COUNT) {
    block_sector_t indirect = get_pointer(root, offsetof(struct inode_disk, indirect), transaction);
    return indirect ? get_pointer(indirect, index * sizeof(block_sector_t), transaction) : 0;
  }
  index -= INDIRECT_COUNT;
  if (index >= INDIRECT_COUNT * INDIRECT_COUNT)
    return 0;
  block_sector_t doubly = get_pointer(root, offsetof(struct inode_disk, doubly), transaction);
  if (!doubly)
    return 0;
  block_sector_t indirect =
      get_pointer(doubly, index / INDIRECT_COUNT * sizeof(block_sector_t), transaction);
  return indirect
             ? get_pointer(indirect, index % INDIRECT_COUNT * sizeof(block_sector_t), transaction)
             : 0;
}

static void finish_allocation(struct list* transaction, bool commit) {
  while (!list_empty(transaction)) {
    struct allocation* record = list_entry(list_pop_front(transaction), struct allocation, elem);
    if (!commit) {
      block_sector_t zero = 0;
      cache_write(record->parent, &zero, record->offset, sizeof zero);
      free_map_release(record->sector, 1);
    }
    free(record);
  }
}

static bool extend_blocks(block_sector_t root, off_t before, off_t after,
                          struct list* transaction) {
  for (size_t i = DIV_ROUND_UP((uint32_t)before, BLOCK_SECTOR_SIZE);
       i < DIV_ROUND_UP((uint32_t)after, BLOCK_SECTOR_SIZE); ++i)
    if (!data_sector(root, i, transaction))
      return false;
  return true;
}

void inode_init(void) {
  list_init(&open_inodes);
  lock_init(&open_lock);
  cond_init(&opened);
}

bool inode_create(block_sector_t sector, off_t length) {
  ASSERT(sizeof(struct inode_disk) == BLOCK_SECTOR_SIZE);
  if (length < 0 || length > MAX_FILE_SIZE)
    return false;
  cache_zero(sector);
  unsigned magic = INODE_MAGIC;
  cache_write(sector, &magic, offsetof(struct inode_disk, magic), sizeof magic);
  struct list transaction;
  list_init(&transaction);
  bool success = extend_blocks(sector, 0, length, &transaction);
  finish_allocation(&transaction, success);
  if (success)
    cache_write(sector, &length, offsetof(struct inode_disk, length), sizeof length);
  return success;
}

/* Unpublished/removed inodes can be reclaimed without a fallible inode_open. */
static void release_tree(block_sector_t sector, unsigned depth) {
  if (!sector)
    return;
  if (depth)
    for (size_t i = 0; i < INDIRECT_COUNT; ++i)
      release_tree(pointer_at(sector, i * sizeof(block_sector_t)), depth - 1);
  free_map_release(sector, 1);
}
void inode_discard_data(block_sector_t sector) {
  for (size_t i = 0; i < DIRECT_COUNT; ++i)
    release_tree(
        pointer_at(sector, offsetof(struct inode_disk, direct) + i * sizeof(block_sector_t)), 0);
  release_tree(pointer_at(sector, offsetof(struct inode_disk, indirect)), 1);
  release_tree(pointer_at(sector, offsetof(struct inode_disk, doubly)), 2);
  cache_zero(sector);
}
void inode_discard(block_sector_t sector) {
  inode_discard_data(sector);
  free_map_release(sector, 1);
}
void inode_set_directory(block_sector_t sector) {
  uint32_t flags = 1;
  cache_write(sector, &flags, offsetof(struct inode_disk, flags), sizeof flags);
}
bool inode_is_dir(const struct inode* inode) { return inode->directory; }
bool inode_is_removed(const struct inode* inode) {
  lock_acquire(&open_lock);
  bool removed = inode->removed;
  lock_release(&open_lock);
  return removed;
}
int inode_open_count(const struct inode* inode) {
  lock_acquire(&open_lock);
  int count = inode->open_cnt;
  lock_release(&open_lock);
  return count;
}

struct inode* inode_open(block_sector_t sector) {
  lock_acquire(&open_lock);
  for (;;) {
    struct inode* found = NULL;
    for (struct list_elem* e = list_begin(&open_inodes); e != list_end(&open_inodes);
         e = list_next(e)) {
      struct inode* candidate = list_entry(e, struct inode, elem);
      if (candidate->sector == sector) {
        found = candidate;
        break;
      }
    }
    if (found == NULL)
      break;
    if (found->loading) {
      cond_wait(&opened, &open_lock);
      continue;
    }
    ++found->open_cnt;
    lock_release(&open_lock);
    return found;
  }
  struct inode* inode = calloc(1, sizeof *inode);
  if (inode == NULL) {
    lock_release(&open_lock);
    return NULL;
  }
  inode->sector = sector;
  inode->open_cnt = 1;
  inode->loading = true;
  rw_lock_init(&inode->growth_lock);
  list_push_front(&open_inodes, &inode->elem);
  lock_release(&open_lock);
  struct cache_entry* entry = cache_get(sector, false);
  const struct inode_disk* disk = cache_data(entry);
  bool valid = disk->magic == INODE_MAGIC && disk->length >= 0 && disk->length <= MAX_FILE_SIZE;
  inode->length = disk->length;
  inode->directory = (disk->flags & 1) != 0;
  cache_put(entry, false);
  lock_acquire(&open_lock);
  inode->loading = false;
  if (!valid)
    list_remove(&inode->elem);
  cond_broadcast(&opened, &open_lock);
  lock_release(&open_lock);
  if (!valid) {
    free(inode);
    return NULL;
  }
  return inode;
}
struct inode* inode_reopen(struct inode* inode) {
  if (inode) {
    lock_acquire(&open_lock);
    ++inode->open_cnt;
    lock_release(&open_lock);
  }
  return inode;
}
block_sector_t inode_get_inumber(const struct inode* inode) { return inode->sector; }
void inode_close(struct inode* inode) {
  if (!inode)
    return;
  lock_acquire(&open_lock);
  bool last = --inode->open_cnt == 0;
  if (last)
    list_remove(&inode->elem);
  lock_release(&open_lock);
  if (last) {
    if (inode->removed)
      inode_discard(inode->sector);
    free(inode);
  }
}
void inode_remove(struct inode* inode) {
  lock_acquire(&open_lock);
  inode->removed = true;
  lock_release(&open_lock);
}

off_t inode_read_at(struct inode* inode, void* buffer_, off_t size, off_t offset) {
  if (offset < 0 || size <= 0)
    return 0;
  uint8_t* buffer = buffer_;
  off_t total = 0;
  rw_lock_acquire(&inode->growth_lock, RW_READER);
  while (size > 0 && offset < inode->length) {
    size_t inside = offset % BLOCK_SECTOR_SIZE;
    off_t amount = BLOCK_SECTOR_SIZE - inside;
    if (amount > size)
      amount = size;
    if (amount > inode->length - offset)
      amount = inode->length - offset;
    block_sector_t sector = data_sector(inode->sector, offset / BLOCK_SECTOR_SIZE, NULL);
    ASSERT(sector != 0);
    cache_read(sector, buffer + total, inside, amount);
    size -= amount;
    offset += amount;
    total += amount;
  }
  rw_lock_release(&inode->growth_lock, RW_READER);
  return total;
}

off_t inode_write_at(struct inode* inode, const void* buffer_, off_t size, off_t offset) {
  if (offset < 0 || size <= 0 || offset > MAX_FILE_SIZE || size > MAX_FILE_SIZE - offset)
    return 0;
  rw_lock_acquire(&inode->growth_lock, RW_READER);
  bool reader = offset + size <= inode->length;
  if (!reader) {
    rw_lock_release(&inode->growth_lock, RW_READER);
    rw_lock_acquire(&inode->growth_lock, RW_WRITER);
  }
  if (inode->deny_write_cnt) {
    rw_lock_release(&inode->growth_lock, reader);
    return 0;
  }
  off_t old_length = inode->length;
  off_t end = offset + size;
  struct list transaction;
  list_init(&transaction);
  if (end > old_length && !extend_blocks(inode->sector, old_length, end, &transaction)) {
    finish_allocation(&transaction, false);
    rw_lock_release(&inode->growth_lock, reader);
    return 0;
  }
  if (offset > old_length && old_length % BLOCK_SECTOR_SIZE) {
    block_sector_t last = data_sector(inode->sector, old_length / BLOCK_SECTOR_SIZE, NULL);
    struct cache_entry* entry = cache_get(last, false);
    size_t gap = BLOCK_SECTOR_SIZE - old_length % BLOCK_SECTOR_SIZE;
    if (gap > (size_t)(offset - old_length))
      gap = offset - old_length;
    memset((uint8_t*)cache_data(entry) + old_length % BLOCK_SECTOR_SIZE, 0, gap);
    cache_put(entry, true);
  }
  off_t total = 0;
  while (size > 0) {
    size_t inside = offset % BLOCK_SECTOR_SIZE;
    off_t amount = BLOCK_SECTOR_SIZE - inside;
    if (amount > size)
      amount = size;
    block_sector_t sector = data_sector(inode->sector, offset / BLOCK_SECTOR_SIZE, NULL);
    ASSERT(sector != 0);
    cache_write(sector, (const uint8_t*)buffer_ + total, inside, amount);
    size -= amount;
    offset += amount;
    total += amount;
  }
  if (end > old_length) {
    cache_write(inode->sector, &end, offsetof(struct inode_disk, length), sizeof end);
    inode->length = end;
  }
  finish_allocation(&transaction, true);
  rw_lock_release(&inode->growth_lock, reader);
  return total;
}
void inode_deny_write(struct inode* inode) {
  rw_lock_acquire(&inode->growth_lock, RW_WRITER);
  lock_acquire(&open_lock);
  ++inode->deny_write_cnt;
  ASSERT(inode->deny_write_cnt <= inode->open_cnt);
  lock_release(&open_lock);
  rw_lock_release(&inode->growth_lock, RW_WRITER);
}
void inode_allow_write(struct inode* inode) {
  rw_lock_acquire(&inode->growth_lock, RW_WRITER);
  ASSERT(inode->deny_write_cnt > 0);
  --inode->deny_write_cnt;
  rw_lock_release(&inode->growth_lock, RW_WRITER);
}
off_t inode_length(const struct inode* inode) {
  struct rw_lock* lock = (struct rw_lock*)&inode->growth_lock;
  rw_lock_acquire(lock, RW_READER);
  off_t length = inode->length;
  rw_lock_release(lock, RW_READER);
  return length;
}
