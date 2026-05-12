#include "cmd/build.h"
#include "cmd/clean.h"
#include "cmd/init.h"
#include "pkg/deps.h"
#include "pkg/registry.h"
#include "project/compdb.h"
#include "project/manifest.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: smelt <command>\n");
    fprintf(stderr, "commands: build, run, clean, init, add, update\n");
    return 1;
  }

  if (strcmp(argv[1], "build") == 0) {
    const char *profile = "debug";
    if (argc >= 3 && strcmp(argv[2], "release") == 0)
      profile = argv[2];
    Manifest m = {0};
    BuildCtx ctx = {0};
    if (!manifest_load("smelt.toml", &m))
      return 1;
    if (!deps_ensure(&m))
      return 1;
    if (!build_run(&m, &ctx, profile))
      return 1;
    compdb_write(&m, &ctx);
    return 0;
  }

  if (strcmp(argv[1], "run") == 0) {
    const char *profile = "debug";
    if (argc >= 3 && strcmp(argv[2], "release") == 0)
      profile = argv[2];
    Manifest m = {0};
    if (!manifest_load("smelt.toml", &m))
      return 1;
    if (!deps_ensure(&m))
      return 1;
    if (!build_run(&m, NULL, profile))
      return 1;

    char output[MAX_PATH * 2];
    snprintf(output, sizeof(output), "%s/%s", m.out_dir, m.name);

    char *child_args[256] = {output};
    int child_argc = 1;
    for (int i = 2; i < argc; i++) {
      if (strcmp(argv[i], "--") == 0)
        continue;
      child_args[child_argc++] = argv[i];
    }
    child_args[child_argc] = NULL;

    execv(output, child_args);
    perror("smelt: execv failed");
    return 1;
  }

  if (strcmp(argv[1], "clean") == 0) {
    Manifest m = {0};
    if (!manifest_load("smelt.toml", &m))
      return 1;
    return clean_run(&m) ? 0 : 1;
  }

  if (strcmp(argv[1], "init") == 0) {
    return init_run() ? 0 : 1;
  }

  if (strcmp(argv[1], "add") == 0) {
    if (argc < 3) {
      fprintf(stderr, "usage: smelt add <git-url> [file1 file2 ...]\n");
      fprintf(stderr, "       smelt add <local/path>\n");
      return 1;
    }

    const char *target = argv[2];

    if (target[0] == '.' || target[0] == '/') {
      return dep_add_local(target) ? 0 : 1;
    }

    if (argc >= 4 && strcmp(argv[2], "--pkg-config") == 0) {
      return dep_add_pkgconfig(argv[3]) ? 0 : 1;
    }

    if (strncmp(target, "http", 4) == 0) {
      if (argc < 4) {
        fprintf(stderr, "smept: git dep requires at least one file\n");
        fprintf(stderr, "usage: smelt add <git-url> file1 [file2 ...]\n");
        return 1;
      }
      const char **files = (const char **)&argv[3];
      int file_count = argc - 3;
      return dep_add_git(target, files, file_count) ? 0 : 1;
    }

    Manifest m = {0};
    if (!manifest_load("smelt.toml", &m))
      return 1;
    return registry_add(&m, target) ? 0 : 1;
  }

  if (strcmp(argv[1], "update") == 0) {
    Manifest m = {0};
    if (!manifest_load("smelt.toml", &m))
      return 1;
    return deps_update(&m) ? 0 : 1;
  }

  fprintf(stderr, "smelt: unknown command: %s\n", argv[1]);
  return 1;
}
