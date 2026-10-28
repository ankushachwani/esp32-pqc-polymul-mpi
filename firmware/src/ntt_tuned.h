// A tuned NTT for the three rings, written for the Xtensa LX6: 32-bit signed Montgomery arithmetic (R = 2^32, the core has mull and mulsh),
// coefficients kept in int32 so there is no sign extension, one reduction per butterfly and none between layers (the sums fit in 32 bits).
// Same transform structure as PQClean (incomplete 7-layer NTT for q = 3329, complete negacyclic NTT for q = 8380417 and q = 12289), so the
// operation count is the same; the comparison is about how each is written for this core, not about a different algorithm.
#ifndef NTT_TUNED_H
#define NTT_TUNED_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
enum { TN_KEM, TN_DSA, TN_FALCON };
typedef struct {
  int kind, n, layers;
  int32_t q, qinv, f;             // f = n'^-1 * R^2 mod q (n' = 2^layers), applied at the end of the inverse transform
  int32_t zeta[512];              // Montgomery form, bit-reversed order
} TNtt;
void tntt_init(TNtt* t, int kind);
void tntt_fwd(const TNtt* t, int32_t* a);                                           // in place
void tntt_inv(const TNtt* t, int32_t* a);                                           // in place, output in [0, q), plain (undone Montgomery factor)
void tntt_mul(const TNtt* t, int32_t* r, const int32_t* a, const int32_t* b);       // pointwise (basemul for q = 3329), result carries R^-1
void tntt_mul_acc(const TNtt* t, int32_t* r, const int32_t* a, const int32_t* b);   // r += a*b pointwise
#ifdef __cplusplus
}
#endif
#endif
