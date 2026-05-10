#ifndef SMELT_COMPDB_H
#define SMELT_COMPDB_H

#include "build.h"
#include "manifest.h"

int compdb_write(const Manifest *m, const BuildCtx *ctx);

#endif
