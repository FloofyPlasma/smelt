#ifndef SMELT_PKG_CACHE_H
#define SMELT_PKG_CACHE_H

#include <stddef.h>

int pkg_cache_ensure(const char *name, const char *url, const char *tag,
                     char *commit_out, size_t commit_sz, char *worktree_out,
                     size_t worktree_sz);

void pkg_cache_bare_path(char *dst, size_t dstsz, const char *name);

void pkg_cache_worktree_path(char *dst, size_t dstsz, const char *name,
                             const char *commit);

#endif
