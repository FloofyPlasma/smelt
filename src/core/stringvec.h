#ifndef SMELT_STRINGVEC_H
#define SMELT_STRINGVEC_H

#include <stddef.h>

typedef struct {
  char **items;
  size_t len;
  size_t cap;
} StringVec;

void stringvec_free(StringVec *v);

int stringvec_push(StringVec *v, const char *str);
int stringvec_push_unique(StringVec *v, const char *str);
int stringvec_grow(StringVec *v, size_t needed);
int stringvec_join(const StringVec *v, char sep, char *dst, size_t dstsz,
                   size_t offset);
const char *stringvec_get(const StringVec *v, size_t i);
size_t stringvec_len(const StringVec *v);

#endif
