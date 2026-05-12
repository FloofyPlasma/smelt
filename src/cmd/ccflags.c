#include "ccflags.h"
#include <stdio.h>
#include <string.h>

int ccflags_write_command_line(const Manifest *manifest, const char *profile,
                               char *cmdline, int cmdlinec, int no_defines) {
  int fpos = 0;
  if (manifest->c_standard[0])
    fpos += snprintf(cmdline + fpos, cmdlinec - fpos, "-std=%s ",
                     manifest->c_standard);
  if (strcmp(manifest->warnings, "all") == 0)
    fpos += snprintf(cmdline + fpos, cmdlinec - fpos, "-Wall -Wextra ");
  for (int i = 0; i < manifest->include_count; i++)
    fpos += snprintf(cmdline + fpos, cmdlinec - fpos, "-I%s ",
                     manifest->include_dirs[i]);
  for (int i = 0; i < (no_defines ? 0 : manifest->define_count); i++)
    fpos += snprintf(cmdline + fpos, cmdlinec - fpos, "-D%s ",
                     manifest->defines[i]);
  const Profile *prof =
      ((profile != NULL && strcmp(profile, "release") == 0) ? &manifest->release
                                                            : &manifest->debug);
  for (int i = 0; i < prof->flag_count; i++)
    fpos += snprintf(cmdline + fpos, cmdlinec - fpos, "%s ", prof->flags[i]);
  return fpos;
}
