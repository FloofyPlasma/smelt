#ifndef SMELT_HASH_H
#define SMELT_HASH_H

#include "core/stringvec.h"
#include "xxhash.h"

void hash_update_vec(XXH3_state_t *state, const StringVec *v);

#endif
