#ifndef SMELT_FS_H
#define SMELT_FS_H

#include "cmd/build.h"

void ensure_dirs(const char *path);
int scan_dir(BuildCtx *ctx, const char *dir);

// TODO(FloofyPlasma): does this need to be exposed like this?
int ends_with_c(const char *name);

int is_c_source(const char *name);
int is_cpp_source(const char *name);
#endif
