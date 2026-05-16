#ifndef SMELT_GITDEPS_H
#define SMELT_GITDEPS_H

#include <git2.h>

void git_deps_init(void);

int clone_repo(git_repository *repo, const char *url, const char *path);

// TODO(FloofyPlasma): Add support for checkout for specifici commit

#endif
