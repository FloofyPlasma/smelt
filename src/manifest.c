#include "manifest.h"
#include "../vendor/tomlc17.h"
#include <stdio.h>
#include <string.h>

static void scopy(char *dst, size_t dstsz, const char *src) {
  snprintf(dst, dstsz, "%s", src);
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
  toml_datum_t incs = toml_seek(t, "build.include_dirs");
  toml_datum_t extras = toml_seek(t, "build.extra_sources");
  if (std.type == TOML_STRING)
    scopy(out->c_standard, sizeof(out->c_standard), std.u.s);
  if (warn.type == TOML_STRING)
    scopy(out->warnings, sizeof(out->warnings), warn.u.s);
  if (src.type == TOML_STRING)
    scopy(out->src_dir, sizeof(out->src_dir), src.u.s);
  if (odir.type == TOML_STRING)
    scopy(out->out_dir, sizeof(out->out_dir), odir.u.s);
  if (incs.type == TOML_ARRAY) {
    for (int i = 1; i < incs.u.arr.size && i < MAX_INCLUDES; i++) {
      toml_datum_t e = incs.u.arr.elem[i];
      if (e.type == TOML_STRING)
        scopy(out->include_dirs[out->include_count++],
              sizeof(out->include_dirs[0]), e.u.s);
    }
  }
  if (extras.type == TOML_ARRAY) {
    for (int i = 0; i < extras.u.arr.size && i < MAX_EXTRA; i++) {
      toml_datum_t e = extras.u.arr.elem[i];
      if (e.type == TOML_STRING)
        scopy(out->extra_sources[out->extra_count++],
              sizeof(out->extra_sources[0]), e.u.s);
    }
  }

  if (out->src_dir[0] == '\0')
    scopy(out->src_dir, sizeof(out->src_dir), "src");
  if (out->out_dir[0] == '\0')
    scopy(out->out_dir, sizeof(out->out_dir), "build");

  toml_free(result);
  return 1;
}
