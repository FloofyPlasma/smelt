#include "pkg/recipe.h"
#include "tomlc17.h"

#if defined(__linux__)
#define SMELT_PLATFORM "linux"
#elif defined(__APPLE__)
#define SMELT_PLATFORM "macos"
#else
#define SMELT_PLATFORM "unknown"
#endif

static int push_str_array(StringVec *out, toml_datum_t arr) {
  if (arr.type != TOML_ARRAY)
    return 1;
  for (int i = 0; i < arr.u.arr.size; i++) {
    toml_datum_t e = arr.u.arr.elem[i];
    if (e.type != TOML_STRING)
      continue;
    if (!stringvec_push(out, e.u.s))
      return 0;
  }

  return 1;
}

static int parse_files(toml_datum_t root, RecipeFiles *out) {
  toml_datum_t files_tbl = toml_seek(root, "files");
  if (files_tbl.type != TOML_TABLE)
    return 1;

  if (!push_str_array(&out->base, toml_get(files_tbl, "base")))
    return 0;

  toml_datum_t platform_tbl = toml_get(files_tbl, "platform");
  if (platform_tbl.type == TOML_TABLE) {
    toml_datum_t plat = toml_get(platform_tbl, SMELT_PLATFORM);
    if (!push_str_array(&out->platform, plat))
      return 0;
  }

  return 1;
}

static int parse_link(toml_datum_t root, StringVec *out) {
  toml_datum_t link_tbl = toml_seek(root, "link");
  if (link_tbl.type != TOML_TABLE)
    return 1;

  toml_datum_t plat = toml_get(link_tbl, SMELT_PLATFORM);
  if (plat.type != TOML_TABLE)
    return 1;

  toml_datum_t flags = toml_get(plat, "flags");
  return push_str_array(out, flags);
}

static int parse_features(toml_datum_t root, RecipeFeatureVec *out) {
  toml_datum_t feat_tbl = toml_seek(root, "features");
  if (feat_tbl.type != TOML_TABLE)
    return 1;

  for (int i = 0; i < feat_tbl.u.tab.size; i++) {
    const char *fname = feat_tbl.u.tab.key[i];
    if (!fname)
      continue;

    toml_datum_t fentry = toml_get(feat_tbl, fname);
    if (fentry.type != TOML_TABLE)
      continue;

    RecipeFeature feat = {0};
    snprintf(feat.name, sizeof(feat.name), "%s", fname);

    if (!push_str_array(&feat.files, toml_get(fentry, "files")))
      return 0;

    if (!push_str_array(&feat.requires, toml_get(fentry, "retquires")))
      return 0;

    if (!vec_push(out, feat)) {
      stringvec_free(&feat.files);
      stringvec_free(&feat.requires);
      return 0;
    }
  }

  return 1;
}

static int parse_includes(toml_datum_t root, RecipeIncludes *out) {
  toml_datum_t iface = toml_seek(root, "interface");
  if (iface.type == TOML_TABLE) {
    if (!push_str_array(&out->public_includes, toml_get(iface, "includes")))
      return 0;
  }

  toml_datum_t build = toml_seek(root, "build");
  if (build.type == TOML_TABLE) {
    if (!push_str_array(&out->private_includes,
                        toml_get(build, "private_includes")))
      return 0;
  }

  return 1;
}

int recipe_load(const char *path, Recipe *out) {
  *out = (Recipe){0};

  toml_result_t result = toml_parse_file_ex(path);
  if (!result.ok) {
    fprintf(stderr, "smelt: recipe parse error in %s: %s\n", path,
            result.errmsg);
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

  if (!parse_includes(t, &out->includes)) {
    toml_free(result);
    recipe_free(out);
    return 0;
  }

  if (!parse_files(t, &out->files)) {
    toml_free(result);
    recipe_free(out);
    return 0;
  }

  if (!parse_features(t, &out->features)) {
    toml_free(result);
    recipe_free(out);
    return 0;
  }

  if (!parse_link(t, &out->link_flags)) {
    toml_free(result);
    recipe_free(out);
    return 0;
  }

  toml_free(result);
  return 1;
}

void recipe_free(Recipe *r) {
  if (!r)
    return;

  stringvec_free(&r->includes.public_includes);
  stringvec_free(&r->includes.private_includes);
  stringvec_free(&r->files.base);
  stringvec_free(&r->files.platform);
  stringvec_free(&r->link_flags);

  for (size_t i = 0; i < vec_len(&r->features); i++) {
    RecipeFeature *f = &vec_get(&r->features, i);
    stringvec_free(&f->files);
    stringvec_free(&f->requires);
  }

  vec_free(&r->features);
}

void recipe_cache_path(char *dst, size_t dstsz, const char *name,
                       const char *ver) {
  const char *home = getenv("HOME");
  if (!home || home[0] == '\0')
    home = "/tmp";
  snprintf(dst, dstsz, "%s/.cache/smelt/recipes/%s@%s.toml", home, name, ver);
}
