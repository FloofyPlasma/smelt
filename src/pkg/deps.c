#define _POSIX_C_SOURCE 200809L
#include "pkg/deps.h"
#include "project/lock.h"
#include "project/manifest.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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

static int copy_file(const char *src, const char *dst) {
  FILE *in = fopen(src, "rb");
  if (!in) {
    perror(src);
    return 0;
  }
  FILE *out = fopen(dst, "wb");
  if (!out) {
    perror(dst);
    fclose(in);
    return 0;
  }
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), in)) > 0)
    fwrite(buf, 1, n, out);
  fclose(in);
  fclose(out);
  return 1;
}

static const char *base_name(const char *path) {
  const char *s = strrchr(path, '/');
  return s ? s + 1 : path;
}

static int ends_with_c(const char *name) {
  size_t len = strlen(name);
  return len > 2 && name[len - 2] == '.' && name[len - 1] == 'c';
}

static int toml_write_dep(const char *entry) {
  FILE *fp = fopen("smelt.toml", "r");
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

  fp = fopen("smelt.toml", "r");
  if (!fp)
    return 0;
  char content[65536] = {0};
  fread(content, 1, sizeof(content) - 1, fp);
  fclose(fp);

  fp = fopen("smelt.toml", "w");
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
    char cmd[MAX_CMD];
    snprintf(cmd, sizeof(cmd), "git clone --depth=1 %s %s", url, cache_out);
    printf("smelt: %s\n", cmd);
    if (system(cmd) != 0) {
      fprintf(stderr, "smelt: git clone failed\n");
      return 0;
    }
  } else {
    printf("smelt: using cached %s\n", cache_out);
  }

  if (use_lock && lf) {
    const char *pinned = lockfile_get_commit(lf, name);
    if (pinned) {
      char cmd[MAX_CMD];
      snprintf(cmd, sizeof(cmd),
               "git -C %s fetch --depth=1 origin %s 2>/dev/null", cache_out,
               pinned);
      system(cmd);
      snprintf(cmd, sizeof(cmd), "git -C %s checkout FETCH_HEAD 2>/dev/null",
               cache_out);
      system(cmd);
      printf("smelt: pinned %s @ %s\n", name, pinned);
    }
  }

  char commit[64] = {0};
  char cmd[MAX_CMD];
  snprintf(cmd, sizeof(cmd), "git -C %s rev-parse HEAD 2>/dev/null", cache_out);
  FILE *fp = popen(cmd, "r");
  if (fp) {
    fgets(commit, sizeof(commit), fp);
    pclose(fp);
    commit[strcspn(commit, "\n")] = '\0';
  }

  if (lf && commit[0])
    lockfile_set(lf, name, url, commit);

  return 1;
}

int dep_add_git(const char *url, const char **files, int file_count) {
  LockFile lf;
  lockfile_load(&lf);

  char cache[MAX_PATH];
  if (!fetch_git(url, cache, sizeof(cache), &lf, 1))
    return 0;

  mkdir("vendor", 0755);

  char files_str[4096] = {0};
  int fpos = 0;
  fpos += snprintf(files_str + fpos, sizeof(files_str) - fpos, "[");
  for (int i = 0; i < file_count; i++) {
    fpos += snprintf(files_str + fpos, sizeof(files_str) - fpos, "\"%s\"%s",
                     files[i], i < file_count - 1 ? ", " : "");
  }
  snprintf(files_str + fpos, sizeof(files_str) - fpos, "]");

  for (int i = 0; i < file_count; i++) {
    char src[MAX_PATH * 2];
    snprintf(src, sizeof(src), "%s/%s", cache, files[i]);
    char dst[MAX_PATH];
    snprintf(dst, sizeof(dst), "vendor/%s", base_name(files[i]));
    if (!copy_file(src, dst))
      return 0;
    printf("smelt: copied %s -> %s\n", files[i], dst);
  }

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
  char cmd[MAX_CMD];
  snprintf(cmd, sizeof(cmd), "pkg-config --exists %s 2>/dev/null", name);
  if (system(cmd) != 0) {
    fprintf(stderr, "smelt: pkg-config: %s not found\n", name);
    return 0;
  }
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
  for (int i = 0; i < dep->file_count; i++) {
    char src[MAX_PATH * 2];
    snprintf(src, sizeof(src), "%s/%s", cache, dep->files[i]);
    const char *fname = base_name(dep->files[i]);
    char dst[MAX_PATH];
    snprintf(dst, sizeof(dst), "vendor/%s", fname);

    if (!file_exists(dst)) {
      if (!copy_file(src, dst))
        return 0;
      printf("smelt: vendored %s\n", dst);
    }

    if (ends_with_c(fname) && m->extra_count < MAX_EXTRA) {
      int found = 0;
      for (int j = 0; j < m->extra_count; j++)
        if (strcmp(m->extra_sources[j], dst) == 0) {
          found = 1;
          break;
        }
      if (!found)
        snprintf(m->extra_sources[m->extra_count++],
                 sizeof(m->extra_sources[0]), "%s", dst);
    }
    vendor_added = 1;
  }

  if (vendor_added && m->include_count < MAX_INCLUDES) {
    int found = 0;
    for (int i = 0; i < m->include_count; i++)
      if (strcmp(m->include_dirs[i], "vendor") == 0) {
        found = 1;
        break;
      }
    if (!found)
      snprintf(m->include_dirs[m->include_count++], sizeof(m->include_dirs[0]),
               "vendor");
  }

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

  if (m->include_count < MAX_INCLUDES) {
    int found = 0;
    for (int i = 0; i < m->include_count; i++)
      if (strcmp(m->include_dirs[i], dep_src) == 0) {
        found = 1;
        break;
      }
    if (!found)
      snprintf(m->include_dirs[m->include_count++], sizeof(m->include_dirs[0]),
               "%s", dep_src);
  }

  for (int i = 0; i < dm.include_count; i++) {
    char inc[MAX_PATH];
    snprintf(inc, sizeof(inc), "%s/%s", dep->path, dm.include_dirs[i]);
    int found = 0;
    for (int j = 0; j < m->include_count; j++)
      if (strcmp(m->include_dirs[j], inc) == 0) {
        found = 1;
        break;
      }
    if (!found && m->include_count < MAX_INCLUDES)
      snprintf(m->include_dirs[m->include_count++], sizeof(m->include_dirs[0]),
               "%s", inc);
  }

  char cmd[MAX_CMD];
  snprintf(cmd, sizeof(cmd), "find %s -name '*.c' 2>/dev/null", dep_src);
  FILE *fp = popen(cmd, "r");
  if (fp) {
    char line[MAX_PATH];
    while (fgets(line, sizeof(line), fp)) {
      line[strcspn(line, "\n")] = '\0';
      if (line[0] == '\0')
        continue;
      int found = 0;
      for (int i = 0; i < m->dep_source_count; i++)
        if (strcmp(m->dep_sources[i], line) == 0) {
          found = 1;
          break;
        }
      if (!found && m->dep_source_count < MAX_DEP_SOURCES)
        snprintf(m->dep_sources[m->dep_source_count++],
                 sizeof(m->dep_sources[0]), "%s", line);
    }
    pclose(fp);
  }
  return 1;
}

static int ensure_pkgconfig_dep(Dep *dep, Manifest *m) {
  if (system("pkg-config --version > /dev/null 2>&1") != 0) {
    fprintf(stderr, "smelt: pkg-config not found\n");
    return 0;
  }

  char cmd[MAX_CMD];
  snprintf(cmd, sizeof(cmd), "pkg-config --exists %s 2>/dev/null",
           dep->pkg_config);
  if (system(cmd) != 0) {
    fprintf(stderr, "smelt: pkg-config: %s not found\n", dep->pkg_config);
    fprintf(stderr, "smelt: try installing %s with your system package manager",
            dep->pkg_config);
    return 0;
  }

  snprintf(cmd, sizeof(cmd), "pkg-config --cflags %s 2>/dev/null",
           dep->pkg_config);
  FILE *fp = popen(cmd, "r");
  if (fp) {
    char cflags[4096] = {0};
    fgets(cflags, sizeof(cflags), fp);
    pclose(fp);
    cflags[strcspn(cflags, "\n")] = '\0';

    char *tok = strtok(cflags, " ");
    while (tok) {
      if (strncmp(tok, "-I", 2) == 0) {
        if (m->include_count < MAX_INCLUDES) {
          int found = 0;
          for (int i = 0; i < m->include_count; i++)
            if (strcmp(m->include_dirs[i], tok + 2) == 0) {
              found = 1;
              break;
            }
          if (!found)
            snprintf(m->include_dirs[m->include_count++],
                     sizeof(m->include_dirs[0]), "%s", tok + 2);
        }
      } else if (strncmp(tok, "-D", 2) == 0) {
        if (m->define_count < MAX_DEFINES) {
          int found = 0;
          for (int i = 0; i < m->define_count; i++)
            if (strcmp(m->defines[i], tok + 2) == 0) {
              found = 1;
              break;
            }
          if (!found)
            snprintf(m->defines[m->define_count++], sizeof(m->defines[0]), "%s",
                     tok + 2);
        }
      }
      tok = strtok(NULL, " ");
    }
  }

  snprintf(cmd, sizeof(cmd), "pkg-config --libs %s 2>/dev/null",
           dep->pkg_config);
  fp = popen(cmd, "r");
  if (fp) {
    char libs[4096] = {0};
    fgets(libs, sizeof(libs), fp);
    pclose(fp);
    libs[strcspn(libs, "\n")] = '\0';

    char *tok = strtok(libs, " ");
    while (tok) {
      if (m->link_flag_count < MAX_LINK_FLAGS) {
        int found = 0;
        for (int i = 0; i < m->link_flag_count; i++)
          if (strcmp(m->link_flags[i], tok) == 0) {
            found = 1;
            break;
          }
        if (!found)
          snprintf(m->link_flags[m->link_flag_count++],
                   sizeof(m->link_flags[0]), "%s", tok);
      }
      tok = strtok(NULL, " ");
    }
  }

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

      char cmd[MAX_CMD];
      char name[MAX_NAME];
      repo_name(name, sizeof(name), dep->git);
      char cache_dir[MAX_PATH];
      cache_path(cache_dir, sizeof(cache_dir), name);

      if (dir_exists(cache_dir)) {
        snprintf(cmd, sizeof(cmd),
                 "git -C %s fetch --depth=1 origin HEAD 2>/dev/null && git -C "
                 "%s reset --hard FETCH_HEAD 2>/dev/null",
                 cache_dir, cache_dir);
        printf("smelt: updating %s\n", name);
        system(cmd);
        snprintf(cache, sizeof(cache), "%s", cache_dir);
      }

      char commit[64] = {0};
      snprintf(cmd, sizeof(cmd), "git -C %s rev-parse HEAD 2>/dev/null",
               cache_dir);
      FILE *fp = popen(cmd, "r");
      if (fp) {
        fgets(commit, sizeof(commit), fp);
        pclose(fp);
        commit[strcspn(commit, "\n")] = '\0';
      }

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
