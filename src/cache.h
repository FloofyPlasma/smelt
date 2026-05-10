#ifndef SMELT_CACHE_H
#define SMELT_CACHE_H

#include "build.h"
#include <stddef.h>

#define MAX_HASH 65

typedef struct {
  char source[MAX_PATH];
  char hash[MAX_HASH];
} CacheEntry;

typedef struct {
  CacheEntry entries[MAX_SOURCES];
  int count;
  char flags_hash[MAX_HASH];
  char compiler_ver[256];
} BuildCache;

int cache_hash_file(const char *path, char *dst, size_t dstsz);
void cache_hash_str(const char *str, char *dst, size_t dstsz);
void cache_load(BuildCache *c);
void cache_save(const BuildCache *c);
const char *cache_get(const BuildCache *c, const char *source);
void cache_set(BuildCache *c, const char *source, const char *hash);

#endif
