#define _POSIX_C_SOURCE 200809L
#include "pkg/deps.h"
#include "core/fs.h"
#include "core/process.h"
#include "pkg/git_deps.h"
#include "project/lock.h"
#include "project/manifest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void parse_pkgconfig_cflags(char *line, void *ud) {
  Manifest *m = ud;
  char *saveptr = NULL;
  char *tok = strtok_r(line, " ", &saveptr);
  while (tok) {
    if (strncmp(tok, "-I", 2) == 0)
      stringvec_push_unique(&m->include_dirs, tok + 2);
    else if (strncmp(tok, "-D", 2) == 0)
      stringvec_push_unique(&m->defines, tok + 2);
    tok = strtok_r(NULL, " ", &saveptr);
  }
}

static void parse_pkgconfig_libs(char *line, void *ud) {
  Manifest *m = ud;
  char *saveptr = NULL;
  char *tok = strtok_r(line, " ", &saveptr);
  while (tok) {
    stringvec_push_unique(&m->link_flags, tok);
    tok = strtok_r(NULL, " ", &saveptr);
  }
}

static void cache_path(char *dst, size_t dstsz, const char *name) {
  const char *home = getenv("HOME");
  if (!home || home[0] == '\0')
    home = "/tmp";
  snprintf(dst, dstsz, "%s/.cache/smelt/%s", home, name);
}

static void repo_name(char *dst, size_t dstsz, const char *url) {
  const char *s = strrchr(url, '/');
  s = s ? s + 1 : url;
  snprintf(dst, dstsz, "%s", s);
  for (char *p = dst; *p; p++)
    *p = (*p >= 'A' && *p <= 'Z') ? (*p + 32) : *p;
  size_t len = strlen(dst);
  if (len > 4 && strcmp(dst + len - 4, ".git") == 0)
    dst[len - 4] = '\0';
}

static int dir_exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int file_exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int copy_file(const char *src, const char *dst) {
  FILE *in = fopen(src, "rbe");
  if (!in) {
    perror(src);
    return 0;
  }
  FILE *out = fopen(dst, "wbe");
  if (!out) {
    perror(dst);
    fclose(in);
    return 0;
  }
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
    if (fwrite(buf, 1, n, out) != n) {
      fclose(in);
      fclose(out);
      return 0;
    }
  }
  fclose(in);
  fclose(out);
  return 1;
}

static const char *base_name(const char *path) {
  const char *s = strrchr(path, '/');
  return s ? s + 1 : path;
}

static int fetch_git(const char *url, char *cache_out, size_t cache_sz,
                     LockFile *lf, int use_lock) {
  char name[MAX_NAME];
  repo_name(name, sizeof(name), url);
  cache_path(cache_out, cache_sz, name);

  if (!dir_exists(cache_out)) {
    ensure_dirs(cache_out);
    git_repository *repo = NULL;
    if (!clone_repo(repo, url, cache_out)) {
      fprintf(stderr, "smelt: git clone failed\n");
      return 0;
    }
  } else {
    printf("smelt: using cached %s\n", cache_out);
  }

  if (use_lock && lf) {
    const char *pinned = lockfile_get_commit(lf, name);
    if (pinned) {
      Process fetch = {0};
      process_argv_push(&fetch, "git");
      process_argv_push(&fetch, "-C");
      process_argv_push(&fetch, cache_out);
      process_argv_push(&fetch, "fetch");
      process_argv_push(&fetch, "--depth=1");
      process_argv_push(&fetch, "origin");
      process_argv_push(&fetch, pinned);
      process_run(&fetch);
      process_free(&fetch);

      Process checkout = {0};
      process_argv_push(&checkout, "git");
      process_argv_push(&checkout, "-C");
      process_argv_push(&checkout, cache_out);
      process_argv_push(&checkout, "checkout");
      process_argv_push(&checkout, "FETCH_HEAD");
      process_run(&checkout);
      process_free(&checkout);
      printf("smelt: pinned %s @ %s\n", name, pinned);
    }
  }

  char commit[64] = {0};
  Process rev_parse = {0};
  process_argv_push(&rev_parse, "git");
  process_argv_push(&rev_parse, "-C");
  process_argv_push(&rev_parse, cache_out);
  process_argv_push(&rev_parse, "rev-parse");
  process_argv_push(&rev_parse, "HEAD");
  if (process_capture(&rev_parse, commit, sizeof(commit)))
    commit[strcspn(commit, "\n")] = '\0';
  process_free(&rev_parse);

  if (lf && commit[0])
    lockfile_set(lf, name, url, commit);

  return 1;
}

int dep_add_git(const char *url, const StringVec *files) {
  LockFile lf;
  lockfile_load(&lf);

  char cache[MAX_PATH];
  if (!fetch_git(url, cache, sizeof(cache), &lf, 1))
    return 0;

  mkdir("vendor", 0755);

  for (size_t i = 0; i < stringvec_len(files); i++) {
    const char *f = stringvec_get(files, i);
    char src[MAX_PATH * 2];
    snprintf(src, sizeof(src), "%s/%s", cache, f);
    char dst[MAX_PATH];
    snprintf(dst, sizeof(dst), "vendor/%s", f);
    if (!copy_file(src, dst))
      return 0;
    printf("smelt: copied %s -> %s\n", f, dst);
  }

  lockfile_save(&lf);
  return 1;
}

int dep_add_local(const char *path) {
  char smelt_toml[MAX_PATH];
  snprintf(smelt_toml, sizeof(smelt_toml), "%s/smelt.toml", path);

  if (!file_exists(smelt_toml)) {
    fprintf(stderr,
            "smelt: no smelt.toml in %s\n"
            "  use 'smelt add <url> <files...>' for manual git deps\n",
            path);
    return 0;
  }

  printf("smelt: found smelt.toml in %s - smelt-aware dep\n", path);
  return 1;
}

int dep_add_pkgconfig(const char *name) {
  Process pkg_exists = {0};
  process_argv_push(&pkg_exists, "pkg-config");
  process_argv_push(&pkg_exists, "--exists");
  process_argv_push(&pkg_exists, name);
  if (!process_run(&pkg_exists)) {
    fprintf(stderr,
            "smelt: system dependency not found: %s\n"
            "  add to your system and re-run smelt build\n",
            name);
    process_free(&pkg_exists);
    return 0;
  }
  process_free(&pkg_exists);
  return 1;
}

static int ensure_git_dep(Dep *dep, Manifest *m, LockFile *lf) {
  char cache[MAX_PATH];
  if (!fetch_git(dep->git.git, cache, sizeof(cache), lf, 1))
    return 0;

  mkdir("vendor", 0755);

  int vendor_added = 0;
  for (size_t i = 0; i < stringvec_len(&dep->features); i++) {
    (void)i;
  }

  (void)vendor_added;
  (void)m;

  return 1;
}

static int ensure_local_dep(Dep *dep, Manifest *m) {
  const char *path = dep->local.path;

  char smelt_toml[MAX_PATH];
  snprintf(smelt_toml, sizeof(smelt_toml), "%s/smelt.toml", path);
  if (!file_exists(smelt_toml)) {
    fprintf(stderr, "smelt: no smelt.toml in %s\n", path);
    return 0;
  }

  Manifest dm = {0};
  if (!manifest_load(smelt_toml, &dm))
    return 0;

  printf("smelt: resolving local dep %s (%s)\n", dep->name, path);

  char dep_src[MAX_PATH];
  snprintf(dep_src, sizeof(dep_src), "%s/%s", path, dm.src_dir);
  stringvec_push_unique(&m->include_dirs, dep_src);

  for (size_t i = 0; i < stringvec_len(&dm.include_dirs); i++) {
    char inc[MAX_PATH];
    snprintf(inc, sizeof(inc), "%s/%s", path,
             stringvec_get(&dm.include_dirs, i));
    stringvec_push_unique(&m->include_dirs, inc);
  }

  Process find_c = {0};
  process_argv_push(&find_c, "find");
  process_argv_push(&find_c, dep_src);
  process_argv_push(&find_c, "-name");
  process_argv_push(&find_c, "*.c");

  char output[65536] = {0};
  if (process_capture(&find_c, output, sizeof(output))) {
    char *saveptr = NULL;
    char *line = strtok_r(output, "\n", &saveptr);
    while (line) {
      if (line[0])
        stringvec_push_unique(&m->dep_sources, line);
      line = strtok_r(NULL, "\n", &saveptr);
    }
  }
  process_free(&find_c);

  manifest_free(&dm);
  return 1;
}

static int ensure_pkgconfig_dep(Dep *dep, Manifest *m) {
  const char *pkg = dep->pkg.pkg_config;

  Process proc = {0};
  process_argv_push(&proc, "pkg-config");
  process_argv_push(&proc, "--exists");
  process_argv_push(&proc, pkg);
  if (!process_run(&proc)) {
    fprintf(stderr,
            "smelt: system dependency not found: %s\n"
            "  add to your system and re-run smelt build\n",
            pkg);
    process_free(&proc);
    return 0;
  }
  process_free(&proc);

  Process cflags = {0};
  process_argv_push(&cflags, "pkg-config");
  process_argv_push(&cflags, "--cflags");
  process_argv_push(&cflags, pkg);
  if (!process_capture_lines(&cflags, parse_pkgconfig_cflags, m)) {
    process_free(&cflags);
    return 0;
  }
  process_free(&cflags);

  Process libs = {0};
  process_argv_push(&libs, "pkg-config");
  process_argv_push(&libs, "--libs");
  process_argv_push(&libs, pkg);
  if (!process_capture_lines(&libs, parse_pkgconfig_libs, m)) {
    process_free(&libs);
    return 0;
  }
  process_free(&libs);

  printf("smelt: pkg-config: resolved %s\n", pkg);
  return 1;
}

static int ensure_system_dep(Dep *dep, Manifest *m) {
  if (dep->pkg.link_flag[0])
    stringvec_push_unique(&m->link_flags, dep->pkg.link_flag);
  return 1;
}

int deps_update(Manifest *m) {
  LockFile lf = {0};

  for (size_t i = 0; i < vec_len(&m->deps); i++) {
    Dep *dep = &m->deps.items[i];

    switch (dep->kind) {
    case DEP_PKG_CONFIG:
      printf("smelt: skip %s (pkg-config, system managed)\n", dep->name);
      break;
    case DEP_SYSTEM:
      printf("smelt: skip %s (system link flag)\n", dep->name);
      break;
    case DEP_LOCAL:
      printf("smelt: skip %s (local)\n", dep->name);
      break;
    case DEP_GIT: {
      char name[MAX_NAME];
      repo_name(name, sizeof(name), dep->git.git);
      char cache_dir[MAX_PATH];
      cache_path(cache_dir, sizeof(cache_dir), name);

      if (dir_exists(cache_dir)) {
        Process fetch = {0};
        process_argv_push(&fetch, "git");
        process_argv_push(&fetch, "-C");
        process_argv_push(&fetch, cache_dir);
        process_argv_push(&fetch, "fetch");
        process_argv_push(&fetch, "--depth=1");
        process_argv_push(&fetch, "origin");
        process_argv_push(&fetch, "HEAD");
        if (process_run(&fetch)) {
          Process reset = {0};
          process_argv_push(&reset, "git");
          process_argv_push(&reset, "-C");
          process_argv_push(&reset, cache_dir);
          process_argv_push(&reset, "reset");
          process_argv_push(&reset, "--hard");
          process_argv_push(&reset, "FETCH_HEAD");
          if (!process_run(&reset)) {
            process_free(&reset);
            process_free(&fetch);
            return 0;
          }
          process_free(&reset);
        }
        process_free(&fetch);
        printf("smelt: updating %s\n", name);
      }

      Process rev = {0};
      process_argv_push(&rev, "git");
      process_argv_push(&rev, "-C");
      process_argv_push(&rev, cache_dir);
      process_argv_push(&rev, "rev-parse");
      process_argv_push(&rev, "HEAD");
      char commit[64] = {0};
      if (process_capture(&rev, commit, sizeof(commit)))
        commit[strcspn(commit, "\n")] = '\0';
      process_free(&rev);

      if (commit[0]) {
        lockfile_set(&lf, name, dep->git.git, commit);
        printf("smelt: updated %s @ %.8s\n", name, commit);
      }
      break;
    }
    }
  }

  lockfile_save(&lf);
  printf("smelt: lockfile updated\n");
  return 1;
}

int deps_ensure(Manifest *m) {
  LockFile lf;
  lockfile_load(&lf);
  int dirty = 0;

  for (size_t i = 0; i < vec_len(&m->deps); i++) {
    Dep *dep = &m->deps.items[i];

    switch (dep->kind) {
    case DEP_PKG_CONFIG:
      if (!ensure_pkgconfig_dep(dep, m))
        return 0;
      break;
    case DEP_SYSTEM:
      if (!ensure_system_dep(dep, m))
        return 0;
      break;
    case DEP_LOCAL:
      if (!ensure_local_dep(dep, m))
        return 0;
      break;
    case DEP_GIT:
      if (!ensure_git_dep(dep, m, &lf))
        return 0;
      dirty = 1;
      break;
    }
  }

  if (dirty)
    lockfile_save(&lf);
  return 1;
}
