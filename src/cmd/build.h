#ifndef SMELT_BUILD_H
#define SMELT_BUILD_H

#include "core/stringvec.h"
#include "project/manifest.h"

#define MAX_OBJ_PATH (MAX_PATH * 2)

typedef struct {
  char compiler[MAX_PATH];
  StringVec sources;
} BuildCtx;

int build_run(const Manifest *m, BuildCtx *ctx_cout, const char *profile);
void buildctx_free(BuildCtx *ctx);

#endif
