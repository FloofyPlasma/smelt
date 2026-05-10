#include "build.h"
#include "manifest.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: smelt <command>\n");
    fprintf(stderr, "commands: build, run\n");
    return 1;
  }

  if (strcmp(argv[1], "build") == 0) {
    Manifest m = {0};
    if (!manifest_load("smelt.toml", &m))
      return 1;
    if (!build_run(&m))
      return 1;
    return 0;
  }

  if (strcmp(argv[1], "run") == 0) {
    Manifest m = {0};
    if (!manifest_load("smelt.toml", &m))
      return 1;
    if (!build_run(&m))
      return 1;

    char output[MAX_PATH * 2];
    snprintf(output, sizeof(output), "%s/%s", m.out_dir, m.name);

    char *child_args[256] = {output};
    int child_argc = 1;
    for (int i = 2; i < argc; i++) {
      if (strcmp(argv[i], "--") == 0)
        continue;
      child_args[child_argc++] = argv[i];
    }
    child_args[child_argc] = NULL;

    execv(output, child_args);
    perror("smelt: execv failed");
    return 1;
  }

  fprintf(stderr, "smelt: unknown command: %s\n", argv[1]);
  return 1;
}
