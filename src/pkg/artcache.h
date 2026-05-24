#ifndef SMELT_ARTCACHE_H
#define SMELT_ARTCACHE_H

#include <stddef.h>

void artifact_dir(char *dst, size_t dstsz, const char *out_dir,
                  const char *profile, const char *name, const char *commit,
                  const char *compiler_hash);

int artifact_valid(const char *art_dir);

void artifact_lib_path(char *dst, size_t dstsz, const char *art_dir);

#endif
