#define _POSIX_C_SOURCE 200809L

#include "pkg/resolve.h"
#include "core/fs.h"
#include "core/process.h"
#include "core/semver.h"
#include "core/stringvec.h"
#include "core/vec.h"
#include "pkg/recipe.h"
#include "project/manifest.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MAX_DEPTH 64

typedef struct {
  const char *names[MAX_DEPTH];
  int len;
} VisitStack;

static int visit_push(VisitStack *s, const char *name) {
  if (s->len >= MAX_DEPTH) {
    fprintf(stderr, "smelt: dependency graph too deep (max %d)\n", MAX_DEPTH);
    return 0;
  }
  s->names[s->len++] = name;
  return 1;
}

static void visit_pop(VisitStack *s) {
  if (s->len > 0)
    s->len--;
}

static int visit_contains(const VisitStack *s, const char *name) {
  for (int i = 0; i < s->len; i++)
    if (strcmp(s->names[i], name) == 0)
      return 1;
  return 0;
}

static void visit_print_cycle(const VisitStack *s, const char *name) {
  fprintf(stderr, "smelt: cycle detected\n  ");
  for (int i = 0; i < s->len; i++)
    fprintf(stderr, "%s -> ", s->names[i]);
  fprintf(stderr, "%s\n", name);
}

static int file_exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

int fetch_recipe(const char *name, const char *version, const ResolveCtx *ctx,
                 char *recipe_path_out, size_t path_sz) {
  snprintf(recipe_path_out, path_sz, "%s/%s@%s.toml", ctx->cache_dir, name,
           version);

  if (file_exists(recipe_path_out)) {
    return 1;
  }

  ensure_dirs(ctx->cache_dir);
  fprintf(stderr, "cache_dir=%s\n", ctx->cache_dir);

  if (!ctx->registry_url || ctx->registry_url[0] == '\0') {
    fprintf(stderr, "smelt: no registry configured\n");
    return 0;
  }

  char url[2048];

  if (strncmp(ctx->registry_url, "file://", 7) == 0) {
    snprintf(url, sizeof(url), "%s/%s@%s.toml", ctx->registry_url + 7, name,
             version);
    if (!file_exists(url)) {
      fprintf(stderr,
              "smelt: no recipe found for %s@%s\n\n"
              "  options:\n"
              "    create a local recipe:  smelt add --recipe ./%.s.toml\n"
              "    contribute a recipe:    "
              "https://github.com/floofyplasma/smelt-registry\n",
              name, version, name);
      return 0;
    }
    FILE *src = fopen(url, "re");
    FILE *dst = fopen(recipe_path_out, "we");
    if (!src || !dst) {
      if (src)
        fclose(src);
      if (dst)
        fclose(dst);
      return 0;
    }
    char buf[4096];
    size_t n;
    while ((n = fread(buf, 1, sizeof(buf), src)) > 0)
      fwrite(buf, 1, n, dst);
    fclose(src);
    fclose(dst);
    return 1;
  }

  snprintf(url, sizeof(url), "%s/%s@%s.toml", ctx->registry_url, name, version);

  Process curl = {0};

  process_argv_push(&curl, "curl");
  process_argv_push(&curl, "-s");
  process_argv_push(&curl, "--max-time");
  process_argv_push(&curl, "10");
  process_argv_push(&curl, "-o");
  process_argv_push(&curl, recipe_path_out);
  process_argv_push(&curl, url);

  process_print(&curl);

  printf("smelt: fetching recipe %s@%s\n", name, version);

  if (!process_run(&curl) || !file_exists(recipe_path_out)) {
    remove(recipe_path_out);
    fprintf(stderr,
            "smelt: no recipe found for %s@%s\n\n"
            "  options:\n"
            "    create a local recipe:  smelt add --recipe ./%s.toml\n"
            "    contribute a recipe:    "
            "https://github.com/floofyplasma/smelt-registry\n",
            name, version, name);
    process_free(&curl);
    return 0;
  }

  process_free(&curl);

  return 1;
}

static ResolvedPackage *find_resolved(ResolvedPackageVec *out,
                                      const char *name) {
  for (size_t i = 0; i < vec_len(out); i++) {
    if (strcmp(vec_get(out, i).name, name) == 0)
      return &out->items[i];
  }
  return NULL;
}

static int check_conflict(const ResolvedPackage *existing, const char *name,
                          const char *version, const char *required_by) {
  if (strcmp(existing->version, version) == 0)
    return 1;

  fprintf(stderr,
          "smelt: version conflict\n"
          "  %s %s required by: %s\n"
          "  %s %s required by: %s\n\n"
          "smelt supports one global version per package.\n"
          "Pin both to the same version in smelt.toml.\n",
          name, existing->version,
          existing->direct ? "your project" : existing->required_by, name,
          version, required_by[0] ? required_by : "your project");
  return 0;
}

static int resolve_one(const char *name, const char *version,
                       const StringVec *features, int direct,
                       const char *required_by, const ResolveCtx *ctx,
                       ResolvedPackageVec *out, VisitStack *stack);

static int resolve_feature_requires(const Recipe *recipe,
                                    const StringVec *enabled_features,
                                    const char *parent_name,
                                    const ResolveCtx *ctx,
                                    ResolvedPackageVec *out,
                                    VisitStack *stack) {
  if (!enabled_features)
    return 1;

  for (size_t fi = 0; fi < stringvec_len(enabled_features); fi++) {
    const char *feat_name = stringvec_get(enabled_features, fi);

    for (size_t ri = 0; ri < vec_len(&recipe->features); ri++) {
      const RecipeFeature *rf = &recipe->features.items[ri];
      if (strcmp(rf->name, feat_name) != 0)
        continue;

      for (size_t rj = 0; rj < stringvec_len(&rf->requires); rj++) {
        const char *req_name = stringvec_get(&rf->requires, rj);

        char req_recipe_path[MAX_PATH * 2];
        char ver_buf[MAX_STR] = {0};

        fprintf(stderr,
                "smelt: feature '%s' of '%s' requires '%s' - "
                "version not yet resolvable from feature requires; "
                "add '%s' as an explicit dependency in smelt.toml\n",
                feat_name, parent_name, req_name, req_name);
        (void)req_recipe_path;
        (void)ver_buf;
        (void)ctx;
        (void)out;
        (void)stack;
        return 0;
      }
      break;
    }
  }
  return 1;
}

static int resolve_one(const char *name, const char *version,
                       const StringVec *features, int direct,
                       const char *required_by, const ResolveCtx *ctx,
                       ResolvedPackageVec *out, VisitStack *stack) {
  if (visit_contains(stack, name)) {
    visit_print_cycle(stack, name);
    return 0;
  }

  Semver sv;
  if (!semver_parse(version, &sv)) {
    fprintf(stderr, "smelt: invalid version '%s' for package '%s'\n", version,
            name);
    return 0;
  }

  ResolvedPackage *existing = find_resolved(out, name);
  if (existing) {
    if (!check_conflict(existing, name, version, required_by))
      return 0;
    if (features) {
      for (size_t i = 0; i < stringvec_len(features); i++)
        stringvec_push_unique(&existing->features, stringvec_get(features, i));
    }
    return 1;
  }

  char recipe_path[MAX_PATH * 2];
  if (!fetch_recipe(name, version, ctx, recipe_path, sizeof(recipe_path)))
    return 0;

  Recipe recipe = {0};
  if (!recipe_load(recipe_path, &recipe)) {
    fprintf(stderr, "smelt: failed to load recipe for %s@%s\n", name, version);
    return 0;
  }

  ResolvedPackage pkg = {0};
  snprintf(pkg.name, sizeof(pkg.name), "%s", name);
  snprintf(pkg.version, sizeof(pkg.version), "%s", version);
  snprintf(pkg.git, sizeof(pkg.git), "%s", recipe.git);
  snprintf(pkg.tag, sizeof(pkg.tag), "%s", recipe.tag);
  pkg.direct = direct;
  if (required_by && required_by[0])
    snprintf(pkg.required_by, sizeof(pkg.required_by), "%s", required_by);
  if (features) {
    for (size_t i = 0; i < stringvec_len(features); i++)
      stringvec_push(&pkg.features, stringvec_get(features, i));
  }

  if (!vec_push(out, pkg)) {
    recipe_free(&recipe);
    fprintf(stderr, "smelt: out of memory\n");
    return 0;
  }

  if (!visit_push(stack, out->items[out->len - 1].name)) {
    recipe_free(&recipe);
    return 0;
  }

  for (size_t i = 0; i < stringvec_len(&recipe.dep_names); i++) {
    const char *dep_name = stringvec_get(&recipe.dep_names, i);
    const char *dep_ver = stringvec_get(&recipe.dep_versions, i);
    if (!resolve_one(dep_name, dep_ver, NULL, 0, name, ctx, out, stack)) {
      visit_pop(stack);
      recipe_free(&recipe);
      return 0;
    }
  }

  if (!resolve_feature_requires(&recipe, features, name, ctx, out, stack)) {
    visit_pop(stack);
    recipe_free(&recipe);
    return 0;
  }

  visit_pop(stack);
  recipe_free(&recipe);
  return 1;
}

int resolve_deps(const Manifest *m, const ResolveCtx *ctx,
                 ResolvedPackageVec *out) {
  *out = (ResolvedPackageVec){0};

  VisitStack stack = {0};

  for (size_t i = 0; i < vec_len(&m->deps); i++) {
    const Dep *dep = &m->deps.items[i];

    if (dep->kind == DEP_LOCAL || dep->kind == DEP_PKG_CONFIG ||
        dep->kind == DEP_SYSTEM)
      continue;

    if (!resolve_one(dep->name, dep->version, &dep->features, 1, "", ctx, out,
                     &stack)) {
      resolved_package_vec_free(out);
      return 0;
    }
  }

  return 1;
}

void resolved_package_vec_free(ResolvedPackageVec *v) {
  for (size_t i = 0; i < vec_len(v); i++) {
    stringvec_free(&v->items[i].features);
  }
  vec_free(v);
}
