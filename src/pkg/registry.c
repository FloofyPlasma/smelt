#include "pkg/registry.h"
#include "pkg/deps.h"
#include "tomlc17.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define MAX_CMD 65536

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

  for (int i = 0; i < m->registry_count; i++) {
    const char *base = m->registries[i];

    if (strncmp(base, "file://", 7) == 0) {
      char local[MAX_PATH];
      snprintf(local, sizeof(local), "%s/%s.toml", base + 7, name);
      if (file_exists(local)) {
        char cmd[MAX_CMD];
        snprintf(cmd, sizeof(cmd), "cp %s %s", local, recipe_path);
        if (system(cmd) == 0) {
          printf("smelt: found recipe for %s in %s\n", name, base);
          return 1;
        }
      }
      continue;
    }

    char url[MAX_STR * 2];
    snprintf(url, sizeof(url), "%s/%s.toml", base, name);

    char cmd[MAX_CMD];
    snprintf(cmd, sizeof(cmd), "curl -sf --max-time 10 -o %s %s 2>/dev/null",
             recipe_path, url);
    printf("smelt: fetching recipe %s\n", url);

    if (system(cmd) == 0 && file_exists(recipe_path)) {
      printf("smelt: found recipe for %s\n", name);
      return 1;
    }

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
    for (int i = 0; i < args.u.arr.size && i < 16; i++) {
      toml_datum_t a = args.u.arr.elem[i];
      if (a.type == TOML_STRING)
        snprintf(out->build_args[out->build_arg_count++],
                 sizeof(out->build_args[0]), "%s", a.u.s);
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
    for (int i = 0; i < copy.u.arr.size && i < MAX_DEP_FILES; i++) {
      toml_datum_t f = copy.u.arr.elem[i];
      if (f.type == TOML_STRING)
        snprintf(out->copy_files[out->copy_file_count++],
                 sizeof(out->copy_files[0]), "%s", f.u.s);
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
    return 0;
  }

  printf("smelt: installing %s @ %s\n", r.name, r.version);

  const char *files[MAX_DEP_FILES];
  for (int i = 0; i < r.copy_file_count; i++)
    files[i] = r.copy_files[i];

  return dep_add_git(r.git, files, r.copy_file_count);
}
