#ifndef SMELT_DEPBUILD_H
#define SMELT_DEPBUILD_H

#include "core/stringvec.h"
#include "core/vec.h"
#include "pkg/resolve.h"
#include "project/manifest.h"

typedef struct {
  char name[MAX_NAME];
  StringVec public_includes;
  StringVec link_flags;
} BuiltDep;

typedef VEC(BuiltDep) BuiltDepVec;

typedef struct {
  const char *cc;
  const char *cxx;
  const char *compiler_hash;
  const char *out_dir;
  const char *profile;
} DepBuildCtx;

int depbuild_run(const ResolvedPackageVec *pkgs, const DepBuildCtx *ctx,
                 BuiltDepVec *out);

void builtdep_vec_free(BuiltDepVec *v);

const BuiltDep *builtdep_find(const BuiltDepVec *v, const char *name);

#endif
