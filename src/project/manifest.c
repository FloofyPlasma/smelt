#include "project/manifest.h"
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
  for (int i = 0; i < arr.u.arr.size; i++) {
    toml_datum_t e = arr.u.arr.elem[i];
    if (e.type == TOML_STRING)
      stringvec_push(&out->flags, e.u.s);
  }
}

static void parse_str_array(toml_datum_t root, const char *key,
                            StringVec *out) {
  toml_datum_t arr = toml_seek(root, key);
  if (arr.type != TOML_ARRAY)
    return;
  for (int i = 0; i < arr.u.arr.size; i++) {
    toml_datum_t e = arr.u.arr.elem[i];
    if (e.type == TOML_STRING)
      stringvec_push(out, e.u.s);
  }
}

// TODO(FloofyPlasma): theres a lot of duplicated code here. clean up.
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

  parse_str_array(t, "build.include_dirs", &out->include_dirs);
  parse_str_array(t, "build.extra_sources", &out->extra_sources);
  parse_str_array(t, "build.link_flags", &out->link_flags);
  parse_str_array(t, "build.defines", &out->defines);

  if (out->src_dir[0] == '\0')
    scopy(out->src_dir, sizeof(out->src_dir), "src");
  if (out->out_dir[0] == '\0')
    scopy(out->out_dir, sizeof(out->out_dir), "build");

  parse_profile(t, "profile.debug.flags", &out->debug);
  parse_profile(t, "profile.release.flags", &out->release);

  if (stringvec_len(&out->debug.flags) == 0) {
    stringvec_grow(&out->debug.flags, 2);
    stringvec_push(&out->debug.flags, "-g");
    stringvec_push(&out->debug.flags, "-O0");
  }
  if (stringvec_len(&out->release.flags) == 0) {
    stringvec_grow(&out->release.flags, 2);
    stringvec_push(&out->release.flags, "-O3");
    stringvec_push(&out->release.flags, "-DNDEBUG");
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
            stringvec_push(&dep->files, f.u.s);
        }
      }

      toml_datum_t pkgcfg = toml_get(entry, "pkg-config");
      if (pkgcfg.type == TOML_STRING)
        snprintf(dep->pkg_config, sizeof(dep->pkg_config), "%s", pkgcfg.u.s);
    }
  }

  // [registries]
  toml_datum_t regs = toml_seek(t, "registries.sources");
  if (regs.type == TOML_ARRAY) {
    for (int i = 0; i < regs.u.arr.size && i < MAX_REGISTRIES; i++) {
      toml_datum_t e = regs.u.arr.elem[i];
      if (e.type == TOML_STRING)
        stringvec_push(&out->registries, e.u.s);
    }
  }

  if (stringvec_len(&out->registries) == 0) {
    stringvec_push(
        &out->registries,
        "https://raw.githubusercontent.com/floofyplasma/smelt-registry/main/"
        "recipes");
  }

  toml_free(result);
  return 1;
}

void manifest_free(Manifest *m) {
  for (int i = 0; i < m->dep_count; i++)
    stringvec_free(&m->deps[i].files);
  stringvec_free(&m->extra_sources);
  stringvec_free(&m->include_dirs);
  stringvec_free(&m->link_flags);
  stringvec_free(&m->defines);
  stringvec_free(&m->dep_sources);
  stringvec_free(&m->registries);
  stringvec_free(&m->debug.flags);
  stringvec_free(&m->release.flags);
}
