#ifndef SMELT_COMPILER_H
#define SMELT_COMPILER_H

#include <stddef.h>

int compiler_fingerprint(const char *cc, char *ver_out, size_t ver_sz,
                         char *hash_out, size_t hash_sz);

#endif
