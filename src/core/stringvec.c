#include "core/stringvec.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static inline StringVec stringvec_empty(void) { return (StringVec){0}; }

void stringvec_free(StringVec *v) {
  if (!v)
    return;

  for (size_t i = 0; i < v->len; i++)
    free(v->items[i]);

  free(v->items);

  *v = stringvec_empty();
}

int stringvec_grow(StringVec *v, size_t needed) {
  if (v->cap >= needed)
    return 1;

  size_t new_cap = v->cap ? v->cap * 2 : 8;

  while (new_cap < needed)
    new_cap *= 2;

  char **new_items = realloc(v->items, new_cap * sizeof(char *));

  if (!new_items)
    return 0;

  v->items = new_items;
  v->cap = new_cap;

  return 1;
}

int stringvec_push(StringVec *v, const char *str) {
  if (!v || !str)
    return 0;

  if (!stringvec_grow(v, v->len + 1))
    return 0;

  size_t len = strlen(str);

  char *copy = malloc(len + 1);
  if (!copy)
    return 0;

  memcpy(copy, str, len + 1);

  v->items[v->len++] = copy;

  return 1;
}

int stringvec_push_unique(StringVec *v, const char *str) {
  for (size_t i = 0; i < v->len; i++)
    if (strcmp(v->items[i], str) == 0)
      return 0;
  return stringvec_push(v, str);
}

int stringvec_join(const StringVec *v, char sep, char *dst, size_t dstsz,
                   size_t offset) {
  if (!v || !dst || offset >= dstsz)
    return 0;
  int written = 0;
  for (size_t i = 0; i < v->len; i++) {
    int n = snprintf(dst + offset + written, dstsz - offset - written, "%c%s",
                     sep, v->items[i]);
    if (n < 0 || (size_t)(written + n) >= dstsz - offset)
      break;
    written += n;
  }
  return written;
}

const char *stringvec_get(const StringVec *v, size_t i) {
  if (!v || i >= v->len)
    return NULL;
  return v->items[i];
}

size_t stringvec_len(const StringVec *v) { return v->len; }
