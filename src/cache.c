#include "cache.h"
#include "xxhash.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CACHE_FILE "build/.smelt_cache"

int cache_hash_file(const char *path, char *dst, size_t dstsz) {
  FILE *fp = fopen(path, "rb");
  if (!fp)
    return 0;

  XXH3_state_t *state = XXH3_createState();
  XXH3_64bits_reset(state);

  unsigned char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
    XXH3_64bits_update(state, buf, n);

  XXH64_hash_t hash = XXH3_64bits_digest(state);
  XXH3_freeState(state);
  fclose(fp);

  snprintf(dst, dstsz, "%016llx", (unsigned long long)hash);
  return 1;
}

void cache_hash_str(const char *str, char *dst, size_t dstsz) {
  XXH64_hash_t hash = XXH64(str, strlen(str), 0);
  snprintf(dst, dstsz, "%016llx", (unsigned long long)hash);
}

void cache_load(BuildCache *c) {
  *c = (BuildCache){0};

  FILE *fp = fopen(CACHE_FILE, "r");
  if (!fp)
    return;

  char line[MAX_PATH + MAX_HASH + 2];
  while (fgets(line, sizeof(line), fp)) {
    line[strcspn(line, "\n")] = '\0';

    char *eq = strchr(line, '=');
    if (!eq)
      continue;
    *eq = '\0';
    const char *key = line;
    const char *val = eq + 1;

    if (strcmp(key, "__flags__") == 0) {
      snprintf(c->flags_hash, sizeof(c->flags_hash), "%s", val);
    } else if (strcmp(key, "__compiler__") == 0) {
      snprintf(c->compiler_ver, sizeof(c->compiler_ver), "%s", val);
    } else {
      if (c->count < MAX_SOURCES) {
        snprintf(c->entries[c->count].source, sizeof(c->entries[0].source),
                 "%s", key);
        snprintf(c->entries[c->count].hash, sizeof(c->entries[0].hash), "%s",
                 val);
        c->count++;
      }
    }
  }
  fclose(fp);
}

void cache_save(const BuildCache *c) {
  FILE *fp = fopen(CACHE_FILE, "w");
  if (!fp) {
    perror("smelt: cannot write cache");
    return;
  }

  fprintf(fp, "__flags__=%s\n", c->flags_hash);
  fprintf(fp, "__compiler__=%s\n", c->compiler_ver);
  for (int i = 0; i < c->count; i++)
    fprintf(fp, "%s=%s\n", c->entries[i].source, c->entries[i].hash);

  fclose(fp);
}

const char *cache_get(const BuildCache *c, const char *source) {
  for (int i = 0; i < c->count; i++)
    if (strcmp(c->entries[i].source, source) == 0)
      return c->entries[i].hash;
  return NULL;
}

void cache_set(BuildCache *c, const char *source, const char *hash) {
  for (int i = 0; i < c->count; i++) {
    if (strcmp(c->entries[i].source, source) == 0) {
      snprintf(c->entries[i].hash, sizeof(c->entries[i].hash), "%s", hash);
      return;
    }
  }

  if (c->count < MAX_SOURCES) {
    snprintf(c->entries[c->count].source, sizeof(c->entries[0].source), "%s",
             source);
    snprintf(c->entries[c->count].hash, sizeof(c->entries[0].hash), "%s", hash);
    c->count++;
  }
}
