#ifndef SMELT_GITDEPS_H
#define SMELT_GITDEPS_H

#include <git2.h>

void git_deps_init(void);
void git_deps_free(void);

int clone_repo(git_repository *repo, const char *url, const char *path);
void free_repo(git_repository *repo);

// TODO(FloofyPlasma): Add support for checkout for specifici commit

#endif
