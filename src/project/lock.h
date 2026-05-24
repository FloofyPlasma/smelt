#ifndef SMELT_LOCK_H
#define SMELT_LOCK_H

#include "core/vec.h"
#include "pkg/resolve.h"

#define LOCK_FILE "smelt.lock"

typedef struct {
  char name[MAX_NAME];
  char version[MAX_STR];
  char commit[64];
  char git[MAX_STR];
  char tag[MAX_STR];
  int direct;
  char required_by[MAX_NAME];
} LockEntry;

typedef VEC(LockEntry) LockVec;

typedef struct {
  LockVec entries;
} LockFile;

int lockfile_write(const ResolvedPackageVec *pkgs);

int lockfile_load(LockFile *lf);

const char *lockfile_get_commit(const LockFile *lf, const char *name);

void lockfile_free(LockFile *lf);

#endif
