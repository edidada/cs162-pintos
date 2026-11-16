#ifndef LIB_CACHE_STATS_H
#define LIB_CACHE_STATS_H
#include <stdint.h>
struct cache_stats {
  uint64_t hits, misses, reads, writes;
};
#endif
