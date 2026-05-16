#include <dirent.h>
#include <string.h>
#ifndef _WIN32

#include "core/fs.h"
#include "project/manifest.h"
#include <stdio.h>
#include <sys/stat.h>

// TODO(FloofyPlasma): Error handling
void ensure_dirs(const char *path) {
  char tmp[MAX_PATH];
  snprintf(tmp, sizeof(tmp), "%s", path);
  for (char *p = tmp + 1; *p; p++) {
    if (*p == '/') {
      *p = '\0';
      mkdir(tmp, 0755);
      *p = '/';
    }
  }
  mkdir(tmp, 0755);
}

int scan_dir(BuildCtx *ctx, const char *dir) {
  DIR *d = opendir(dir);
  if (!d) {
    fprintf(stderr, "smelt: cannot open dir: %s\n", dir);
    return 0;
  }

  struct dirent *entry;
  while ((entry = readdir(d)) != NULL) {
    if (entry->d_name[0] == '.')
      continue;

    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);

    struct stat st;
    if (stat(path, &st) != 0)
      continue;

    if (S_ISDIR(st.st_mode)) {
      scan_dir(ctx, path);
    } else if (S_ISREG(st.st_mode) && ends_with_c(entry->d_name)) {
      if (!stringvec_push(&ctx->sources, path)) {
        fprintf(stderr, "smelt: out of memory\n");
        closedir(d);
        return 0;
      }
    }
  }

  closedir(d);
  return 1;
}

int ends_with_c(const char *name) {
  size_t len = strlen(name);
  return len > 2 && name[len - 2] == '.' && name[len - 1] == 'c';
}

#endif
