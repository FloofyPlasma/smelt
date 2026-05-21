#ifndef SMELT_RECIPE_H
#define SMELT_RECIPE_H

#include "core/stringvec.h"
#include "core/vec.h"
#include "project/manifest.h"

typedef struct {
  char name[MAX_NAME];
  StringVec files;
  StringVec requires;
} RecipeFeature;

typedef VEC(RecipeFeature) RecipeFeatureVec;

typedef struct {
  StringVec public_includes;
  StringVec private_includes;
} RecipeIncludes;

typedef struct {
  StringVec base;
  StringVec platform;
} RecipeFiles;

typedef struct {
  char name[MAX_NAME];
  char version[MAX_STR];
  char git[MAX_STR];
  char tag[MAX_STR];
  RecipeIncludes includes;
  RecipeFiles files;
  RecipeFeatureVec features;
  StringVec link_flags;
} Recipe;

int recipe_load(const char *path, Recipe *out);

void recipe_free(Recipe *r);

void recipe_cache_path(char *dst, size_t dstsz, const char *name,
                       const char *ver);

#endif
