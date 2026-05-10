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

static int scan_sources(BuildCtx *ctx, const char *src_dir) {
  DIR *d = opendir(src_dir);
  if (!d) {
    fprintf(stderr, "smelt: cannot open src dir: %s\n", src_dir);
    return 0;
  }

  struct dirent *entry;
  while ((entry = readdir(d)) != NULL) {
    if (!ends_with_c(entry->d_name))
      continue;
    if (ctx->source_count >= MAX_SOURCES) {
      fprintf(stderr, "smelt: too many source files\n");
      closedir(d);
      return 0;
    }
    path_join(ctx->sources[ctx->source_count], sizeof(ctx->sources[0]), src_dir,
              entry->d_name);
    ctx->source_count++;
  }

  closedir(d);
  return 1;
}

static void ensure_dir(const char *path) { mkdir(path, 0755); }

int build_run(const Manifest *m) {
  BuildCtx ctx = {0};

  // get compiler
  const char *cc = getenv("CC");
  if (!cc || cc[0] == '\0')
    cc = "gcc";
  snprintf(ctx.compiler, sizeof(ctx.compiler), "%s", cc);

  // scan sources
  if (!scan_sources(&ctx, m->src_dir))
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

  pos += snprintf(cmd + pos, sizeof(cmd) - pos, " -o %s", output);

  printf("smelt: %s\n", cmd);

  int ret = system(cmd);
  if (ret != 0) {
    fprintf(stderr, "smelt: build failed\n");
    return 0;
  }

  printf("smelt: built %s\n", output);
  return 1;
}
