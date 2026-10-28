// Kronecker packing of a negacyclic polynomial product into big-integer multiplies (see host/kron_proto.py for the derivation).
// Portable C: no hardware calls in here, so it builds on the host for the tests. The ESP32 stages that run the multiplies are in polymul_hw.cpp.
#ifndef KRON_H
#define KRON_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

#define KRON_MAX_CHUNKS 16
#define KRON_MAX_WORDS 64                         // one operand: 2048 bits (the Z block of the unit holds the 4096-bit product)
#define KRON_MAX_ACC (2 * 528)                    // 2 * c * m with c * m <= n + c - 1, n <= 512
#define KRON_ZWORDS (2 * KRON_MAX_WORDS + 4)      // product buffer, tail words stay zero so slot extraction can read past the end

typedef struct {
  const char* name;
  int n;                                          // ring is Z_q[x]/(x^n + 1)
  uint32_t q;
  int32_t amin, amax, bmin, bmax;                 // guaranteed coefficient ranges of the two operands
} KronCase;

typedef struct {
  const KronCase* cs;
  int n, c, m, w;                                 // c chunks of m coefficients, w bits per slot
  uint32_t oa, ob;                                // shifts that make the operands non-negative
  uint32_t hw;                                    // operand length in words, padded to a multiple of 16 (512 bits)
} KronPlan;

// 0 on success, -1 if the operands would not fit in 2048 bits (or c is out of range)
int kron_plan(const KronCase* cs, int c, KronPlan* p);

// chunk-th chunk of v (+off) into out[0..hw)
void kron_pack(const KronPlan* p, const int32_t* v, uint32_t off, int chunk, uint32_t* out);
// add the slots of the product of chunk ci of a and chunk cj of b (z has 2*hw words plus zero tail) into acc
void kron_accum(const KronPlan* p, const uint32_t* z, int ci, int cj, uint64_t* acc);
// pre[0] = 0, pre[i+1] = pre[i] + v[i]
void kron_prefix(const int32_t* v, int n, int64_t* pre);
// remove the shifts, fold x^n = -1, reduce mod q into [0, q)
void kron_finish(const KronPlan* p, const uint64_t* acc, const int64_t* pa, const int64_t* pb, int32_t* out);

// O(n^2) reference, result in [0, q)
void kron_ref_negacyclic(int n, uint32_t q, const int32_t* a, const int32_t* b, int32_t* out);
// schoolbook stand-in for the multiplier (host tests only)
void kron_sw_mul(const uint32_t* x, const uint32_t* y, uint32_t hw, uint32_t* z);

// One polynomial product: out = a*b in [0, q). The callback type lets the scheme code in schemes_*.c run with any of the stages.
typedef void (*kron_mul_fn)(const KronPlan* p, const int32_t* a, const int32_t* b, int32_t* out);
// reference implementation of the whole pipeline on top of kron_sw_mul (host tests)
void kron_mul_sw(const KronPlan* p, const int32_t* a, const int32_t* b, int32_t* out);

#ifdef __cplusplus
}
#endif
#endif
