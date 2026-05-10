#include "build.h"
#include "manifest.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv) {
  if (argc < 2) {
    fprintf(stderr, "usage: smelt <command>\n");
    fprintf(stderr, "commands: build\n");
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

  fprintf(stderr, "smelt: unknown command: %s\n", argv[1]);
  return 1;
}
