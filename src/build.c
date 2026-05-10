#include "build.h"
#include "manifest.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#define MAX_CMD 8192

static void path_join(char *dst, size_t dstsz, const char *a, const char *b) {
  snprintf(dst, dstsz, "%s/%s", a, b);
}

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
      continue; // skip . .. .git etc

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

int build_run(const Manifest *m, BuildCtx *ctx_cout) {
  BuildCtx ctx = {0};

  // get compiler
  const char *cc = getenv("CC");
  if (!cc || cc[0] == '\0')
    cc = "gcc";
  snprintf(ctx.compiler, sizeof(ctx.compiler), "%s", cc);

  // scan sources
  if (!scan_dir(&ctx, m->src_dir))
    return 0;
  if (ctx.source_count == 0) {
    fprintf(stderr, "smelt: no .c files found in %s\n", m->src_dir);
    return 0;
  }

  ensure_dir(m->out_dir);

  char output[MAX_PATH];
  path_join(output, sizeof(output), m->out_dir, m->name);

  char cmd[MAX_CMD] = {0};
  int pos = 0;

  pos += snprintf(cmd + pos, sizeof(cmd) - pos, "%s", ctx.compiler);

  if (m->c_standard[0])
    pos += snprintf(cmd + pos, sizeof(cmd) - pos, " -std=%s", m->c_standard);

  if (strcmp(m->warnings, "all") == 0)
    pos += snprintf(cmd + pos, sizeof(cmd) - pos, " -Wall -Wextra");

  for (int i = 0; i < ctx.source_count; i++)
    pos += snprintf(cmd + pos, sizeof(cmd) - pos, " %s", ctx.sources[i]);

  // extra sources
  for (int i = 0; i < m->extra_count; i++)
    pos += snprintf(cmd + pos, sizeof(cmd) - pos, " %s", m->extra_sources[i]);

  // include dirs :3
  for (int i = 0; i < m->include_count; i++)
    pos += snprintf(cmd + pos, sizeof(cmd) - pos, " -I%s", m->include_dirs[i]);

  pos += snprintf(cmd + pos, sizeof(cmd) - pos, " -o %s", output);

  printf("smelt: %s\n", cmd);

  int ret = system(cmd);
  if (ret != 0) {
    fprintf(stderr, "smelt: build failed\n");
    return 0;
  }

  if (ctx_cout)
    *ctx_cout = ctx;
  printf("smelt: built %s\n", output);
  return 1;
}
