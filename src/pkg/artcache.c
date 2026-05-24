#include "pkg/artcache.h"
#include <stdio.h>
#include <sys/stat.h>

void artifact_dir(char *dst, size_t dstsz, const char *out_dir,
                  const char *profile, const char *name, const char *commit,
                  const char *compiler_hash) {
  snprintf(dst, dstsz, "%s/%s/deps/%s/%.16s-%s", out_dir, profile, name, commit,
           compiler_hash);
}

void artifact_lib_path(char *dst, size_t dstsz, const char *art_dir) {
  snprintf(dst, dstsz, "%s/libdep.a", art_dir);
}

int artifact_valid(const char *art_dir) {
  char lib[4096];
  artifact_lib_path(lib, sizeof(lib), art_dir);
  struct stat st;
  return stat(lib, &st) == 0 && S_ISREG(st.st_mode);
}
