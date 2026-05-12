#ifndef SMELT_DEPS_H
#define SMELT_DEPS_H

#define MAX_CMD 16384

#include "manifest.h"

int deps_ensure(Manifest *m);
int deps_update(Manifest *m);
int dep_add_pkgconfig(const char *name);
int dep_add_git(const char *url, const char **files, int file_count);
int dep_add_local(const char *path);

#endif
