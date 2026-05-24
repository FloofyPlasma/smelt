#include "project/lock.h"
#include "core/vec.h"
#include "pkg/resolve.h"
#include <stdio.h>
#include <string.h>

int lockfile_write(const ResolvedPackageVec *pkgs) {
  FILE *fp = fopen(LOCK_FILE, "we");
  if (!fp) {
    perror("smelt: cannot write smelt.lock");
    return 0;
  }

  fprintf(fp, "# smelt.lock -- do not edit manually --\n\n");

  for (size_t i = 0; i < vec_len(pkgs); i++) {
    const ResolvedPackage *p = &pkgs->items[i];

    fprintf(fp, "[[package]]\n");
    fprintf(fp, "name     = \"%s\"\n", p->name);
    fprintf(fp, "version  = \"%s\"\n", p->version);
    fprintf(fp, "commit   = \"%s\"\n", p->commit);
    fprintf(fp, "git      = \"%s\"\n", p->git);
    fprintf(fp, "tag      = \"%s\"\n", p->tag);
    fprintf(fp, "direct   = %s\n", p->direct ? "true" : "false");
    if (!p->direct && p->required_by[0])
      fprintf(fp, "required_by = \"%s\"\n", p->required_by);
    fprintf(fp, "\n");
  }

  fclose(fp);
  return 1;
}

static void trim_quotes(char *s) {
  size_t len = strlen(s);

  while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' ||
                     s[len - 1] == '\r' || s[len - 1] == '\n'))
    s[--len] = '\0';

  if (len >= 2 && s[0] == '"' && s[len - 1] == '"') {
    memmove(s, s + 1, len - 2);
    s[len - 2] = '\0';
  }
}

int lockfile_load(LockFile *lf) {
  lf->entries = (LockVec){0};

  FILE *fp = fopen(LOCK_FILE, "re");
  if (!fp)
    return 1;

  char line[1024];
  LockEntry cur = {0};
  int has_cur = 0;

  while (fgets(line, sizeof(line), fp)) {
    line[strcspn(line, "\n")] = '\0';

    if (line[0] == '#' || line[0] == '\0')
      continue;

    if (strcmp(line, "[[package]]") == 0) {
      if (has_cur) {
        if (!vec_push(&lf->entries, cur)) {
          fclose(fp);
          lockfile_free(lf);
          return 0;
        }
      }
      cur = (LockEntry){0};
      has_cur = 1;
      continue;
    }

    if (!has_cur)
      continue;

    char *eq = strchr(line, '=');
    if (!eq)
      continue;

    *eq = '\0';
    char *key = line;
    char *val = eq + 1;

    while (*key == ' ' || *key == '\t')
      key++;
    char *ke = key + strlen(key) - 1;
    while (ke > key && (*ke == ' ' || *ke == '\t'))
      *ke-- = '\0';

    while (*val == ' ' || *val == '\t')
      val++;
    trim_quotes(val);

    if (strcmp(key, "name") == 0)
      snprintf(cur.name, sizeof(cur.name), "%s", val);
    else if (strcmp(key, "version") == 0)
      snprintf(cur.version, sizeof(cur.version), "%s", val);
    else if (strcmp(key, "commit") == 0)
      snprintf(cur.commit, sizeof(cur.commit), "%s", val);
    else if (strcmp(key, "git") == 0)
      snprintf(cur.git, sizeof(cur.git), "%s", val);
    else if (strcmp(key, "tag") == 0)
      snprintf(cur.tag, sizeof(cur.tag), "%s", val);
    else if (strcmp(key, "direct") == 0)
      cur.direct = strcmp(val, "true") == 0;
    else if (strcmp(key, "required_by") == 0)
      snprintf(cur.required_by, sizeof(cur.required_by), "%s", val);
  }

  if (has_cur) {
    if (!vec_push(&lf->entries, cur)) {
      fclose(fp);
      lockfile_free(lf);
      return 0;
    }
  }

  fclose(fp);
  return 1;
}

const char *lockfile_get_commit(const LockFile *lf, const char *name) {
  for (size_t i = 0; i < vec_len(&lf->entries); i++) {
    const LockEntry *e = &lf->entries.items[i];
    if (strcmp(e->name, name) == 0)
      return e->commit[0] ? e->commit : NULL;
  }
  return NULL;
}

void lockfile_free(LockFile *lf) { vec_free(&lf->entries); }
