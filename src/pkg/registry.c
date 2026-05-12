#include "pkg/registry.h"
#include "core/process.h"
#include "pkg/deps.h"
#include "tomlc17.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

// FIXME: duplicated across files
static void ensure_dirs(const char *path) {
  char tmp[MAX_PATH];
  snprintf(tmp, sizeof(tmp), "%s", path);
  for (char *p = tmp + 1; *p; p++) {
    if (*p == '/') {
      *p = '\0';
      mkdir(tmp, 0755);
      *p = '/';
    }
  }
  mkdir(tmp, 0755);
}

static int file_exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

void registry_recipe_path(char *dst, size_t dstsz, const char *name) {
  const char *home = getenv("HOME");
  if (!home || home[0] == '\0')
    home = "/tmp";
  snprintf(dst, dstsz, "%s/.cache/smelt/recipes/%s.toml", home, name);
}

int registry_fetch_recipe(const Manifest *m, const char *name) {
  char recipe_path[MAX_PATH];
  registry_recipe_path(recipe_path, sizeof(recipe_path), name);

  if (file_exists(recipe_path)) {
    printf("smelt: using cached recipe for %s\n", name);
    return 1;
  }

  char dir[MAX_PATH];
  const char *home = getenv("HOME");
  if (!home || home[0] == '\0')
    home = "/tmp";
  snprintf(dir, sizeof(dir), "%s/.cache/smelt/recipes", home);
  ensure_dirs(dir);

  for (size_t i = 0; i < stringvec_len(&m->registries); i++) {
    const char *base = stringvec_get(&m->registries, i);

    if (strncmp(base, "file://", 7) == 0) {
      char local[MAX_PATH];
      snprintf(local, sizeof(local), "%s/%s.toml", base + 7, name);
      if (file_exists(local)) {
        Process proc = {0};

        process_argv_push(&proc, "cp");
        process_argv_push(&proc, local);
        process_argv_push(&proc, recipe_path);
        if (process_run(&proc)) {
          printf("smelt: found recipe for %s in %s\n", name, base);
          process_free(&proc);
          return 1;
        }
        process_free(&proc);
      }
      continue;
    }

    char url[MAX_STR * 2];
    snprintf(url, sizeof(url), "%s/%s.toml", base, name);

    // TODO(FloofyPlasma): libcurl instead?
    Process proc = {0};

    process_argv_push(&proc, "curl");
    process_argv_push(&proc, "-sf");
    process_argv_push(&proc, "--max-time");
    process_argv_push(&proc, "10");
    process_argv_push(&proc, "-o");
    process_argv_push(&proc, recipe_path);
    process_argv_push(&proc, url);
    printf("smelt: fetching recipe %s\n", url);

    if (process_run(&proc) && file_exists(recipe_path)) {
      printf("smelt: found recipe for %s\n", name);
      process_free(&proc);
      return 1;
    }
    process_free(&proc);

    remove(recipe_path);
  }

  fprintf(stderr, "smelt: no recipe found for %s\n", name);
  fprintf(stderr, "smelt: try: smelt add <git-url> <files...>\n");
  return 0;
}

int recipe_load(const char *path, Recipe *out) {
  *out = (Recipe){0};

  toml_result_t result = toml_parse_file_ex(path);
  if (!result.ok) {
    fprintf(stderr, "smelt: recipe parse error: %s\n", result.errmsg);
    return 0;
  }

  toml_datum_t t = result.toptab;

  toml_datum_t name = toml_seek(t, "package.name");
  toml_datum_t ver = toml_seek(t, "package.version");
  if (name.type == TOML_STRING)
    snprintf(out->name, sizeof(out->name), "%s", name.u.s);
  if (ver.type == TOML_STRING)
    snprintf(out->version, sizeof(out->version), "%s", ver.u.s);

  toml_datum_t git = toml_seek(t, "source.git");
  toml_datum_t tag = toml_seek(t, "source.tag");
  if (git.type == TOML_STRING)
    snprintf(out->git, sizeof(out->git), "%s", git.u.s);
  if (tag.type == TOML_STRING)
    snprintf(out->tag, sizeof(out->tag), "%s", tag.u.s);

  toml_datum_t sys = toml_seek(t, "build.system");
  toml_datum_t args = toml_seek(t, "build.args");
  if (sys.type == TOML_STRING)
    snprintf(out->build_system, sizeof(out->build_system), "%s", sys.u.s);
  if (args.type == TOML_ARRAY) {
    for (int i = 0; i < args.u.arr.size; i++) {
      toml_datum_t a = args.u.arr.elem[i];
      if (a.type == TOML_STRING)
        stringvec_push(&out->build_args, a.u.s);
    }
  }

  toml_datum_t lib = toml_seek(t, "output.lib");
  toml_datum_t inc = toml_seek(t, "output.include");
  if (lib.type == TOML_STRING)
    snprintf(out->output_lib, sizeof(out->output_lib), "%s", lib.u.s);
  if (inc.type == TOML_STRING)
    snprintf(out->output_include, sizeof(out->output_include), "%s", inc.u.s);

  toml_datum_t copy = toml_seek(t, "files.copy");
  if (copy.type == TOML_ARRAY) {
    for (int i = 0; i < copy.u.arr.size; i++) {
      toml_datum_t f = copy.u.arr.elem[i];
      if (f.type == TOML_STRING)
        stringvec_push(&out->copy_files, f.u.s);
    }
  }

  toml_free(result);
  return 1;
}

int registry_add(Manifest *m, const char *name) {
  if (!registry_fetch_recipe(m, name))
    return 0;

  char recipe_path[MAX_PATH];
  registry_recipe_path(recipe_path, sizeof(recipe_path), name);

  Recipe r = {0};
  if (!recipe_load(recipe_path, &r))
    return 0;

  if (r.build_system[0] && strcmp(r.build_system, "none") != 0) {
    fprintf(stderr, "smelt: build system '%s' not yet supported\n",
            r.build_system);
    recipe_free(&r);
    return 0;
  }

  printf("smelt: installing %s @ %s\n", r.name, r.version);
  int ok = dep_add_git(r.git, &r.copy_files);

  recipe_free(&r);
  return ok;
}

void recipe_free(Recipe *r) {
  stringvec_free(&r->build_args);
  stringvec_free(&r->copy_files);
}
