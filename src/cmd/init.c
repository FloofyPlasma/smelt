#include "cmd/init.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>

int init_run(void) {
  FILE *check = fopen("smelt.toml", "r");
  if (check) {
    fclose(check);
    fprintf(stderr, "smelt: smelt.toml already exists\n");
    return 0;
  }

  char name[128] = {0};
  char version[64] = {0};
  char standard[32] = {0};

  printf("project name: ");
  if (!fgets(name, sizeof(name), stdin))
    return 0;
  name[strcspn(name, "\n")] = '\0';

  printf("version [0.1.0]: ");
  if (!fgets(version, sizeof(version), stdin))
    return 0;
  version[strcspn(version, "\n")] = '\0';
  if (version[0] == '\0')
    snprintf(version, sizeof(version), "0.1.0");

  printf("c standard [c17]: ");
  if (!fgets(standard, sizeof(standard), stdin))
    return 0;
  standard[strcspn(standard, "\n")] = '\0';
  if (standard[0] == '\0')
    snprintf(standard, sizeof(standard), "c17");

  FILE *fp = fopen("smelt.toml", "w");
  if (!fp) {
    perror("smelt: cannot write smelt.toml");
    return 0;
  }

  fprintf(fp,
          "[package]\n"
          "name = \"%s\"\n"
          "version = \"%s\"\n"
          "\n"
          "[build]\n"
          "c_standard = \"%s\"\n"
          "warnings = \"all\"\n"
          "src_dir = \"src\"\n"
          "out_dir = \"build\"\n",
          name, version, standard);

  fclose(fp);

  mkdir("src", 0755);
  mkdir("build", 0755);

  printf("smelt: created smelt.toml\n");
  printf("smelt: created src/\n");
  printf("smelt: created build/\n");
  return 1;
}
