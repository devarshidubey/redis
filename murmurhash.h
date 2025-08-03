#ifndef MURMURHASH_H
#define MURMURHASH_H

#include <stdint.h>
#include <stddef.h>

// Returns a 64-bit MurmurHash for the given key
uint64_t murmurhash64(const void *key, size_t len, uint64_t seed);

#endif
