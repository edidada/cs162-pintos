#ifndef FILESYS_CACHE_H
#define FILESYS_CACHE_H

#include <stdbool.h>
#include <stddef.h>
#include <cache-stats.h>
#include "devices/block.h"

struct cache_entry;
void cache_init(void);
struct cache_entry* cache_get(block_sector_t, bool overwrite);
void* cache_data(struct cache_entry*);
void cache_put(struct cache_entry*, bool dirty);
void cache_read(block_sector_t, void*, size_t offset, size_t size);
void cache_write(block_sector_t, const void*, size_t offset, size_t size);
void cache_zero(block_sector_t);
void cache_flush(void);
void cache_reset(void);
void cache_stats(struct cache_stats*);

#endif
