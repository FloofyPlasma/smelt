#ifndef SMELT_MANIFEST_H
#define SMELT_MANIFEST_H

#include "core/stringvec.h"

// TODO(FloofyPlasma): make MAX_DEPS, MAX_DEP_FILES, MAX_DEP_SOURCES,
//                     MAX_REGISTRIES obsolete. the other ones are sensible
//                     defaults, maybe move to a constants header instead?
#define MAX_NAME 128
#define MAX_STR 256
#define MAX_PATH 1024
#define MAX_DEPS 64
#define MAX_DEP_FILES 16
#define MAX_DEP_SOURCES 256
#define MAX_REGISTRIES 8

typedef struct {
  char name[MAX_NAME];
  char git[MAX_STR];
  char path[MAX_PATH];
  char pkg_config[MAX_NAME];
  StringVec files;
  int file_count;
  int is_local;
  int is_smelt_aware;
} Dep;

typedef struct {
  StringVec flags;
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
  StringVec extra_sources;
  StringVec include_dirs;
  Profile debug;
  Profile release;
  StringVec link_flags;
  StringVec defines;
  StringVec dep_sources;
  StringVec registries;
} Manifest;

void manifest_free(Manifest *m);
int manifest_load(const char *path, Manifest *out);

#endif
