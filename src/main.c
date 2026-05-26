#include "cmd/build.h"
#include "cmd/clean.h"
#include "cmd/init.h"
#include "core/semver.h"
#include "pkg/deps.h"
#include "pkg/git_deps.h"
#include "pkg/registry.h"
#include "pkg/resolve.h"
#include "project/compdb.h"
#include "project/lock.h"
#include "project/manifest.h"
#include "ya_getopt.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static const char *help_string =
    "usage: smelt <command>\n"
    "commands: build, run, clean, init, add, update\n";

static int resolve_and_lock(const Manifest *m) {
  const char *home = getenv("HOME");
  if (!home || home[0] == '\0')
    home = "/tmp";

  char cache_dir[MAX_PATH];
  snprintf(cache_dir, sizeof(cache_dir), "%s/.cache/smelt/recipes", home);

  ResolveCtx ctx = {
      .registry_url = stringvec_len(&m->registries) > 0
                          ? stringvec_get(&m->registries, 0)
                          : "https://raw.githubusercontent.com/floofyplasma/"
                            "smelt-registry/main/recipes",
      .cache_dir = cache_dir,
  };

  ResolvedPackageVec pkgs = {0};
  if (!resolve_deps(m, &ctx, &pkgs)) {
    resolved_package_vec_free(&pkgs);
    return 0;
  }

  if (!lockfile_write(&pkgs)) {
    resolved_package_vec_free(&pkgs);
    return 0;
  }

  printf("smelt: lockfile updated (%zu packages)\n", vec_len(&pkgs));
  resolved_package_vec_free(&pkgs);
  return 1;
}

int main(int argc, char **argv) {
  if (argc < 2 || (argc >= 2 && strcmp(argv[1], "--help") == 0)) {
    fprintf(stderr, "%s", help_string);
    return 1;
  }

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
    git_deps_init();
    if (!deps_ensure(&m)) {
      manifest_free(&m);
      git_deps_free();
      return 1;
    }
    if (!build_run(&m, &ctx, profile)) {
      manifest_free(&m);
      git_deps_free();
      return 1;
    }
    compdb_write(&m, &ctx);
    buildctx_free(&ctx);
    manifest_free(&m);
    git_deps_free();
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
    git_deps_init();
    if (!deps_ensure(&m)) {
      manifest_free(&m);
      git_deps_free();
      return 1;
    }
    if (!build_run(&m, NULL, profile)) {
      manifest_free(&m);
      git_deps_free();
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
    git_deps_free();
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

  if (strcmp(argv[1], "update") == 0) {
    Manifest m = {0};
    if (!manifest_load("smelt.toml", &m))
      return 1;

    printf("smelt: re-resolving dependency graph from scratch\n");
    int ok = resolve_and_lock(&m);
    manifest_free(&m);
    return ok ? 0 : 1;
  }

  if (strcmp(argv[1], "add") == 0) {
    // smelt add <name> <version>
    if (argc < 4) {
      fprintf(stderr, "usage: smelt add <name> <version>\n");
      fprintf(stderr, "       smelt add --pkg-config <package>\n");
      return 1;
    }

    // --pkg-config <name>
    if (strcmp(argv[2], "--pkg-config") == 0) {
      if (argc < 4) {
        fprintf(stderr, "usage: smelt add --pkg-config <package>\n");
        return 1;
      }

      const char *pkg = argv[3];
      if (!dep_add_pkgconfig(pkg))
        return 1;

      Manifest m = {0};
      if (!manifest_load("smelt.toml", &m))
        return 1;

      int found = 0;
      for (size_t i = 0; i < vec_len(&m.deps); i++)
        if (strcmp(m.deps.items[i].name, pkg) == 0) {
          found = 1;
          break;
        }

      if (!found) {
        Dep dep = {0};
        dep.kind = DEP_PKG_CONFIG;
        snprintf(dep.name, sizeof(dep.name), "%s", pkg);
        snprintf(dep.pkg.pkg_config, sizeof(dep.pkg.pkg_config), "%s", pkg);
        if (!vec_push(&m.deps, dep)) {
          manifest_free(&m);
          return 1;
        }
        if (!manifest_save("smelt.toml", &m)) {
          manifest_free(&m);
          return 1;
        }
        printf("smelt: added system dep %s\n", pkg);
      } else {
        printf("smelt: %s already in manifest\n", pkg);
      }

      manifest_free(&m);
      return 0;
    }

    // smelt add <name> <version>
    const char *dep_name = argv[2];
    const char *dep_ver = argv[3];

    Semver sv;
    if (!semver_parse(dep_ver, &sv)) {
      fprintf(stderr,
              "smelt: invalid version '%s' - expected MAJOR.MINOR.PATCH\n",
              dep_ver);
      return 1;
    }

    Manifest m = {0};
    if (!manifest_load("smelt.toml", &m))
      return 1;

    const char *home = getenv("HOME");
    if (!home || home[0] == '\0')
      home = "/tmp";
    char cache_dir[MAX_PATH];
    snprintf(cache_dir, sizeof(cache_dir), "%s/.cache/smelt/recipes", home);

    const char *registry_url = stringvec_len(&m.registries) > 0
                                   ? stringvec_get(&m.registries, 0)
                                   : "https://raw.githubusercontent.com/"
                                     "floofyplasma/smelt-registry/main/recipes";

    ResolveCtx ctx = {.registry_url = registry_url, .cache_dir = cache_dir};

    char recipe_path[MAX_PATH * 2];
    if (!fetch_recipe(dep_name, dep_ver, &ctx, recipe_path,
                      sizeof(recipe_path))) {
      manifest_free(&m);
      return 1;
    }

    if (!manifest_add_dep(&m, dep_name, dep_ver)) {
      fprintf(stderr, "smelt: %s already in manifest\n", dep_name);
      manifest_free(&m);
      return 1;
    }

    if (!manifest_save("smelt.toml", &m)) {
      manifest_free(&m);
      return 1;
    }

    printf("smelt: added %s@%s to smelt.toml\n", dep_name, dep_ver);

    if (!resolve_and_lock(&m)) {
      manifest_free(&m);
      return 1;
    }

    manifest_free(&m);
    return 0;
  }

  fprintf(stderr, "smelt: unknown command: %s\n", argv[1]);
  fprintf(stderr, "%s", help_string);
  return 1;
}
