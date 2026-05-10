#ifndef SMELT_MANIFEST_H
#define SMELT_MANIFEST_H

#define MAX_NAME 128
#define MAX_STR 256
#define MAX_PATH 1024
#define MAX_DEPS 64
#define MAX_EXTRA 16

typedef struct {
  char name[MAX_NAME];
  char version[MAX_STR];
} Dep;

typedef struct {
  char name[MAX_NAME];
  char version[MAX_STR];
  char c_standard[32];
  char warnings[32];
  char src_dir[MAX_PATH];
  char out_dir[MAX_PATH];
  Dep deps[MAX_DEPS];
  int dep_count;
  char extra_sources[MAX_EXTRA][MAX_PATH];
  int extra_count;
} Manifest;

int manifest_load(const char *path, Manifest *out);

#endif
