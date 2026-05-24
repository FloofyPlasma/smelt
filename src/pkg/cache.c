#define _POSIX_C_SOURCE 200809L

#include "pkg/cache.h"
#include "core/fs.h"
#include "core/process.h"
#include "project/manifest.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static void cache_root(char *dst, size_t dstsz) {
  const char *home = getenv("HOME");
  if (!home || home[0] == '\0')
    home = "/tmp";
  snprintf(dst, dstsz, "%s/.cache/smelt", home);
}

void pkg_cache_bare_path(char *dst, size_t dstsz, const char *name) {
  char root[MAX_PATH];
  cache_root(root, sizeof(root));
  snprintf(dst, dstsz, "%s/%s", root, name);
}

void pkg_cache_worktree_path(char *dst, size_t dstsz, const char *name,
                             const char *commit) {
  char root[MAX_PATH];
  cache_root(root, sizeof(root));
  snprintf(dst, dstsz, "%s/%s/%s", root, name, commit);
}

static int dir_exists(const char *path) {
  struct stat st;
  return stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int git_clone_bare(const char *url, const char *dst_path) {
  Process proc = {0};
  process_argv_push(&proc, "git");
  process_argv_push(&proc, "clone");
  process_argv_push(&proc, "--bare");
  process_argv_push(&proc, "--filter=blob:none");
  process_argv_push(&proc, url);
  process_argv_push(&proc, dst_path);
  process_print(&proc);
  int ok = process_run(&proc);
  process_free(&proc);
  if (!ok)
    fprintf(stderr, "smelt: git clone failed: %s\n", url);
  return ok;
}

static int git_fetch_tag(const char *bare_path, const char *tag) {
  char refspec[MAX_STR * 2];
  snprintf(refspec, sizeof(refspec), "refs/tags/%s:refs/tags/%s", tag, tag);

  Process proc = {0};
  process_argv_push(&proc, "git");
  process_argv_push(&proc, "-C");
  process_argv_push(&proc, bare_path);
  process_argv_push(&proc, "fetch");
  process_argv_push(&proc, "--depth=1");
  process_argv_push(&proc, "origin");
  process_argv_push(&proc, refspec);
  process_print(&proc);
  int ok = process_run(&proc);
  process_free(&proc);
  if (!ok)
    fprintf(stderr, "smelt: git fetch tag failed: %s\n", tag);
  return ok;
}

static int git_resolve_tag(const char *bare_path, const char *tag,
                           char *commit_out, size_t commit_sz) {
  char tagref[MAX_STR * 2];
  snprintf(tagref, sizeof(tagref), "refs/tags/%s^{}", tag);

  Process proc = {0};
  process_argv_push(&proc, "git");
  process_argv_push(&proc, "-C");
  process_argv_push(&proc, bare_path);
  process_argv_push(&proc, "rev-parse");
  process_argv_push(&proc, tagref);

  char buf[128] = {0};
  int ok = process_capture(&proc, buf, sizeof(buf));
  process_free(&proc);

  if (!ok || buf[0] == '\0') {
    fprintf(stderr, "smelt: failed to resolve tag %s\n", tag);
    return 0;
  }

  buf[strcspn(buf, "\n")] = '\0';
  snprintf(commit_out, commit_sz, "%s", buf);
  return 1;
}

static int git_add_worktree(const char *bare_path, const char *worktree_path,
                            const char *commit) {
  Process proc = {0};
  process_argv_push(&proc, "git");
  process_argv_push(&proc, "-C");
  process_argv_push(&proc, bare_path);
  process_argv_push(&proc, "worktree");
  process_argv_push(&proc, "add");
  process_argv_push(&proc, "--detach");
  process_argv_push(&proc, worktree_path);
  process_argv_push(&proc, commit);
  process_print(&proc);
  int ok = process_run(&proc);
  process_free(&proc);
  if (!ok)
    fprintf(stderr, "smelt: git worktree add failed for commit %s\n", commit);
  return ok;
}

int pkg_cache_ensure(const char *name, const char *url, const char *tag,
                     char *commit_out, size_t commit_sz, char *worktree_out,
                     size_t worktree_sz) {
  char bare[MAX_PATH];
  pkg_cache_bare_path(bare, sizeof(bare), name);

  if (!dir_exists(bare)) {
    ensure_dirs(bare);

    if (!git_clone_bare(url, bare))
      return 0;
  }

  if (!git_fetch_tag(bare, tag))
    return 0;

  char commit[64] = {0};
  if (!git_resolve_tag(bare, tag, commit, sizeof(commit)))
    return 0;

  snprintf(commit_out, commit_sz, "%s", commit);

  char worktree[MAX_PATH * 2];
  pkg_cache_worktree_path(worktree, sizeof(worktree), name, commit);
  snprintf(worktree_out, worktree_sz, "%s", worktree);

  if (!dir_exists(worktree)) {
    if (!git_add_worktree(bare, worktree, commit))
      return 0;
    printf("smelt: cached %s @ %.8s\n", name, commit);
  } else {
    printf("smelt: using cached %s @ %.8s\n", name, commit);
  }

  return 1;
}
