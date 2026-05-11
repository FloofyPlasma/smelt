#include "manifest.h"
#include "tomlc17.h"
#include <stdio.h>
#include <string.h>

static void scopy(char *dst, size_t dstsz, const char *src) {
  snprintf(dst, dstsz, "%s", src);
}

static void parse_profile(toml_datum_t root, const char *key, Profile *out) {
  toml_datum_t arr = toml_seek(root, key);
  if (arr.type != TOML_ARRAY)
    return;
  for (int i = 0; i < arr.u.arr.size && i < MAX_PROFILE_FLAGS; i++) {
    toml_datum_t e = arr.u.arr.elem[i];
    if (e.type == TOML_STRING)
      scopy(out->flags[out->flag_count++], sizeof(out->flags[0]), e.u.s);
  }
}

static void parse_str_array(toml_datum_t root, const char *key,
                            char dst[][MAX_PATH], int *count, int max) {
  toml_datum_t arr = toml_seek(root, key);
  if (arr.type != TOML_ARRAY)
    return;
  for (int i = 0; i < arr.u.arr.size && i < max; i++) {
    toml_datum_t e = arr.u.arr.elem[i];
    if (e.type == TOML_STRING)
      scopy(dst[(*count)++], MAX_PATH, e.u.s);
  }
}

int manifest_load(const char *path, Manifest *out) {
  *out = (Manifest){0};

  toml_result_t result = toml_parse_file_ex(path);
  if (!result.ok) {
    fprintf(stderr, "smelt: parse error: %s\n", result.errmsg);
    return 0;
  }

  toml_datum_t t = result.toptab;

  // [package]
  toml_datum_t name = toml_seek(t, "package.name");
  toml_datum_t ver = toml_seek(t, "package.version");
  if (name.type == TOML_STRING)
    scopy(out->name, sizeof(out->name), name.u.s);
  if (ver.type == TOML_STRING)
    scopy(out->version, sizeof(out->version), ver.u.s);

  // [build]
  toml_datum_t std = toml_seek(t, "build.c_standard");
  toml_datum_t warn = toml_seek(t, "build.warnings");
  toml_datum_t src = toml_seek(t, "build.src_dir");
  toml_datum_t odir = toml_seek(t, "build.out_dir");
  if (std.type == TOML_STRING)
    scopy(out->c_standard, sizeof(out->c_standard), std.u.s);
  if (warn.type == TOML_STRING)
    scopy(out->warnings, sizeof(out->warnings), warn.u.s);
  if (src.type == TOML_STRING)
    scopy(out->src_dir, sizeof(out->src_dir), src.u.s);
  if (odir.type == TOML_STRING)
    scopy(out->out_dir, sizeof(out->out_dir), odir.u.s);

  parse_str_array(t, "build.include_dirs", out->include_dirs,
                  &out->include_count, MAX_INCLUDES);
  parse_str_array(t, "build.extra_sources", out->extra_sources,
                  &out->extra_count, MAX_EXTRA);
  parse_str_array(t, "build.link_flags", out->link_flags, &out->link_flag_count,
                  MAX_LINK_FLAGS);
  parse_str_array(t, "build.defines", out->defines, &out->define_count,
                  MAX_DEFINES);

  if (out->src_dir[0] == '\0')
    scopy(out->src_dir, sizeof(out->src_dir), "src");
  if (out->out_dir[0] == '\0')
    scopy(out->out_dir, sizeof(out->out_dir), "build");

  parse_profile(t, "profile.debug.flags", &out->debug);
  parse_profile(t, "profile.release.flags", &out->release);

  if (out->debug.flag_count == 0) {
    scopy(out->debug.flags[0], MAX_STR, "-g");
    scopy(out->debug.flags[1], MAX_STR, "-O0");
    out->debug.flag_count = 2;
  }
  if (out->release.flag_count == 0) {
    scopy(out->release.flags[0], MAX_STR, "-O3");
    scopy(out->release.flags[1], MAX_STR, "-DNDEBUG");
    out->release.flag_count = 2;
  }

  // [dependencies]
  toml_datum_t deps_table = toml_seek(t, "dependencies");
  if (deps_table.type == TOML_TABLE) {
    int n = deps_table.u.tab.size;
    for (int i = 0; i < n && out->dep_count < MAX_DEPS; i++) {
      const char *depname = deps_table.u.tab.key[i];
      if (!depname)
        continue;

      Dep *dep = &out->deps[out->dep_count++];
      snprintf(dep->name, sizeof(dep->name), "%s", depname);

      toml_datum_t entry = toml_get(deps_table, depname);
      if (entry.type != TOML_TABLE)
        continue;

      toml_datum_t git = toml_get(entry, "git");
      if (git.type == TOML_STRING)
        snprintf(dep->git, sizeof(dep->git), "%s", git.u.s);

      toml_datum_t path = toml_get(entry, "path");
      if (path.type == TOML_STRING) {
        snprintf(dep->path, sizeof(dep->path), "%s", path.u.s);
        dep->is_local = 1;
      }

      toml_datum_t files = toml_get(entry, "files");
      if (files.type == TOML_ARRAY) {
        for (int j = 0; j < files.u.arr.size && j < MAX_DEP_FILES; j++) {
          toml_datum_t f = files.u.arr.elem[j];
          if (f.type == TOML_STRING)
            snprintf(dep->files[dep->file_count++], sizeof(dep->files[0]), "%s",
                     f.u.s);
        }
      }

      toml_datum_t pkgcfg = toml_get(entry, "pkg-config");
      if (pkgcfg.type == TOML_STRING)
        snprintf(dep->pkg_config, sizeof(dep->pkg_config), "%s", pkgcfg.u.s);
    }
  }

  toml_free(result);
  return 1;
}
