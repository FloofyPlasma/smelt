#include "project/compdb.h"
#include "cmd/build.h"
#include "cmd/ccflags.h"
#include "project/manifest.h"
#include <stdio.h>
#include <unistd.h>

int compdb_write(const Manifest *m, const BuildCtx *ctx) {
  FILE *fp = fopen("compile_commands.json", "w");
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
  for (int i = 0; i < m->extra_count; i++)
    epos += snprintf(extra + epos, sizeof(extra) - epos, " %s/%s", cwd,
                     m->extra_sources[i]);

  fprintf(fp, "[\n");
  for (int i = 0; i < ctx->source_count; i++) {
    fprintf(fp,
            "  {\n"
            "    \"directory\": \"%s\",\n"
            "    \"command\": \"%s %s/%s%s\",\n"
            "    \"file\": \"%s/%s\"\n"
            "  }%s\n",
            cwd, cmdline, cwd, ctx->sources[i], extra, cwd, ctx->sources[i],
            i < ctx->source_count - 1 ? "," : "");
  }
  fprintf(fp, "]\n");

  fclose(fp);
  printf("smelt: wrote compile_commands.json\n");
  return 1;
}
