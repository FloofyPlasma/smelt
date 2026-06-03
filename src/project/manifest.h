#ifndef SMELT_MANIFEST_H
#define SMELT_MANIFEST_H

#include "core/stringvec.h"
#include "core/vec.h"

#define MAX_NAME 128
#define MAX_STR 256
#define MAX_PATH 1024
#define MAX_REGISTRIES 8

typedef enum {
  DEP_GIT,
  DEP_LOCAL,
  DEP_PKG_CONFIG,
  DEP_SYSTEM,
} DepKind;

typedef struct {
  DepKind kind;
  char name[MAX_NAME];
  char version[MAX_STR];

  union {
    struct {
      char git[MAX_STR];
      char tag[MAX_STR];
      char commit[64];
    } git;
    struct {
      char path[MAX_PATH];
    } local;
    struct {
      char pkg_config[MAX_NAME];
      char link_flag[MAX_STR];
    } pkg;
  };

  StringVec features;
} Dep;

typedef VEC(Dep) DepVec;

typedef struct {
  StringVec flags;
  StringVec link_flags;
} Profile;

typedef struct {
  char name[MAX_NAME];
  char out[MAX_PATH];
  StringVec exclude;
} Target;

typedef VEC(Target) TargetVec;

typedef struct {
  char name[MAX_NAME];
  char version[MAX_STR];
  char c_standard[32];
  char cpp_standard[32];
  char warnings[32];
  char src_dir[MAX_PATH];
  char out_dir[MAX_PATH];

  DepVec deps;

  StringVec extra_sources;
  StringVec include_dirs;
  StringVec link_flags;
  StringVec defines;
  StringVec dep_sources;
  StringVec registries;

  Profile debug;
  Profile release;

  TargetVec targets;
} Manifest;

void manifest_free(Manifest *m);
int manifest_load(const char *path, Manifest *out);

int manifest_save(const char *path, const Manifest *m);

int manifest_add_dep(Manifest *m, const char *name, const char *version);

#endif
