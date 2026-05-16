#include "cmd/build.h"
#include "cmd/clean.h"
#include "cmd/init.h"
#include "pkg/deps.h"
#include "pkg/git_deps.h"
#include "pkg/registry.h"
#include "project/compdb.h"
#include "project/manifest.h"
#include "ya_getopt.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char *help_string =
    "usage: smelt <command>\n"
    "commands: build, run, clean, init, add, update\n";

int main(int argc, char **argv) {
  if (argc < 2 || (argc >= 2 && strcmp(argv[1], "--help") == 0)) {
    fprintf(stderr, "%s", help_string);
    return 1;
  }

  git_deps_init();

  if (strcmp(argv[1], "build") == 0) {
    const char *profile = "debug";

    static const struct option build_longopts[] = {
        {"profile", required_argument, 0, 'p'},
        {0, 0, 0, 0},
    };

    int c, optindex;
    while ((c = getopt_long(argc, argv, "p:", build_longopts, &optindex)) !=
           -1) {
      switch (c) {
      case 'p':
        profile = optarg;
        break;
      default: /* ? */
        fprintf(stderr, "smelt: command-line error\n");
        return 1;
      }
    }

    // TODO(maelstrom): Check that the requested profile even exists

    Manifest m = {0};
    BuildCtx ctx = {0};
    if (!manifest_load("smelt.toml", &m))
      return 1;
    if (!deps_ensure(&m)) {
      manifest_free(&m);
      return 1;
    }
    if (!build_run(&m, &ctx, profile)) {
      manifest_free(&m);
      return 1;
    }
    compdb_write(&m, &ctx);
    buildctx_free(&ctx);
    manifest_free(&m);
    return 0;
  }

  if (strcmp(argv[1], "run") == 0) {
    const char *profile = "debug";

    static const struct option run_longopts[] = {
        {"profile", required_argument, 0, 'p'},
        {0, 0, 0, 0},
    };

    int c, optindex;
    while ((c = getopt_long(argc, argv, "p:", run_longopts, &optindex)) != -1) {
      switch (c) {
      case 'p':
        profile = optarg;
        break;
      default: /* ? */
        fprintf(stderr, "smelt: command-line error\n");
        return 1;
      }
    }

    // TODO(maelstrom): Check that the requested profile even exists

    Manifest m = {0};
    if (!manifest_load("smelt.toml", &m))
      return 1;
    if (!deps_ensure(&m)) {
      manifest_free(&m);
      return 1;
    }
    if (!build_run(&m, NULL, profile)) {
      manifest_free(&m);
      return 1;
    }

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

    manifest_free(&m);
    execv(output, child_args);
    perror("smelt: execv failed");
    return 1;
  }

  if (strcmp(argv[1], "clean") == 0) {
    Manifest m = {0};
    if (!manifest_load("smelt.toml", &m))
      return 1;
    int ok = clean_run(&m);

    manifest_free(&m);
    return ok ? 0 : 1;
  }

  if (strcmp(argv[1], "init") == 0) {
    return init_run() ? 0 : 1;
  }

  if (strcmp(argv[1], "add") == 0) {
    const char *target = NULL;
    const char *pkg_config_target = NULL;

    static const struct option add_longopts[] = {
        {"pkg-config", required_argument, 0, 0},
        {0, 0, 0, 0},
    };

    int c, optindex;
    while ((c = getopt_long(argc, argv, "", add_longopts, &optindex)) != -1) {
      switch (c) {
      case 0:
        switch (optindex) {
        case 0:
          pkg_config_target = optarg;
          break;
        }
        break;
      default: /* ? */
        fprintf(stderr, "smelt: command-line error\n");
        return 1;
      }
    }

    if (pkg_config_target != NULL) {
      return dep_add_pkgconfig(pkg_config_target) ? 0 : 1;
    }

    if (argc < 3) {
      fprintf(stderr, "usage: smelt add <git-url> [file1 file2 ...]\n");
      fprintf(stderr, "       smelt add <local/path>\n");
      fprintf(stderr, "       smelt add --pkg-config <package name>\n");
      return 1;
    }

    target = argv[2];

    if (target[0] == '.' || target[0] == '/') {
      return dep_add_local(target) ? 0 : 1;
    }

    if (strncmp(target, "http", 4) == 0) {
      if (argc < 4) {
        fprintf(stderr, "smelt: git dep requires at least one file\n");
        fprintf(stderr, "usage: smelt add <git-url> file1 [file2 ...]\n");
        return 1;
      }
      StringVec files = {0};
      for (int i = 3; i < argc; i++)
        stringvec_push(&files, argv[i]);
      int ok = dep_add_git(target, &files);
      stringvec_free(&files);
      return ok ? 0 : 1;
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
    int ok = deps_update(&m);

    manifest_free(&m);
    return ok ? 0 : 1;
  }

  fprintf(stderr, "smelt: unknown command: %s\n", argv[1]);
  fprintf(stderr, "%s", help_string);
  return 1;
}
