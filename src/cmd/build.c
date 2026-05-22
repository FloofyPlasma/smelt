#define _POSIX_C_SOURCE 200809L
#include "cmd/build.h"
#include "cmd/ccflags.h"
#include "core/fs.h"
#include "core/hash.h"
#include "core/process.h"
#include "project/build_cache.h"
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
  if (!cc || cc[0] == '\0')
    cc = "gcc";
  snprintf(ctx.compiler, sizeof(ctx.compiler), "%s", cc);

  // Gather sources
  if (!scan_dir(&ctx, m->src_dir))
    return 0;

  for (size_t i = 0; i < stringvec_len(&m->dep_sources); i++) {
    if (!stringvec_push(&ctx.sources, stringvec_get(&m->dep_sources, i))) {
      fprintf(stderr, "smelt: too many sources\n");
      return 0;
    }
  }

  if (stringvec_len(&ctx.sources) == 0) {
    fprintf(stderr, "smelt: no .c files found in %s\n", m->src_dir);
    return 0;
  }

  ensure_dirs(m->out_dir);

  StringVec flags = {0};

  if (!ccflags_build_vec(m, profile, &flags, 0))
    return 0;

  printf("smelt: profile=%s\n", profile);

  Process ver_proc = {0};

  process_argv_push(&ver_proc, ctx.compiler);
  process_argv_push(&ver_proc, "--version");

  char compiler_ver[256] = {0};

  if (process_capture(&ver_proc, compiler_ver, sizeof(compiler_ver))) {
    compiler_ver[strcspn(compiler_ver, "\n")] = '\0';
  }

  process_free(&ver_proc);

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
    process_argv_extend(&proc, &m->link_flags);

    process_argv_push(&proc, "-o");
    process_argv_push(&proc, output);

    process_print(&proc);

    if (!process_run(&proc)) {
      fprintf(stderr, "smelt: link failed\n");
      process_free(&proc);
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
  return 1;
}

void buildctx_free(BuildCtx *ctx) { stringvec_free(&ctx->sources); }
