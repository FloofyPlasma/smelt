#include "project/compdb.h"
#include "cmd/build.h"
#include "cmd/ccflags.h"
#include "project/manifest.h"
#include <stdio.h>
#include <unistd.h>

// TODO(FloofyPlasma): clean this up and make it less shitty
int compdb_write(const Manifest *m, const BuildCtx *ctx) {
  FILE *fp = fopen("compile_commands.json", "we");
  if (!fp) {
    perror("smelt: cannot write compile_commands.json");
    return 0;
  }

  char cwd[MAX_PATH];
  if (!getcwd(cwd, sizeof(cwd))) {
    perror("smelt: getcwd");
    fclose(fp);
    return 0;
  }

  char cmdline[4096] = {0};
  ccflags_write_command_line(m, NULL, cmdline, sizeof(cmdline), 1);

  char extra[2048] = {0};
  int epos = 0;
  for (size_t i = 0; i < stringvec_len(&m->extra_sources); i++)
    epos += snprintf(extra + epos, sizeof(extra) - epos, " %s/%s", cwd,
                     stringvec_get(&m->extra_sources, i));

  fprintf(fp, "[\n");
  for (size_t i = 0; i < stringvec_len(&ctx->sources); i++) {
    const char *src = stringvec_get(&ctx->sources, i);
    fprintf(fp,
            "  {\n"
            "    \"directory\": \"%s\",\n"
            "    \"command\": \"%s %s/%s%s\",\n"
            "    \"file\": \"%s/%s\"\n"
            "  }%s\n",
            cwd, cmdline, cwd, src, extra, cwd, src,
            i < stringvec_len(&ctx->sources) - 1 ? "," : "");
  }
  fprintf(fp, "]\n");

  fclose(fp);
  printf("smelt: wrote compile_commands.json\n");
  return 1;
}
