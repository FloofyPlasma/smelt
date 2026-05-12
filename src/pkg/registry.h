#ifndef SMELT_REGISTRY_C
#define SMELT_REGISTRY_C

#include "project/manifest.h"
#include <stddef.h>

typedef struct {
  char name[MAX_NAME];
  char version[MAX_STR];
  char git[MAX_STR];
  char tag[MAX_STR];
  char build_system[32];
  char build_args[16][MAX_STR];
  int build_arg_count;
  char output_lib[MAX_PATH];
  char output_include[MAX_PATH];
  char copy_files[MAX_DEP_FILES][MAX_PATH];
  int copy_file_count;
} Recipe;

int registry_fetch_recipe(const Manifest *m, const char *name);
void registry_recipe_path(char *dst, size_t dstsz, const char *name);

int recipe_load(const char *path, Recipe *out);
int recipe_install(const Recipe *r, Manifest *m);
int registry_add(Manifest *m, const char *name);

#endif
