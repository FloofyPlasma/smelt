#ifndef SMELT_VEC_H
#define SMELT_VEC_H

#include <stdlib.h>

#define VEC(T)                                                                 \
  struct {                                                                     \
    T *items;                                                                  \
    size_t len;                                                                \
    size_t cap;                                                                \
  }

#define vec_push(v, item)                                                      \
  ({                                                                           \
    int _ok = 1;                                                               \
    if ((v)->len >= (v)->cap) {                                                \
      size_t _new_cap = (v)->cap ? (v)->cap * 2 : 8;                           \
      void *_ptr = realloc((v)->items, _new_cap * sizeof(*(v)->items));        \
      if (!_ptr) {                                                             \
        _ok = 0;                                                               \
      } else {                                                                 \
        (v)->items = _ptr;                                                     \
        (v)->cap = _new_cap;                                                   \
      }                                                                        \
    }                                                                          \
    if (_ok)                                                                   \
      (v)->items[(v)->len++] = (item);                                         \
    _ok;                                                                       \
  })

#define vec_free(v)                                                            \
  do {                                                                         \
    free((v)->items);                                                          \
    (v)->items = NULL;                                                         \
    (v)->len = 0;                                                              \
    (v)->cap = 0;                                                              \
  } while (0)

#define vec_get(v, i) ((v)->items[i])
#define vec_len(v) ((v)->len)

#endif
