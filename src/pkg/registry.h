#ifndef SMELT_REGISTRY_C
#define SMELT_REGISTRY_C

#include "core/stringvec.h"
#include "project/manifest.h"
#include <stddef.h>

typedef struct {
  char name[MAX_NAME];
  char version[MAX_STR];
  char git[MAX_STR];
  char tag[MAX_STR];
  char build_system[32];
  StringVec build_args;
  char output_lib[MAX_PATH];
  char output_include[MAX_PATH];
  StringVec copy_files;
} Recipe;

void recipe_free(Recipe *r);
int registry_fetch_recipe(const Manifest *m, const char *name);
void registry_recipe_path(char *dst, size_t dstsz, const char *name);
int recipe_load(const char *path, Recipe *out);
int registry_add(Manifest *m, const char *name);

#endif
