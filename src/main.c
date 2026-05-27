#include "cmd/build.h"
#include "cmd/clean.h"
#include "cmd/init.h"
#include "core/semver.h"
#include "pkg/cache.h"
#include "pkg/deps.h"
#include "pkg/git_deps.h"
#include "pkg/recipe.h"
#include "pkg/resolve.h"
#include "project/compdb.h"
#include "project/lock.h"
#include "project/manifest.h"
#include "ya_getopt.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static const char *help_string =
    "usage: smelt <command>\n"
    "commands: build, run, clean, init, add, update\n";

static ResolveCtx make_resolve_ctx(const Manifest *m, char *cache_dir,
                                   size_t cache_sz) {
  const char *home = getenv("HOME");
  if (!home || home[0] == '\0')
    home = "/tmp";
  snprintf(cache_dir, cache_sz, "%s/.cache/smelt/recipes", home);

  return (ResolveCtx){
      .registry_url = stringvec_len(&m->registries) > 0
                          ? stringvec_get(&m->registries, 0)
                          : "https://raw.githubusercontent.com/floofyplasma/"
                            "smelt-registry/main/recipes",
      .cache_dir = cache_dir,
  };
}

static int resolve_fetch_and_lock(const Manifest *m) {
  char cache_dir[MAX_PATH];
  ResolveCtx ctx = make_resolve_ctx(m, cache_dir, sizeof(cache_dir));

  ResolvedPackageVec pkgs = {0};
  if (!resolve_deps(m, &ctx, &pkgs)) {
    resolved_package_vec_free(&pkgs);
    return 0;
  }

  for (size_t i = 0; i < vec_len(&pkgs); i++) {
    ResolvedPackage *pkg = &pkgs.items[i];
    if (!pkg->git[0] || !pkg->tag[0])
      continue;

    char commit[64] = {0};
    char worktree[MAX_PATH * 2] = {0};
    if (!pkg_cache_ensure(pkg->name, pkg->git, pkg->tag, commit, sizeof(commit),
                          worktree, sizeof(worktree))) {
      resolved_package_vec_free(&pkgs);
      return 0;
    }
    snprintf(pkg->commit, sizeof(pkg->commit), "%s", commit);
  }

  if (!lockfile_write(&pkgs)) {
    resolved_package_vec_free(&pkgs);
    return 0;
  }

  printf("smelt: lockfile updated (%zu packages)\n", vec_len(&pkgs));
  resolved_package_vec_free(&pkgs);
  return 1;
}

static int add_local_recipe(Manifest *m, const char *recipe_path,
                            const char *cache_dir) {
  Recipe recipe = {0};
  if (!recipe_load(recipe_path, &recipe)) {
    fprintf(stderr, "smelt: failed to parse recipe: %s\n", recipe_path);
    return 0;
  }

  if (!recipe.name[0] || !recipe.version[0]) {
    fprintf(stderr, "smelt: recipe missing [package] name/version: %s\n",
            recipe_path);
    recipe_free(&recipe);
    return 0;
  }

  char dst[MAX_PATH * 2];
  snprintf(dst, sizeof(dst), "%s/%s@%s.toml", cache_dir, recipe.name,
           recipe.version);

  char tmp[MAX_PATH * 2];
  snprintf(tmp, sizeof(tmp), "%s", cache_dir);
  for (char *p = tmp + 1; *p; p++) {
    if (*p == '/') {
      *p = '\0';
      mkdir(tmp, 0755);
      *p = '/';
    }
  }
  mkdir(tmp, 0755);

  FILE *src_fp = fopen(recipe_path, "re");
  if (!src_fp) {
    perror(recipe_path);
    recipe_free(&recipe);
    return 0;
  }
  FILE *dst_fp = fopen(dst, "we");
  if (!dst_fp) {
    perror(dst);
    fclose(src_fp);
    recipe_free(&recipe);
    return 0;
  }
  char buf[4096];
  size_t n;
  while ((n = fread(buf, 1, sizeof(buf), src_fp)) > 0)
    fwrite(buf, 1, n, dst_fp);
  fclose(src_fp);
  fclose(dst_fp);

  printf("smelt: cached local recipe %s@%s -> %s\n", recipe.name,
         recipe.version, dst);

  if (!manifest_add_dep(m, recipe.name, recipe.version)) {
    fprintf(stderr, "smelt: %s already in manifest\n", recipe.name);
    recipe_free(&recipe);
    return 0;
  }

  if (!manifest_save("smelt.toml", m)) {
    recipe_free(&recipe);
    return 0;
  }

  printf("smelt: added %s@%s to smelt.toml\n", recipe.name, recipe.version);
  recipe_free(&recipe);
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
    int ok = resolve_fetch_and_lock(&m);
    manifest_free(&m);
    return ok ? 0 : 1;
  }

  if (strcmp(argv[1], "add") == 0) {
    if (argc < 4) {
      fprintf(stderr, "usage: smelt add <name> <version>\n");
      fprintf(stderr, "       smelt add --pkg-config <package>\n");
      fprintf(stderr, "       smelt add --recipe <path/to/foo.toml>\n");
      return 1;
    }

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

    if (strcmp(argv[2], "--recipe") == 0) {
      const char *recipe_path = argv[3];

      Manifest m = {0};
      if (!manifest_load("smelt.toml", &m))
        return 1;

      char cache_dir[MAX_PATH];
      ResolveCtx ctx = make_resolve_ctx(&m, cache_dir, sizeof(cache_dir));

      if (!add_local_recipe(&m, recipe_path, ctx.cache_dir)) {
        manifest_free(&m);
        return 1;
      }

      int ok = resolve_fetch_and_lock(&m);
      manifest_free(&m);
      return ok ? 0 : 1;
    }

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

    char cache_dir[MAX_PATH];
    ResolveCtx ctx = make_resolve_ctx(&m, cache_dir, sizeof(cache_dir));

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

    int ok = resolve_fetch_and_lock(&m);
    manifest_free(&m);
    return ok ? 0 : 1;
  }

  fprintf(stderr, "smelt: unknown command: %s\n", argv[1]);
  fprintf(stderr, "%s", help_string);
  return 1;
}
