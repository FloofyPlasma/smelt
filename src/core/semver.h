#ifndef SMELT_SEMVER_H
#define SMELT_SEMVER_H

#include <stddef.h>

typedef struct {
  int major;
  int minor;
  int patch;
} Semver;

int semver_parse(const char *s, Semver *out);

int semver_eq(Semver a, Semver b);

int semver_cmp(Semver a, Semver b);

const char *semver_str(Semver v, char *dst, size_t dstsz);

#endif
