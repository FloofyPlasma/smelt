#include "core/hash.h"
#include <string.h>

void hash_update_vec(XXH3_state_t *state, const StringVec *v) {
  for (size_t i = 0; i < stringvec_len(v); i++) {
    const char *s = stringvec_get(v, i);

    XXH3_64bits_update(state, s, strlen(s));

    char nul = '\0';
    XXH3_64bits_update(state, &nul, 1);
  }
}
