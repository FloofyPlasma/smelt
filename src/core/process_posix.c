#ifndef _WIN32

#define _POSIX_C_SOURCE 200809L

#include "core/process.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static inline Process process_empty(void) { return (Process){0}; }

static char **process_make_argv(Process *p) {
  size_t argc = stringvec_len(&p->argv);

  char **argv = calloc(argc + 1, sizeof(char *));
  if (!argv)
    return NULL;

  for (size_t i = 0; i < argc; i++)
    argv[i] = (char *)stringvec_get(&p->argv, i);

  argv[argc] = NULL;

  return argv;
}

static int process_wait(pid_t pid) {
  int status;

  if (waitpid(pid, &status, 0) < 0)
    return 0;

  return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

void process_free(Process *p) {
  if (!p)
    return;

  stringvec_free(&p->argv);

  *p = process_empty();
}

int process_argv_push(Process *p, const char *arg) {
  return stringvec_push(&p->argv, arg);
}

int process_argv_extend(Process *p, const StringVec *v) {
  for (size_t i = 0; i < stringvec_len(v); i++) {
    if (!process_argv_push(p, stringvec_get(v, i)))
      return 0;
  }

  return 1;
}

void process_print(const Process *p) {
  if (!p)
    return;

  printf("smelt:");

  for (size_t i = 0; i < stringvec_len(&p->argv); i++) {
    printf(" %s", stringvec_get(&p->argv, i));
  }

  printf("\n");
}

int process_run(Process *p) {
  char **argv = process_make_argv(p);
  if (!argv)
    return 0;

  pid_t pid = fork();

  if (pid < 0) {
    free(argv);
    return 0;
  }

  if (pid == 0) {
    if (p->cwd && chdir(p->cwd) != 0) {
      perror("smelt: chdir");
      _exit(1);
    }

    execvp(argv[0], argv);

    perror("smelt: execvp");
    _exit(1);
  }

  free(argv);

  return process_wait(pid);
}

int process_capture(Process *p, char *dst, size_t dstsz) {
  if (!dst || dstsz == 0)
    return 0;

  dst[0] = '\0';

  int pipefd[2];

  // TODO(FloofyPlasma): Is clang-tidy right here?
  // NOLINTNEXTLINE
  if (pipe(pipefd) != 0)
    return 0;

  char **argv = process_make_argv(p);

  if (!argv) {
    close(pipefd[0]);
    close(pipefd[1]);
    return 0;
  }

  pid_t pid = fork();

  if (pid < 0) {
    free(argv);
    close(pipefd[0]);
    close(pipefd[1]);
    return 0;
  }

  if (pid == 0) {
    close(pipefd[0]);

    dup2(pipefd[1], STDOUT_FILENO);
    dup2(pipefd[1], STDERR_FILENO);

    close(pipefd[1]);

    if (p->cwd && chdir(p->cwd) != 0) {
      perror("smelt: chdir");
      _exit(1);
    }

    execvp(argv[0], argv);

    perror("smelt: execvp");
    _exit(1);
  }

  free(argv);

  close(pipefd[1]);

  ssize_t nread = read(pipefd[0], dst, dstsz - 1);

  if (nread < 0)
    nread = 0;

  dst[nread] = '\0';

  close(pipefd[0]);

  return process_wait(pid);
}

#endif
