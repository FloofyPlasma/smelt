#define _POSIX_C_SOURCE 200809L

#include "pkg/depbuild.h"
#include "core/fs.h"
#include "core/process.h"
#include "core/stringvec.h"
#include "core/vec.h"
#include "pkg/artcache.h"
#include "pkg/cache.h"
#include "pkg/recipe.h"
#include "pkg/resolve.h"
#include "project/manifest.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

static int ends_with(const char *s, const char *suffix) {
  size_t slen = strlen(s);
  size_t suflen = strlen(suffix);
  if (suflen > slen)
    return 0;
  return strcmp(s + slen - suflen, suffix) == 0;
}

static int is_c_source(const char *f) { return ends_with(f, ".c"); }
static int is_cpp_source(const char *f) {
  return ends_with(f, ".C") || ends_with(f, ".cpp") || ends_with(f, ".cc") ||
         ends_with(f, ".cxx");
}

static int collect_sources(const Recipe *r, const StringVec *features,
                           const char *worktree, StringVec *out) {
  for (size_t i = 0; i < stringvec_len(&r->files.base); i++) {
    const char *f = stringvec_get(&r->files.base, i);
    if (!is_c_source(f) && !is_cpp_source(f))
      continue;
    char path[MAX_PATH * 2];
    snprintf(path, sizeof(path), "%s/%s", worktree, f);
    if (!stringvec_push(out, path))
      return 0;
  }

  for (size_t i = 0; i < stringvec_len(&r->files.platform); i++) {
    const char *f = stringvec_get(&r->files.platform, i);
    if (!is_c_source(f) && !is_cpp_source(f))
      continue;
    char path[MAX_PATH * 2];
    snprintf(path, sizeof(path), "%s/%s", worktree, f);
    if (!stringvec_push(out, path))
      return 0;
  }

  if (features) {
    for (size_t fi = 0; fi < stringvec_len(features); fi++) {
      const char *feat_name = stringvec_get(features, fi);
      for (size_t ri = 0; ri < vec_len(&r->features); ri++) {
        const RecipeFeature *rf = &r->features.items[ri];
        if (strcmp(rf->name, feat_name) != 0)
          continue;
        for (size_t fj = 0; fj < stringvec_len(&rf->files); fj++) {
          const char *f = stringvec_get(&rf->files, fj);
          if (!is_c_source(f) && !is_cpp_source(f))
            continue;
          char path[MAX_PATH * 2];
          snprintf(path, sizeof(path), "%s/%s", worktree, f);
          if (!stringvec_push(out, path))
            return 0;
        }
        break;
      }
    }
  }

  return 1;
}

static int collect_public_includes(const Recipe *r, const char *worktree,
                                   StringVec *out) {
  for (size_t i = 0; i < stringvec_len(&r->includes.public_includes); i++) {
    char path[MAX_PATH * 2];
    snprintf(path, sizeof(path), "%s/%s", worktree,
             stringvec_get(&r->includes.public_includes, i));
    if (!stringvec_push(out, path))
      return 0;
  }
  return 1;
}

static int compile_source(const char *src, const char *art_dir,
                          const StringVec *includes, const char *cc,
                          const char *cxx) {
  const char *compiler = is_cpp_source(src) ? cxx : cc;

  const char *base = strchr(src, '/');
  base = base ? base + 1 : src;

  char obj[MAX_PATH * 2];
  snprintf(obj, sizeof(obj), "%s/%s.o", art_dir, base);

  Process proc = {0};
  process_argv_push(&proc, compiler);

  for (size_t i = 0; i < stringvec_len(includes); i++) {
    char flag[MAX_PATH * 2];
    snprintf(flag, sizeof(flag), "-I%s", stringvec_get(includes, i));
    process_argv_push(&proc, flag);
  }

  process_argv_push(&proc, "-c");
  process_argv_push(&proc, src);
  process_argv_push(&proc, "-o");
  process_argv_push(&proc, obj);

  process_print(&proc);
  int ok = process_run(&proc);
  process_free(&proc);

  if (!ok)
    fprintf(stderr, "smelt: compile failed: %s\n", src);
  return ok;
}

static int archive_objects(const char *art_dir, const StringVec *sources) {
  char lib[MAX_PATH * 2];
  artifact_lib_path(lib, sizeof(lib), art_dir);

  Process proc = {0};
  process_argv_push(&proc, "ar");
  process_argv_push(&proc, "rcs");
  process_argv_push(&proc, lib);

  for (size_t i = 0; i < stringvec_len(sources); i++) {
    const char *src = stringvec_get(sources, i);
    const char *base = strrchr(src, '/');
    base = base ? base + 1 : src;

    char obj[MAX_PATH * 2];
    snprintf(obj, sizeof(obj), "%s/%s.o", art_dir, base);
    process_argv_push(&proc, obj);
  }

  process_print(&proc);
  int ok = process_run(&proc);
  process_free(&proc);

  if (!ok)
    fprintf(stderr, "smelt: ar failed for %s\n", art_dir);
  return ok;
}

static int build_one(const ResolvedPackage *pkg, const DepBuildCtx *ctx,
                     const BuiltDepVec *already_built,
                     const char *recipe_cache_dir, BuiltDep *out) {
  *out = (BuiltDep){0};
  snprintf(out->name, sizeof(out->name), "%s", pkg->name);

  char commit[64] = {0};
  char worktree[MAX_PATH * 2] = {0};
  if (!pkg_cache_ensure(pkg->name, pkg->git, pkg->tag, commit, sizeof(commit),
                        worktree, sizeof(worktree)))
    return 0;

  char art_dir[MAX_PATH * 2];
  artifact_dir(art_dir, sizeof(art_dir), ctx->out_dir, ctx->profile, pkg->name,
               commit, ctx->compiler_hash);

  char recipe_path[MAX_PATH * 2];
  recipe_cache_path(recipe_path, sizeof(recipe_path), pkg->name, pkg->version);
  Recipe recipe = {0};
  if (!recipe_load(recipe_path, &recipe)) {
    fprintf(stderr, "smelt: failed to load recipe for %s\n", pkg->name);
    return 0;
  }

  if (!collect_public_includes(&recipe, worktree, &out->public_includes)) {
    recipe_free(&recipe);
    return 0;
  }

  for (size_t i = 0; i < stringvec_len(&recipe.link_flags); i++) {
    if (!stringvec_push(&out->link_flags,
                        stringvec_get(&recipe.link_flags, i))) {
      recipe_free(&recipe);
      return 0;
    }
  }

  if (artifact_valid(art_dir)) {
    printf("smelt: dep %s up to date\n", pkg->name);
    char lib[MAX_PATH * 2];
    artifact_lib_path(lib, sizeof(lib), art_dir);
    stringvec_push(&out->link_flags, lib);
    recipe_free(&recipe);
    return 1;
  }

  StringVec sources = {0};
  if (!collect_sources(&recipe, &pkg->features, worktree, &sources)) {
    recipe_free(&recipe);
    stringvec_free(&sources);
    return 0;
  }

  if (stringvec_len(&sources) == 0) {
    printf("smelt: dep %s header-only, skipping compile\n", pkg->name);
    recipe_free(&recipe);
    stringvec_free(&sources);
    return 1;
  }

  StringVec includes = {0};

  for (size_t i = 0; i < stringvec_len(&recipe.includes.private_includes);
       i++) {
    char path[MAX_PATH * 2];
    snprintf(path, sizeof(path), "%s/%s", worktree,
             stringvec_get(&recipe.includes.private_includes, i));
    stringvec_push_unique(&includes, path);
  }

  for (size_t i = 0; i < stringvec_len(&out->public_includes); i++)
    stringvec_push_unique(&includes, stringvec_get(&out->public_includes, i));

  for (size_t i = 0; i < vec_len(already_built); i++) {
    const BuiltDep *bd = &already_built->items[i];
    for (size_t j = 0; j < stringvec_len(&bd->public_includes); j++)
      stringvec_push_unique(&includes, stringvec_get(&bd->public_includes, j));
  }

  ensure_dirs(art_dir);

  printf("smelt: building dep %s\n", pkg->name);
  for (size_t i = 0; i < stringvec_len(&sources); i++) {
    if (!compile_source(stringvec_get(&sources, i), art_dir, &includes, ctx->cc,
                        ctx->cxx)) {
      stringvec_free(&includes);
      stringvec_free(&sources);
      recipe_free(&recipe);
      return 0;
    }
  }

  if (!archive_objects(art_dir, &sources)) {
    stringvec_free(&includes);
    stringvec_free(&sources);
    recipe_free(&recipe);
    return 0;
  }

  char lib[MAX_PATH * 2];
  artifact_lib_path(lib, sizeof(lib), art_dir);
  stringvec_push(&out->link_flags, lib);

  printf("smelt: built dep %s\n", pkg->name);

  stringvec_free(&includes);
  stringvec_free(&sources);
  recipe_free(&recipe);
  return 1;
}

int depbuild_run(const ResolvedPackageVec *pkgs, const DepBuildCtx *ctx,
                 BuiltDepVec *out) {
  *out = (BuiltDepVec){0};

  char recipe_cache[MAX_PATH];
  const char *home = getenv("HOME");
  if (!home || home[0] == '\0')
    home = "/tmp";
  snprintf(recipe_cache, sizeof(recipe_cache), "%s/.cache/smelt/recipes", home);

  for (size_t i = 0; i < vec_len(pkgs); i++) {
    const ResolvedPackage *pkg = &pkgs->items[i];

    BuiltDep bd = {0};
    if (!build_one(pkg, ctx, out, recipe_cache, &bd)) {
      builtdep_vec_free(out);
      return 0;
    }

    if (!vec_push(out, bd)) {
      stringvec_free(&bd.public_includes);
      stringvec_free(&bd.link_flags);
      builtdep_vec_free(out);
      return 0;
    }
  }

  return 1;
}

void builtdep_vec_free(BuiltDepVec *v) {
  for (size_t i = 0; i < vec_len(v); i++) {
    stringvec_free(&v->items[i].public_includes);
    stringvec_free(&v->items[i].link_flags);
  }
  vec_free(v);
}

const BuiltDep *builtdep_find(const BuiltDepVec *v, const char *name) {
  for (size_t i = 0; i < vec_len(v); i++)
    if (strcmp(v->items[i].name, name) == 0)
      return &v->items[i];
  return NULL;
}
