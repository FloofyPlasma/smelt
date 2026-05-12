#ifndef SMELT_LOCK_H
#define SMELT_LOCK_H

#include "project/manifest.h"

#define MAX_LOCK_ENTRIES 64
#define LOCK_FILE "smelt.lock"

typedef struct {
  char name[MAX_NAME];
  char git[MAX_STR];
  char commit[64];
} LockEntry;

typedef struct {
  LockEntry entries[MAX_LOCK_ENTRIES];
  int count;
} LockFile;

int lockfile_load(LockFile *lf);
int lockfile_save(const LockFile *lf);
const char *lockfile_get_commit(const LockFile *lf, const char *name);
void lockfile_set(LockFile *lf, const char *name, const char *git,
                  const char *commit);

#endif
