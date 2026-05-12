#ifndef SMELT_MANIFEST_H
#define SMELT_MANIFEST_H

#define MAX_NAME 128
#define MAX_STR 256
#define MAX_PATH 1024
#define MAX_DEPS 64
#define MAX_EXTRA 16
#define MAX_INCLUDES 32
#define MAX_PROFILE_FLAGS 32
#define MAX_LINK_FLAGS 32
#define MAX_DEFINES 32
#define MAX_DEP_FILES 16
#define MAX_DEP_SOURCES 256
#define MAX_REGISTRIES 8

typedef struct {
  char name[MAX_NAME];
  char git[MAX_STR];
  char path[MAX_PATH];
  char pkg_config[MAX_NAME];
  char files[MAX_DEP_FILES][MAX_PATH];
  int file_count;
  int is_local;
  int is_smelt_aware;
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
  char link_flags[MAX_LINK_FLAGS][MAX_PATH];
  int link_flag_count;
  char defines[MAX_DEFINES][MAX_PATH];
  int define_count;
  char dep_sources[MAX_DEP_SOURCES][MAX_PATH];
  int dep_source_count;
  char registries[MAX_REGISTRIES][MAX_STR];
  int registry_count;
} Manifest;

int manifest_load(const char *path, Manifest *out);

#endif
