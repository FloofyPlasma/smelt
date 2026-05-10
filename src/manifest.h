#ifndef SMELT_MANIFEST_H
#define SMELT_MANIFEST_H

#define MAX_NAME 128
#define MAX_STR 256
#define MAX_PATH 1024
#define MAX_DEPS 64
#define MAX_EXTRA 16
#define MAX_INCLUDES 32
#define MAX_PROFILE_FLAGS 32

typedef struct {
  char name[MAX_NAME];
  char version[MAX_STR];
} Dep;

typedef struct {
  char flags[MAX_PROFILE_FLAGS][MAX_STR];
  int flag_count;
} Profile;

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
  char include_dirs[MAX_INCLUDES][MAX_PATH];
  int include_count;
  Profile debug;
  Profile release;
} Manifest;

int manifest_load(const char *path, Manifest *out);

#endif
