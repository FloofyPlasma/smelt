#ifndef SMELT_CCFLAGS_H
#define SMELT_CCFLAGS_H

#include "project/manifest.h"

// Reads manifest and profile and writes new command line arguments to char
// *command_line
// If profile is NULL, does not add profile-specific flags
// TODO(maelstrom): fix string escaping instead of using no_defines
int ccflags_write_command_line(const Manifest *manifest, const char *profile,
                               char *cmdline, int cmdlinec, int no_defines);

// TODO(FloofyPlasma): Make flags work across diffreent compilers
int ccflags_build_vec(const Manifest *manifest, const char *profile,
                      StringVec *out, int no_defines);

#endif
