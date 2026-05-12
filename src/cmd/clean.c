#include "cmd/clean.h"
#include "project/manifest.h"
#include <dirent.h>
#include <stdio.h>
#include <string.h>

// FIXME: make this platform independent
int clean_run(const Manifest *m) {
  DIR *d = opendir(m->out_dir);
  if (!d) {
    printf("smelt: nothing to clean\n");
    return 1;
  }

  struct dirent *entry;
  char path[MAX_PATH * 2];
  while ((entry = readdir(d)) != NULL) {
    if (entry->d_name[0] == '.')
      continue; // skip . and ..
    snprintf(path, sizeof(path), "%s/%s", m->out_dir, entry->d_name);
    if (remove(path) == 0)
      printf("smelt: removed %s\n", path);
    else
      perror(path);
  }

  closedir(d);
  printf("smelt: clean done\n");
  return 1;
}
