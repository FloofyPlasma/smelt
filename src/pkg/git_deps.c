#include "pkg/git_deps.h"
#include <git2.h>
#include <stdio.h>

static int INITIALIZED = 0;

void git_deps_init(void) {
  if (!INITIALIZED) {
    INITIALIZED = 1;
    git_libgit2_init();
  }
}

void git_deps_free(void) {
  if (INITIALIZED) {
    git_libgit2_shutdown();
  }
}

int clone_repo(git_repository *repo, const char *url, const char *path) {
  if (!url || !path)
    return 0;
  git_clone_options options = {0};
  if (git_clone_options_init(&options, GIT_CLONE_OPTIONS_VERSION) != 0)
    return 0;

  options.fetch_opts.depth = 1;
  options.bare = 0;

  if (git_clone(&repo, url, path, &options)) {
    fprintf(stderr, "smelt: git clone failed: %s", git_error_last()->message);
    return 0;
  }

  return 1;
}
