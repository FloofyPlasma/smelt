#define _POSIX_C_SOURCE 200809L
#include "cmd/build.h"
#include "cmd/ccflags.h"
#include "project/cache.h"
#include "project/manifest.h"
#include "xxhash.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAX_CMD 16384

static int ends_with_c(const char *name) {
  size_t len = strlen(name);
  return len > 2 && name[len - 2] == '.' && name[len - 1] == 'c';
}

static int scan_dir(BuildCtx *ctx, const char *dir);

static int scan_dir(BuildCtx *ctx, const char *dir) {
  DIR *d = opendir(dir);
  if (!d) {
    fprintf(stderr, "smelt: cannot open dir: %s\n", dir);
    return 0;
  }

  struct dirent *entry;
  while ((entry = readdir(d)) != NULL) {
    if (entry->d_name[0] == '.')
      continue;

    char path[MAX_PATH];
    snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);

    struct stat st;
    if (stat(path, &st) != 0)
      continue;

    if (S_ISDIR(st.st_mode)) {
      scan_dir(ctx, path);
    } else if (S_ISREG(st.st_mode) && ends_with_c(entry->d_name)) {
      if (ctx->source_count >= MAX_SOURCES) {
        fprintf(stderr, "smelt: too many source files\n");
        closedir(d);
        return 0;
      }
      snprintf(ctx->sources[ctx->source_count++], sizeof(ctx->sources[0]), "%s",
               path);
    }
  }

  closedir(d);
  return 1;
}

static void ensure_dir(const char *path) { mkdir(path, 0755); }

static int hash_source_and_deps(const char *src, const char *obj_dir,
                                const char *flags, char *out, size_t outsz) {
  XXH3_state_t *state = XXH3_createState();
  XXH3_64bits_reset(state);

  XXH3_64bits_update(state, flags, strlen(flags));

  // Hash source file
  FILE *fp = fopen(src, "rbe");
  if (!fp) {
    XXH3_freeState(state);
    return 0;
  }
  unsigned char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), fp)) > 0)
    XXH3_64bits_update(state, buf, n);
  fclose(fp);

  char dfile[MAX_PATH];
  const char *base = strrchr(src, '/');
  base = base ? base + 1 : src;
  snprintf(dfile, sizeof(dfile), "%s/%s", obj_dir, base);
  size_t dlen = strlen(dfile);
  if (dlen > 2)
    dfile[dlen - 1] = 'd';

  FILE *df = fopen(dfile, "re");
  if (df) {
    char line[4096];
    while (fgets(line, sizeof(line), df)) {
      char *p = line;
      while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\\' || *p == '\n')
          p++;
        if (!*p)
          break;
        char token[MAX_PATH];
        int ti = 0;
        while (*p && *p != ' ' && *p != '\t' && *p != '\\' && *p != '\n')
          token[ti++] = *p++;
        token[ti] = '\0';
        if (ti == 0 || token[ti - 1] == ':')
          continue;
        if (strcmp(token, src) == 0)
          continue;
        FILE *hf = fopen(token, "rbe");
        if (hf) {
          while ((n = fread(buf, 1, sizeof(buf), hf)) > 0)
            XXH3_64bits_update(state, buf, n);
          fclose(hf);
        }
      }
    }
    fclose(df);
  }

  XXH64_hash_t hash = XXH3_64bits_digest(state);
  XXH3_freeState(state);
  snprintf(out, outsz, "%016llx", (unsigned long long)hash);
  return 1;
}

int build_run(const Manifest *m, BuildCtx *ctx_out, const char *profile) {
  BuildCtx ctx = {0};

  // Find compiler
  const char *cc = getenv("CC");
  if (!cc || cc[0] == '\0')
    cc = "gcc";
  snprintf(ctx.compiler, sizeof(ctx.compiler), "%s", cc);

  // Gather sources
  if (!scan_dir(&ctx, m->src_dir))
    return 0;

  for (int i = 0; i < m->dep_source_count; i++) {
    if (ctx.source_count >= MAX_SOURCES) {
      fprintf(stderr, "smelt: too many sources\n");
      return 0;
    }
    snprintf(ctx.sources[ctx.source_count++], sizeof(ctx.sources[0]), "%s",
             m->dep_sources[i]);
  }

  if (ctx.source_count == 0) {
    fprintf(stderr, "smelt: no .c files found in %s\n", m->src_dir);
    return 0;
  }

  ensure_dir(m->out_dir);

  char cmdline[4096] = {0};
  ccflags_write_command_line(m, profile, cmdline, sizeof(cmdline), 0);

  printf("smelt: profile=%s\n", profile);

  char ver_cmd[MAX_PATH + 64];
  snprintf(ver_cmd, sizeof(ver_cmd), "%s --version 2>&1 | head -1",
           ctx.compiler);
  char compiler_ver[256] = {0};
  FILE *vp = popen(ver_cmd, "r");
  if (vp) {
    fgets(compiler_ver, sizeof(compiler_ver), vp);
    pclose(vp);
  }
  compiler_ver[strcspn(compiler_ver, "\n")] = '\0';

  BuildCache cache;
  cache_load(&cache);

  char flags_hash[MAX_HASH];
  cache_hash_str(cmdline, flags_hash, sizeof(flags_hash));
  int flags_changed = strcmp(flags_hash, cache.flags_hash) != 0 ||
                      strcmp(compiler_ver, cache.compiler_ver) != 0;
  if (flags_changed)
    printf("smelt: flags or compiler changed, full rebuild\n");

  char obj_files[MAX_SOURCES][MAX_OBJ_PATH];
  char new_hashes[MAX_SOURCES][MAX_HASH];
  int needs_compile[MAX_SOURCES];

  for (int i = 0; i < ctx.source_count; i++) {
    const char *src = ctx.sources[i];

    const char *base = strrchr(src, '/');
    base = base ? base + 1 : src;
    snprintf(obj_files[i], MAX_OBJ_PATH, "%s/%s", m->out_dir, base);
    size_t olen = strlen(obj_files[i]);
    if (olen > 2)
      obj_files[i][olen - 1] = 'o';

    new_hashes[i][0] = '\0';
    hash_source_and_deps(src, m->out_dir, cmdline, new_hashes[i], MAX_HASH);

    const char *old_hash = cache_get(&cache, src);
    needs_compile[i] =
        flags_changed || !old_hash || strcmp(old_hash, new_hashes[i]) != 0;
  }

  pid_t pids[MAX_SOURCES] = {0};
  int any_compiled = 0;

  for (int i = 0; i < ctx.source_count; i++) {
    if (!needs_compile[i]) {
      printf("smelt: skip %s (unchanged)\n", ctx.sources[i]);
      continue;
    }

    char cmd[MAX_CMD] = {0};
    snprintf(cmd, sizeof(cmd), "%s %s-MMD -c %s -o %s", ctx.compiler, cmdline,
             ctx.sources[i], obj_files[i]);
    printf("smelt: %s\n", cmd);

    pid_t pid = fork();
    if (pid < 0) {
      perror("smelt: fork");
      return 0;
    }
    if (pid == 0) {
      execl("/bin/sh", "sh", "-c", cmd, NULL);
      _exit(1);
    }
    pids[i] = pid;
    any_compiled = 1;
  }

  int build_ok = 1;
  for (int i = 0; i < ctx.source_count; i++) {
    if (pids[i] == 0)
      continue;
    int status;
    waitpid(pids[i], &status, 0);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
      fprintf(stderr, "smelt: compile failed: %s\n", ctx.sources[i]);
      build_ok = 0;
    }
  }
  if (!build_ok)
    return 0;

  for (int i = 0; i < ctx.source_count; i++) {
    if (!needs_compile[i])
      continue;
    char final_hash[MAX_HASH] = {0};
    hash_source_and_deps(ctx.sources[i], m->out_dir, cmdline, final_hash,
                         MAX_HASH);
    cache_set(&cache, ctx.sources[i], final_hash);
  }

  char output[MAX_PATH * 2];
  snprintf(output, sizeof(output), "%s/%s", m->out_dir, m->name);

  if (any_compiled || flags_changed) {
    char cmd[MAX_CMD] = {0};
    int pos = 0;
    pos += snprintf(cmd + pos, sizeof(cmd) - pos, "%s", ctx.compiler);

    for (int i = 0; i < ctx.source_count; i++)
      pos += snprintf(cmd + pos, sizeof(cmd) - pos, " %s", obj_files[i]);

    for (int i = 0; i < m->extra_count; i++)
      pos += snprintf(cmd + pos, sizeof(cmd) - pos, " %s", m->extra_sources[i]);

    for (int i = 0; i < m->link_flag_count; i++)
      pos += snprintf(cmd + pos, sizeof(cmd) - pos, " %s", m->link_flags[i]);

    pos += snprintf(cmd + pos, sizeof(cmd) - pos, " -o %s", output);
    printf("smelt: %s\n", cmd);

    if (system(cmd) != 0) {
      fprintf(stderr, "smelt: link failed\n");
      return 0;
    }
    printf("smelt: Build success!\n");
    printf("smelt: built %s\n", output);
  } else {
    printf("smelt: nothing to build\n");
  }

  snprintf(cache.flags_hash, sizeof(cache.flags_hash), "%s", flags_hash);
  snprintf(cache.compiler_ver, sizeof(cache.compiler_ver), "%s", compiler_ver);
  cache_save(&cache);

  if (ctx_out)
    *ctx_out = ctx;
  return 1;
}
