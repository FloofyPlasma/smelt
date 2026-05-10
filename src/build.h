#ifndef SMELT_BUILD_H
#define SMELT_BUILD_H

#include "manifest.h"

#define MAX_SOURCES 512

typedef struct {
  char compiler[MAX_PATH];
  char sources[MAX_SOURCES][MAX_PATH];
  int source_count;
} BuildCtx;

int build_run(const Manifest *m);

#endif
