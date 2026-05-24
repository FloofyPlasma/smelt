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

static void write_str_array(FILE *fp, const char *key, const StringVec *v) {
  if (stringvec_len(v) == 0) {
    fprintf(fp, "%s  = []\n", key);
    return;
  }
  fprintf(fp, "%s = [", key);
  for (size_t i = 0; i < stringvec_len(v); i++) {
    fprintf(fp, "\"%s\"", stringvec_get(v, i));
    if (i < stringvec_len(v) - 1)
      fprintf(fp, ", ");
  }
  fprintf(fp, "]\n");
}

int manifest_save(const char *path, const Manifest *m) {
  FILE *fp = fopen(path, "we");
  if (!fp) {
    perror("smelt: cannot write manifest");
    return 0;
  }

  fprintf(fp, "[package]\n");
  fprintf(fp, "name = \"%s\"\n", m->name);
  fprintf(fp, "version = \"%s\"\n", m->version);
  fprintf(fp, "\n");

  fprintf(fp, "[build]\n");
  if (m->c_standard[0])
    fprintf(fp, "c_standard = \"%s\"\n", m->c_standard);
  if (m->warnings[0])
    fprintf(fp, "warnings = \"%s\"\n", m->warnings);
  if (m->src_dir[0])
    fprintf(fp, "src_dir = \"%s\"\n", m->src_dir);
  if (m->out_dir[0])
    fprintf(fp, "out_dir = \"%s\"\n", m->out_dir);
  write_str_array(fp, "link_flags", &m->link_flags);
  write_str_array(fp, "defines", &m->defines);
  write_str_array(fp, "include_dirs", &m->include_dirs);
  write_str_array(fp, "extra_sources", &m->extra_sources);
  fprintf(fp, "\n");

  fprintf(fp, "[profile.debug]\n");
  write_str_array(fp, "flags", &m->debug.flags);
  fprintf(fp, "\n");

  fprintf(fp, "[profile.release]\n");
  write_str_array(fp, "flags", &m->release.flags);
  fprintf(fp, "\n");

  int has_deps = 0;
  for (size_t i = 0; i < vec_len(&m->deps); i++) {
    const Dep *dep = &m->deps.items[i];
    if (dep->kind == DEP_GIT || dep->kind == DEP_LOCAL) {
      has_deps = 1;
      break;
    }
  }
  if (has_deps) {
    fprintf(fp, "[dependencies]\n");
    for (size_t i = 0; i < vec_len(&m->deps); i++) {
      const Dep *dep = &m->deps.items[i];
      if (dep->kind == DEP_GIT) {
        if (stringvec_len(&dep->features) == 0) {
          fprintf(fp, "%s = \"%s\"\n", dep->name, dep->version);
        } else {
          fprintf(fp, "%s = { version = \"%s\", features = [", dep->name,
                  dep->version);
          for (size_t j = 0; j < stringvec_len(&dep->features); j++) {
            fprintf(fp, "\"%s\"", stringvec_get(&dep->features, j));
            if (j < stringvec_len(&dep->features) - 1)
              fprintf(fp, ", ");
          }
          fprintf(fp, "] }\n");
        }
      } else if (dep->kind == DEP_LOCAL) {
        fprintf(fp, "%s = { path = \"%s\" }\n", dep->name, dep->local.path);
      }
    }
    fprintf(fp, "\n");
  }

  int has_sys = 0;
  for (size_t i = 0; i < vec_len(&m->deps); i++) {
    const Dep *dep = &m->deps.items[i];
    if (dep->kind == DEP_PKG_CONFIG || dep->kind == DEP_SYSTEM) {
      has_sys = 1;
      break;
    }
  }
  if (has_sys) {
    fprintf(fp, "[system-dependencies]\n");
    for (size_t i = 0; i < vec_len(&m->deps); i++) {
      const Dep *dep = &m->deps.items[i];
      if (dep->kind == DEP_PKG_CONFIG)
        fprintf(fp, "%s = { pkg-config = \"%s\" }\n", dep->name,
                dep->pkg.pkg_config);
      else if (dep->kind == DEP_SYSTEM)
        fprintf(fp, "%s = { link = \"%s\" }\n", dep->name, dep->pkg.link_flag);
    }
    fprintf(fp, "\n");
  }

  fclose(fp);
  return 1;
}

int manifest_add_dep(Manifest *m, const char *name, const char *version) {
  for (size_t i = 0; i < vec_len(&m->deps); i++)
    if (strcmp(m->deps.items[i].name, name) == 0)
      return 0;

  Dep dep = {0};
  dep.kind = DEP_GIT;
  snprintf(dep.name, sizeof(dep.name), "%s", name);
  snprintf(dep.version, sizeof(dep.version), "%s", version);

  if (!vec_push(&m->deps, dep))
    return 0;

  return 1;
}
