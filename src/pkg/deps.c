#define _POSIX_C_SOURCE 200809L
#include "pkg/deps.h"
#include "core/process.h"
#include "project/lock.h"
#include "project/manifest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int process_capture_lines(Process *proc,
                                 void (*fn)(char *line, void *ud), void *ud) {
  char output[65536] = {0};

  if (!process_capture(proc, output, sizeof(output)))
    return 0;

  char *saveptr = NULL;

  char *line = strtok_r(output, "\n", &saveptr);

  while (line) {
    fn(line, ud);

    line = strtok_r(NULL, "\n", &saveptr);
  }

  return 1;
}

static void parse_pkgconfig_cflags(char *line, void *ud) {
  Manifest *m = ud;

  char *saveptr = NULL;

  char *tok = strtok_r(line, " ", &saveptr);

  while (tok) {
    if (strncmp(tok, "-I", 2) == 0) {
      stringvec_push_unique(&m->include_dirs, tok + 2);
    } else if (strncmp(tok, "-D", 2) == 0) {
      stringvec_push_unique(&m->defines, tok + 2);
    }

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

// TODO(floofyplasma): treat all repo names as lowercase
static void repo_name(char *dst, size_t dstsz, const char *url) {
  const char *s = strrchr(url, '/');
  s = s ? s + 1 : url;
  snprintf(dst, dstsz, "%s", s);
  size_t len = strlen(dst);
  if (len > 4 && strcmp(dst + len - 4, ".git") == 0)
    dst[len - 4] = '\0';
}

// TODO(floofyplasma): make this platform independent
static int dir_exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

// TODO(floofyplasma): make this platform independent
static int file_exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

// TODO(floofyplasma): make this platform independent, add error handling
static void ensure_dirs(const char *path) {
  char tmp[MAX_PATH];
  snprintf(tmp, sizeof(tmp), "%s", path);
  for (char *p = tmp + 1; *p; p++) {
    if (*p == '/') {
      *p = '\0';
      mkdir(tmp, 0755);
      *p = '/';
    }
  }
  mkdir(tmp, 0755);
}

// TODO(floofyplasma): add error handling
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

// FIXME: duplicated across files
static int ends_with_c(const char *name) {
  size_t len = strlen(name);
  return len > 2 && name[len - 2] == '.' && name[len - 1] == 'c';
}

// FIXME: this is very fucking disgusting, should probably be rewritten entirely
static int toml_write_dep(const char *entry) {
  FILE *fp = fopen("smelt.toml", "re");
  if (fp) {
    char line[1024];
    while (fgets(line, sizeof(line), fp)) {
      if (strstr(line, entry)) {
        fclose(fp);
        return 1;
      }
    }
    fclose(fp);
  }

  fp = fopen("smelt.toml", "re");
  if (!fp)
    return 0;
  char content[65536] = {0};
  fread(content, 1, sizeof(content) - 1, fp);
  fclose(fp);

  fp = fopen("smelt.toml", "we");
  if (!fp)
    return 0;

  if (strstr(content, "[dependencies]")) {
    char *pos = strstr(content, "[dependencies]");
    char *end = pos + strlen("[dependencies]");

    char *nl = strchr(end, '\n');
    if (nl)
      nl++;
    fwrite(content, 1, nl ? (size_t)(nl - content) : strlen(content), fp);
    fprintf(fp, "%s\n", entry);
    fputs(nl ? nl : "", fp);
  } else {
    fputs(content, fp);
    fprintf(fp, "\n[dependencies]\n%s\n", entry);
  }
  fclose(fp);
  printf("smelt: updated smelt.toml\n");
  return 1;
}

static int fetch_git(const char *url, char *cache_out, size_t cache_sz,
                     LockFile *lf, int use_lock) {
  char name[MAX_NAME];
  repo_name(name, sizeof(name), url);
  cache_path(cache_out, cache_sz, name);

  if (!dir_exists(cache_out)) {
    ensure_dirs(cache_out);
    Process clone = {0};

    process_argv_push(&clone, "git");
    process_argv_push(&clone, "clone");
    process_argv_push(&clone, "--depth=1");
    process_argv_push(&clone, url);
    process_argv_push(&clone, cache_out);
    process_print(&clone);
    if (!process_run(&clone)) {
      fprintf(stderr, "smelt: git clone failed\n");
      process_free(&clone);
      return 0;
    }
    process_free(&clone);
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
  if (process_capture(&rev_parse, commit, sizeof(commit))) {
    commit[strcspn(commit, "\n")] = '\0';
  }
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

  char files_str[4096] = {0};
  int fpos = 0;
  fpos += snprintf(files_str + fpos, sizeof(files_str) - fpos, "[");
  for (size_t i = 0; i < stringvec_len(files); i++) {
    fpos += snprintf(files_str + fpos, sizeof(files_str) - fpos, "\"%s\"%s",
                     stringvec_get(files, i),
                     i < stringvec_len(files) - 1 ? ", " : "");
  }
  snprintf(files_str + fpos, sizeof(files_str) - fpos, "]");

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

  // FIXME: abstract into own function
  char name[MAX_NAME];
  repo_name(name, sizeof(name), url);
  char entry[MAX_STR * 4];
  snprintf(entry, sizeof(entry), "%s = { git = \"%s\", files = %s }", name, url,
           files_str);
  toml_write_dep(entry);

  lockfile_save(&lf);
  return 1;
}

int dep_add_local(const char *path) {
  char smelt_toml[MAX_PATH];
  snprintf(smelt_toml, sizeof(smelt_toml), "%s/smelt.toml", path);

  int smelt_aware = file_exists(smelt_toml);

  char entry[MAX_STR * 2];
  const char *name = base_name(path);
  if (smelt_aware) {
    printf("smelt: found smelt.toml in %s - smelt-aware dep\n", path);
    snprintf(entry, sizeof(entry), "%s = { path = \"%s\" }", name, path);
  } else {
    fprintf(stderr,
            "smelt: no smelt.toml in %s - use 'smelt add <url> <files...> for "
            "manual deps\n",
            path);
    return 0;
  }

  toml_write_dep(entry);
  return 1;
}

int dep_add_pkgconfig(const char *name) {
  Process pkg_exists = {0};

  process_argv_push(&pkg_exists, "pkg-config");
  process_argv_push(&pkg_exists, "--exists");
  process_argv_push(&pkg_exists, name);
  if (!process_run(&pkg_exists)) {
    fprintf(stderr, "smelt: pkg-config: %s not found\n", name);
    process_free(&pkg_exists);
    return 0;
  }
  process_free(&pkg_exists);

  // FIXME: ditto
  char entry[MAX_STR * 2];
  snprintf(entry, sizeof(entry), "%s = { pkg-config = \"%s\" }", name, name);
  return toml_write_dep(entry);
}

static int ensure_git_dep(Dep *dep, Manifest *m, LockFile *lf) {
  char cache[MAX_PATH];
  if (!fetch_git(dep->git, cache, sizeof(cache), lf, 1))
    return 0;

  mkdir("vendor", 0755);

  int vendor_added = 0;
  for (size_t i = 0; i < stringvec_len(&dep->files); i++) {
    const char *f = stringvec_get(&dep->files, i);
    char src[MAX_PATH * 2];
    snprintf(src, sizeof(src), "%s/%s", cache, f);
    const char *fname = base_name(f);
    char dst[MAX_PATH];
    snprintf(dst, sizeof(dst), "vendor/%s", fname);

    if (!file_exists(dst)) {
      if (!copy_file(src, dst))
        return 0;
      printf("smelt: vendored %s\n", dst);
    }

    if (ends_with_c(fname))
      stringvec_push_unique(&m->extra_sources, dst);

    vendor_added = 1;
  }

  if (vendor_added)
    stringvec_push_unique(&m->include_dirs, "vendor");

  return 1;
}

static int ensure_local_dep(Dep *dep, Manifest *m) {
  char smelt_toml[MAX_PATH];
  snprintf(smelt_toml, sizeof(smelt_toml), "%s/smelt.toml", dep->path);
  if (!file_exists(smelt_toml)) {
    fprintf(stderr, "smelt: no smelt.toml in %s\n", dep->path);
    return 0;
  }
  Manifest dm = {0};
  if (!manifest_load(smelt_toml, &dm))
    return 0;
  printf("smelt: resolving local dep %s (%s)\n", dep->name, dep->path);

  char dep_src[MAX_PATH];
  snprintf(dep_src, sizeof(dep_src), "%s/%s", dep->path, dm.src_dir);
  stringvec_push_unique(&m->include_dirs, dep_src);

  for (size_t i = 0; i < stringvec_len(&dm.include_dirs); i++) {
    char inc[MAX_PATH];
    snprintf(inc, sizeof(inc), "%s/%s", dep->path,
             stringvec_get(&dm.include_dirs, i));
    stringvec_push_unique(&m->include_dirs, inc);
  }

  Process find_c_files = {0};

  process_argv_push(&find_c_files, "find");
  process_argv_push(&find_c_files, dep_src);
  process_argv_push(&find_c_files, "-name");
  process_argv_push(&find_c_files, "*.c");

  char output[65536] = {0};

  if (process_capture(&find_c_files, output, sizeof(output))) {
    char *saveptr = NULL;

    char *line = strtok_r(output, "\n", &saveptr);

    while (line) {
      if (line[0])
        stringvec_push_unique(&m->dep_sources, line);

      line = strtok_r(NULL, "\n", &saveptr);
    }
  }

  process_free(&find_c_files);

  manifest_free(&dm);
  return 1;
}

static int ensure_pkgconfig_dep(Dep *dep, Manifest *m) {
  Process proc = {0};

  process_argv_push(&proc, "pkg-config");
  process_argv_push(&proc, "--exists");
  process_argv_push(&proc, dep->pkg_config);

  if (!process_run(&proc)) {
    fprintf(stderr, "smelt: pkg-config: %s not found\n", dep->pkg_config);

    fprintf(stderr,
            "smelt: try installing %s with your system package manager\n",
            dep->pkg_config);

    process_free(&proc);

    return 0;
  }

  process_free(&proc);

  Process cflags = {0};

  process_argv_push(&cflags, "pkg-config");
  process_argv_push(&cflags, "--cflags");
  process_argv_push(&cflags, dep->pkg_config);

  if (!process_capture_lines(&cflags, parse_pkgconfig_cflags, m)) {
    process_free(&cflags);
    return 0;
  }

  process_free(&cflags);

  Process libs = {0};

  process_argv_push(&libs, "pkg-config");
  process_argv_push(&libs, "--libs");
  process_argv_push(&libs, dep->pkg_config);

  if (!process_capture_lines(&libs, parse_pkgconfig_libs, m)) {
    process_free(&libs);
    return 0;
  }

  process_free(&libs);

  printf("smelt: pkg-config: resolved %s\n", dep->pkg_config);

  return 1;
}

int deps_update(Manifest *m) {
  LockFile lf = {0};

  for (int i = 0; i < m->dep_count; i++) {
    Dep *dep = &m->deps[i];
    if (dep->pkg_config[0]) {
      printf("smelt: skip %s (pkg-config, system managed)\n", dep->name);
    } else if (dep->is_local) {
      printf("smelt: skip %s (local)\n", dep->name);
    } else if (dep->git[0]) {
      char cache[MAX_PATH];

      char name[MAX_NAME];
      repo_name(name, sizeof(name), dep->git);
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
            return 0;
          }
          process_free(&reset);
        }
        process_free(&fetch);
        printf("smelt: updating %s\n", name);
        snprintf(cache, sizeof(cache), "%s", cache_dir);
      }

      Process parse_commit = {0};

      process_argv_push(&parse_commit, "git");
      process_argv_push(&parse_commit, "-C");
      process_argv_push(&parse_commit, cache_dir);
      process_argv_push(&parse_commit, "rev-parse");
      process_argv_push(&parse_commit, "HEAD");
      char commit[64] = {0};
      if (process_capture(&parse_commit, commit, sizeof(commit))) {
        commit[strcspn(commit, "\n")] = '\0';
      }
      process_free(&parse_commit);

      if (commit[0]) {
        lockfile_set(&lf, name, dep->git, commit);
        printf("smelt: updated %s @ %.8s\n", name, commit);
      }

      if (!ensure_git_dep(dep, m, &lf))
        return 0;
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

  for (int i = 0; i < m->dep_count; i++) {
    Dep *dep = &m->deps[i];
    if (dep->pkg_config[0]) {
      if (!ensure_pkgconfig_dep(dep, m))
        return 0;
    } else if (dep->is_local) {
      if (!ensure_local_dep(dep, m))
        return 0;
    } else if (dep->git[0]) {
      if (!ensure_git_dep(dep, m, &lf))
        return 0;
      dirty = 1;
    }
  }

  if (dirty)
    lockfile_save(&lf);
  return 1;
}
