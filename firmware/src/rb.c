// Deterministic randombytes for the benchmark: a xorshift generator that can be reseeded, so the reference and the accelerated variant
// of a randomised operation see the same bytes. Not a CSPRNG.
#include <stddef.h>
#include <stdint.h>
#include "randombytes.h"
static uint64_t rb_state = 0x9E3779B97F4A7C15ull;
void rb_seed(uint64_t s) { rb_state = s ? s : 0x9E3779B97F4A7C15ull; }
int randombytes(uint8_t* out, size_t n) {
  for (size_t i = 0; i < n; i++) { rb_state ^= rb_state << 13; rb_state ^= rb_state >> 7; rb_state ^= rb_state << 17; out[i] = (uint8_t)(rb_state >> 24); }
  return 0;
}
