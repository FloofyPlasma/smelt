#ifndef SMELT_RESOLVE_H
#define SMELT_RESOLVE_H

#include "core/stringvec.h"
#include "core/vec.h"
#include "project/manifest.h"

typedef struct {
  char name[MAX_NAME];
  char version[MAX_STR];
  char commit[64];
  char git[MAX_STR];
  char tag[MAX_STR];
  int direct;
  char required_by[MAX_NAME];
  StringVec features;
} ResolvedPackage;

typedef VEC(ResolvedPackage) ResolvedPackageVec;

typedef struct {
  const char *registry_url;
  const char *cache_dir;
} ResolveCtx;

int resolve_deps(const Manifest *m, const ResolveCtx *ctx,
                 ResolvedPackageVec *out);
void resolved_package_vec_free(ResolvedPackageVec *v);
int fetch_recipe(const char *name, const char *version, const ResolveCtx *ctx,
                 char *recipe_path_out, size_t path_sz);

#endif
