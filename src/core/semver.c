#include "core/semver.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>

static int parse_component(const char **s, int *out) {
  if (!**s)
    return -1;

  if (**s == '0' && (*s)[1] >= '0' && (*s)[1] <= '9')
    return -1;

  char *end;
  errno = 0;
  long val = strtol(*s, &end, 10);

  if (end == *s)
    return -1;
  if (errno == ERANGE)
    return -1;
  if (val < 0 || val > INT_MAX)
    return -1;

  *out = (int)val;
  *s = end;
  return 0;
}

int semver_parse(const char *s, Semver *out) {
  if (!s || !out)
    return 0;

  *out = (Semver){0};

  if (parse_component(&s, &out->major) != 0)
    return 0;
  if (*s++ != '.')
    return 0;

  if (parse_component(&s, &out->minor) != 0)
    return 0;
  if (*s++ != '.')
    return 0;

  if (parse_component(&s, &out->patch) != 0)
    return 0;

  if (*s != '\0')
    return 0;

  return 1;
}

int semver_eq(Semver a, Semver b) {
  return a.major == b.major && a.minor == b.minor && a.patch == b.patch;
}

int semver_cmp(Semver a, Semver b) {
  if (a.major != b.major)
    return a.major < b.major ? -1 : 1;
  if (a.minor != b.minor)
    return a.minor < b.minor ? -1 : 1;
  if (a.patch != b.patch)
    return a.patch < b.patch ? -1 : 1;
  return 0;
}

const char *semver_str(Semver v, char *dst, size_t dstsz) {
  snprintf(dst, dstsz, "%d.%d.%d", v.major, v.minor, v.patch);
  return dst;
}
