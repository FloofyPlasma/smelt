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

  for (size_t i = 0; i < manifest->include_dirs.len; i++)
    fpos += snprintf(cmdline + fpos, cmdlinec - fpos, "-I%s ",
                     stringvec_get(&manifest->include_dirs, i));

  if (!no_defines)
    for (size_t i = 0; i < manifest->defines.len; i++)
      fpos += snprintf(cmdline + fpos, cmdlinec - fpos, "-D%s ",
                       stringvec_get(&manifest->defines, i));

  const Profile *prof = (profile && strcmp(profile, "release") == 0)
                            ? &manifest->release
                            : &manifest->debug;
  for (size_t i = 0; i < prof->flags.len; i++)
    fpos += snprintf(cmdline + fpos, cmdlinec - fpos, "%s ",
                     stringvec_get(&prof->flags, i));

  return fpos;
}
