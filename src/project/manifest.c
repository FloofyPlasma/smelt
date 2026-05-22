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

static int parse_dep_entry(const char *depname, toml_datum_t entry,
                           DepVec *deps) {
  Dep dep = {0};
  scopy(dep.name, sizeof(dep.name), depname);

  if (entry.type == TOML_STRING) {
    dep.kind = DEP_GIT;
    scopy(dep.version, sizeof(dep.version), entry.u.s);
  } else if (entry.type == TOML_TABLE) {
    toml_datum_t ver = toml_get(entry, "version");
    if (ver.type == TOML_STRING) {
      dep.kind = DEP_GIT;
      scopy(dep.version, sizeof(dep.version), ver.u.s);

      toml_datum_t feats = toml_get(entry, "features");
      if (feats.type == TOML_ARRAY) {
        for (int j = 0; j < feats.u.arr.size; j++) {
          toml_datum_t f = feats.u.arr.elem[j];
          if (f.type == TOML_STRING)
            stringvec_push(&dep.features, f.u.s);
        }
      }
    } else {
      toml_datum_t path = toml_get(entry, "path");
      if (path.type == TOML_STRING) {
        dep.kind = DEP_LOCAL;
        scopy(dep.local.path, sizeof(dep.local.path), path.u.s);
      }
    }
  } else {
    return 1;
  }

  if (!vec_push(deps, dep)) {
    stringvec_free(&dep.features);
    return 0;
  }
  return 1;
}

static int parse_system_dep_entry(const char *depname, toml_datum_t entry,
                                  DepVec *deps) {
  if (entry.type != TOML_TABLE)
    return 1;

  Dep dep = {0};
  scopy(dep.name, sizeof(dep.name), depname);

  toml_datum_t pkgcfg = toml_get(entry, "pkg-config");
  toml_datum_t link = toml_get(entry, "link");

  if (pkgcfg.type == TOML_STRING) {
    dep.kind = DEP_PKG_CONFIG;
    scopy(dep.pkg.pkg_config, sizeof(dep.pkg.pkg_config), pkgcfg.u.s);
  } else if (link.type == TOML_STRING) {
    dep.kind = DEP_SYSTEM;
    scopy(dep.pkg.link_flag, sizeof(dep.pkg.link_flag), link.u.s);
  } else {
    return 1;
  }

  if (!vec_push(deps, dep)) {
    stringvec_free(&dep.features);
    return 0;
  }
  return 1;
}

int manifest_load(const char *path, Manifest *out) {
  *out = (Manifest){0};

  toml_result_t result = toml_parse_file_ex(path);
  if (!result.ok) {
    fprintf(stderr, "smelt: parse error: %s\n", result.errmsg);
    return 0;
  }

  toml_datum_t t = result.toptab;

  toml_datum_t name = toml_seek(t, "package.name");
  toml_datum_t ver = toml_seek(t, "package.version");
  if (name.type == TOML_STRING)
    scopy(out->name, sizeof(out->name), name.u.s);
  if (ver.type == TOML_STRING)
    scopy(out->version, sizeof(out->version), ver.u.s);

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
    stringvec_push(&out->debug.flags, "-g");
    stringvec_push(&out->debug.flags, "-O0");
  }
  if (stringvec_len(&out->release.flags) == 0) {
    stringvec_push(&out->release.flags, "-O3");
    stringvec_push(&out->release.flags, "-DNDEBUG");
  }

  toml_datum_t deps_tbl = toml_seek(t, "dependencies");
  if (deps_tbl.type == TOML_TABLE) {
    for (int i = 0; i < deps_tbl.u.tab.size; i++) {
      const char *depname = deps_tbl.u.tab.key[i];
      if (!depname)
        continue;
      toml_datum_t entry = toml_get(deps_tbl, depname);
      if (!parse_dep_entry(depname, entry, &out->deps)) {
        toml_free(result);
        manifest_free(out);
        return 0;
      }
    }
  }

  toml_datum_t sys_tbl = toml_seek(t, "system-dependencies");
  if (sys_tbl.type == TOML_TABLE) {
    for (int i = 0; i < sys_tbl.u.tab.size; i++) {
      const char *depname = sys_tbl.u.tab.key[i];
      if (!depname)
        continue;
      toml_datum_t entry = toml_get(sys_tbl, depname);
      if (!parse_system_dep_entry(depname, entry, &out->deps)) {
        toml_free(result);
        manifest_free(out);
        return 0;
      }
    }
  }

  toml_datum_t regs = toml_seek(t, "registries.sources");
  if (regs.type == TOML_ARRAY) {
    for (int i = 0; i < regs.u.arr.size; i++) {
      toml_datum_t e = regs.u.arr.elem[i];
      if (e.type == TOML_STRING)
        stringvec_push(&out->registries, e.u.s);
    }
  }

  if (stringvec_len(&out->registries) == 0) {
    stringvec_push(&out->registries,
                   "https://raw.githubusercontent.com/floofyplasma/"
                   "smelt-registry/main/recipes");
  }

  toml_free(result);
  return 1;
}

void manifest_free(Manifest *m) {
  for (size_t i = 0; i < vec_len(&m->deps); i++)
    stringvec_free(&m->deps.items[i].features);
  vec_free(&m->deps);

  stringvec_free(&m->extra_sources);
  stringvec_free(&m->include_dirs);
  stringvec_free(&m->link_flags);
  stringvec_free(&m->defines);
  stringvec_free(&m->dep_sources);
  stringvec_free(&m->registries);
  stringvec_free(&m->debug.flags);
  stringvec_free(&m->release.flags);
}
