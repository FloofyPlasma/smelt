#include "project/lock.h"
#include <stdio.h>
#include <string.h>

int lockfile_load(LockFile *lf) {
  *lf = (LockFile){0};

  FILE *fp = fopen(LOCK_FILE, "re");
  if (!fp)
    return 1;

  char line[512];
  LockEntry *cur = NULL;

  while (fgets(line, sizeof(line), fp)) {
    line[strcspn(line, "\n")] = '\0';
    if (line[0] == '\0')
      continue;

    if (line[0] == '#')
      continue;

    if (line[0] == '[') {
      if (lf->count >= MAX_LOCK_ENTRIES)
        break;
      cur = &lf->entries[lf->count++];
      *cur = (LockEntry){0};

      char *end = strchr(line, ']');
      if (end)
        *end = '\0';
      snprintf(cur->name, sizeof(cur->name), "%s", line + 1);
      continue;
    }

    if (!cur)
      continue;

    char *eq = strchr(line, '=');
    if (!eq)
      continue;
    *eq = '\0';
    char *key = line;
    char *val = eq + 1;

    while (*key == ' ' || *key == '\t')
      key++;
    while (*val == ' ' || *val == '\t')
      val++;

    char *key_end = key + strlen(key) - 1;
    while (key_end > key && (*key_end == ' ' || *key_end == '\t'))
      *key_end-- = '\0';

    if (val[0] == '"') {
      val++;
      char *q = strchr(val, '"');
      if (q)
        *q = '\0';
    }

    if (strcmp(key, "git") == 0)
      snprintf(cur->git, sizeof(cur->git), "%s", val);
    else if (strcmp(key, "commit") == 0)
      snprintf(cur->commit, sizeof(cur->commit), "%s", val);
  }

  fclose(fp);
  return 1;
}

int lockfile_save(const LockFile *lf) {
  FILE *fp = fopen(LOCK_FILE, "we");
  if (!fp) {
    perror("smelt: cannot write smelt.lock");
    return 0;
  }

  fprintf(fp, "# smelt.lock -- do not edit manually --\n\n");
  for (int i = 0; i < lf->count; i++) {
    const LockEntry *e = &lf->entries[i];
    fprintf(fp, "[%s]\n", e->name);
    if (e->git[0])
      fprintf(fp, "git = \"%s\"\n", e->git);
    if (e->commit[0])
      fprintf(fp, "commit = \"%s\"\n", e->commit);
    fprintf(fp, "\n");
  }

  fclose(fp);
  return 1;
}

const char *lockfile_get_commit(const LockFile *lf, const char *name) {
  for (int i = 0; i < lf->count; i++)
    if (strcmp(lf->entries[i].name, name) == 0)
      return lf->entries[i].commit[0] ? lf->entries[i].commit : NULL;
  return NULL;
}

void lockfile_set(LockFile *lf, const char *name, const char *git,
                  const char *commit) {
  for (int i = 0; i < lf->count; i++) {
    if (strcmp(lf->entries[i].name, name) == 0) {
      snprintf(lf->entries[i].git, sizeof(lf->entries[i].git), "%s", git);
      snprintf(lf->entries[i].commit, sizeof(lf->entries[i].commit), "%s",
               commit);
      return;
    }
  }

  if (lf->count >= MAX_LOCK_ENTRIES)
    return;
  LockEntry *e = &lf->entries[lf->count++];
  snprintf(e->name, sizeof(e->name), "%s", name);
  snprintf(e->git, sizeof(e->git), "%s", git);
  snprintf(e->commit, sizeof(e->commit), "%s", commit);
}
