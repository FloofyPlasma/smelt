#include "core/compiler.h"
#include "core/process.h"
#include "xxhash.h"
#include <stdio.h>
#include <string.h>

int compiler_fingerprint(const char *cc, char *ver_out, size_t ver_sz,
                         char *hash_out, size_t hash_sz) {
  if (!cc || !ver_out || !hash_out)
    return 0;

  ver_out[0] = '\0';
  hash_out[0] = '\0';

  char buf[1024] = {0};
  Process proc = {0};
  process_argv_push(&proc, cc);
  process_argv_push(&proc, "--version");
  int ok = process_capture(&proc, buf, sizeof(buf));
  process_free(&proc);

  if (!ok || buf[0] == '\0')
    return 0;

  char *nl = strchr(buf, '\n');
  if (nl)
    *nl = '\0';

  snprintf(ver_out, ver_sz, "%s", buf);

  XXH64_hash_t h = XXH3_64bits(buf, strlen(buf));
  snprintf(hash_out, hash_sz, "%016llx", (unsigned long long)h);

  return 1;
}
