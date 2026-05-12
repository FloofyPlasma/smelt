#ifndef SMELT_COMPDB_H
#define SMELT_COMPDB_H

#include "cmd/build.h"
#include "project/manifest.h"

int compdb_write(const Manifest *m, const BuildCtx *ctx);

#endif
