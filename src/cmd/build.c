#define _POSIX_C_SOURCE 200809L
#include "cmd/build.h"
#include "cmd/ccflags.h"
#include "core/compiler.h"
#include "core/fs.h"
#include "core/hash.h"
#include "core/process.h"
#include "pkg/artcache.h"
#include "pkg/cache.h"
#include "pkg/depbuild.h"
#include "pkg/resolve.h"
#include "project/build_cache.h"
#include "project/lock.h"
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

static int hash_source_and_deps(const char *src, const char *obj_dir,
                                const StringVec *flags, char *out,
                                size_t outsz) {
  XXH3_state_t *state = XXH3_createState();
  XXH3_64bits_reset(state);

  hash_update_vec(state, flags);

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
  const char *cxx = getenv("CXX");
  if (!cc || cc[0] == '\0')
    cc = "cc";
  if (!cxx || cxx[0] == '\0')
    cxx = "c++";
  snprintf(ctx.compiler, sizeof(ctx.compiler), "%s", cc);

  char compiler_ver[256] = {0};
  char compiler_hash[64] = {0};
  compiler_fingerprint(cc, compiler_ver, sizeof(compiler_ver), compiler_hash,
                       sizeof(compiler_hash));

  const char *home = getenv("HOME");
  if (!home || home[0] == '\0')
    home = "/tmp";

  char recipe_cache[MAX_PATH];
  snprintf(recipe_cache, sizeof(recipe_cache), "%s/.cache/smelt/recipes", home);

  ResolveCtx rctx = {
      .registry_url = stringvec_len(&m->registries) > 0
                          ? stringvec_get(&m->registries, 0)
                          : "https://raw.githubusercontent.com/floofyplasma/"
                            "smelt-registry/main/recipes",
      .cache_dir = recipe_cache,
  };

  ResolvedPackageVec pkgs = {0};
  if (!resolve_deps(m, &rctx, &pkgs)) {
    resolved_package_vec_free(&pkgs);
    return 0;
  }

  for (size_t i = 0; i < vec_len(&pkgs); i++) {
    ResolvedPackage *pkg = &pkgs.items[i];
    if (pkg->git[0] == '\0' || pkg->tag[0] == '\0')
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

  lockfile_write(&pkgs);

  DepBuildCtx dctx = {
      .cc = cc,
      .cxx = cxx,
      .compiler_hash = compiler_hash,
      .out_dir = m->out_dir,
      .profile = profile,
  };

  BuiltDepVec built_deps = {0};
  if (!depbuild_run(&pkgs, &dctx, &built_deps)) {
    resolved_package_vec_free(&pkgs);
    builtdep_vec_free(&built_deps);
    return 0;
  }

  resolved_package_vec_free(&pkgs);

  // Gather sources
  if (!scan_dir(&ctx, m->src_dir)) {
    builtdep_vec_free(&built_deps);
    return 0;
  }

  for (size_t i = 0; i < stringvec_len(&m->dep_sources); i++) {
    if (!stringvec_push(&ctx.sources, stringvec_get(&m->dep_sources, i))) {
      fprintf(stderr, "smelt: too many sources\n");
      builtdep_vec_free(&built_deps);
      return 0;
    }
  }

  if (stringvec_len(&ctx.sources) == 0) {
    fprintf(stderr, "smelt: no .c files found in %s\n", m->src_dir);
    builtdep_vec_free(&built_deps);
    return 0;
  }

  ensure_dirs(m->out_dir);

  StringVec flags = {0};
  if (!ccflags_build_vec(m, profile, &flags, 0)) {
    builtdep_vec_free(&built_deps);
    return 0;
  }

  for (size_t i = 0; i < vec_len(&built_deps); i++) {
    const BuiltDep *bd = &built_deps.items[i];
    for (size_t j = 0; j < stringvec_len(&bd->public_includes); j++) {
      char flag[MAX_PATH * 2];
      snprintf(flag, sizeof(flag), "-I%s",
               stringvec_get(&bd->public_includes, j));
      stringvec_push_unique(&flags, flag);
    }
  }

  printf("smelt: profile=%s\n", profile);

  BuildCache cache;
  cache_load(&cache);

  char flags_hash[MAX_HASH];
  cache_hash_vec(&flags, flags_hash, sizeof(flags_hash));
  int flags_changed = strcmp(flags_hash, cache.flags_hash) != 0 ||
                      strcmp(compiler_ver, cache.compiler_ver) != 0;
  if (flags_changed)
    printf("smelt: flags or compiler changed, full rebuild\n");

  StringVec obj_files = {0};
  stringvec_grow(&obj_files, ctx.sources.len);
  char new_hashes[MAX_SOURCES][MAX_HASH];
  int needs_compile[MAX_SOURCES];

  for (size_t i = 0; i < stringvec_len(&ctx.sources); i++) {
    const char *src = stringvec_get(&ctx.sources, i);
    const char *base = strrchr(src, '/');
    base = base ? base + 1 : src;

    char obj[MAX_OBJ_PATH];
    snprintf(obj, sizeof(obj), "%s/%s", m->out_dir, base);
    size_t olen = strlen(obj);
    if (olen > 2)
      obj[olen - 1] = 'o';
    stringvec_push(&obj_files, obj);

    new_hashes[i][0] = '\0';
    hash_source_and_deps(src, m->out_dir, &flags, new_hashes[i], MAX_HASH);

    const char *old_hash = cache_get(&cache, src);
    needs_compile[i] =
        flags_changed || !old_hash || strcmp(old_hash, new_hashes[i]) != 0;
  }

  int any_compiled = 0;

  for (size_t i = 0; i < stringvec_len(&ctx.sources); i++) {
    const char *src = stringvec_get(&ctx.sources, i);
    const char *obj = stringvec_get(&obj_files, i);

    if (!needs_compile[i]) {
      printf("smelt: skip %s (unchanged)\n", stringvec_get(&ctx.sources, i));
      continue;
    }

    Process proc = {0};
    process_argv_push(&proc, ctx.compiler);
    process_argv_extend(&proc, &flags);
    process_argv_push(&proc, "-MMD");
    process_argv_push(&proc, "-c");
    process_argv_push(&proc, src);
    process_argv_push(&proc, "-o");
    process_argv_push(&proc, obj);
    process_print(&proc);

    if (!process_run(&proc)) {
      fprintf(stderr, "smelt: compile failed: %s\n", src);
      process_free(&proc);
      stringvec_free(&flags);
      stringvec_free(&obj_files);
      builtdep_vec_free(&built_deps);
      return 0;
    }
    process_free(&proc);
    any_compiled = 1;
  }

  for (size_t i = 0; i < stringvec_len(&ctx.sources); i++) {
    if (!needs_compile[i])
      continue;
    char final_hash[MAX_HASH] = {0};
    hash_source_and_deps(stringvec_get(&ctx.sources, i), m->out_dir, &flags,
                         final_hash, MAX_HASH);
    cache_set(&cache, stringvec_get(&ctx.sources, i), final_hash);
  }

  char output[MAX_PATH * 2];
  snprintf(output, sizeof(output), "%s/%s", m->out_dir, m->name);

  if (any_compiled || flags_changed) {
    Process proc = {0};
    process_argv_push(&proc, ctx.compiler);

    process_argv_extend(&proc, &obj_files);
    process_argv_extend(&proc, &m->extra_sources);

    for (size_t i = 0; i < vec_len(&built_deps); i++) {
      const BuiltDep *bd = &built_deps.items[i];
      for (size_t j = 0; j < stringvec_len(&bd->link_flags); j++)
        process_argv_push(&proc, stringvec_get(&bd->link_flags, j));
    }

    process_argv_extend(&proc, &m->link_flags);

    process_argv_push(&proc, "-o");
    process_argv_push(&proc, output);
    process_print(&proc);

    if (!process_run(&proc)) {
      fprintf(stderr, "smelt: link failed\n");
      process_free(&proc);
      stringvec_free(&flags);
      stringvec_free(&obj_files);
      builtdep_vec_free(&built_deps);
      return 0;
    }

    process_free(&proc);

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

  stringvec_free(&flags);
  stringvec_free(&obj_files);
  builtdep_vec_free(&built_deps);
  return 1;
}

void buildctx_free(BuildCtx *ctx) { stringvec_free(&ctx->sources); }
