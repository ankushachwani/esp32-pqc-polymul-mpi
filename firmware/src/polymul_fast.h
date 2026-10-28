// The optimised path: everything of the overlap stage plus the changes studied in the README ("Further optimisations"). Each change has a switch so it
// can be measured alone and together. The unit must be acquired (mpihw::acquire) by the caller.
#pragma once
#include "kron.h"
enum {
  OPT_PREPACK = 1,    // fixed operands (key material) packed once, with their prefix sums; only the varying operand is packed per call
  OPT_XRES = 2,       // X stays in the unit's X block across products that share it (only Y and the cleared Z are stored)
  OPT_FUSE = 4,       // a sum of products is added as integers before one unpacking / unshifting (plan with terms > 1)
  OPT_SHARE = 8,      // products with a common operand: it is packed once and stays loaded
  OPT_FASTCPU = 16,   // unrolled pack / unpack and the one-pass unshift, fold and reduce (kron_fast.cpp)
  OPT_NOCLEAR = 32,   // experiment, REJECTED: skipping the clear of the lower Z half gives wrong products on the unit (the bit-exact checks fail), so it is not part of OPT_ALL
  OPT_ALL = 31
};
struct Packed { uint32_t* words; void* pre; };            // a fixed operand packed for one plan: c chunks of hw words, prefix sums (if the plan needs them)
Packed* packed_new(const KronPlan* p, const int32_t* b);
void packed_free(Packed* x);
void* prefix_sum_new(const KronPlan* p, const int32_t* const* b, int terms);       // prefix sums of b_0 + ... + b_{terms-1}, for fused sums with fixed b

// out = sum over t < p->terms of a[t]*b[t]. fb[t] / pre_b are used only with OPT_PREPACK (otherwise b is packed here); pre_b may be null.
void fast_mul(const KronPlan* p, const int32_t* const* a, const int32_t* const* b, const Packed* const* fb, const void* pre_b, int32_t* out, uint32_t opt);
// out[t] = a * b[t], t < nb <= 4, narrow plans only
void fast_mul_shared(const KronPlan* p, const int32_t* a, int nb, const int32_t* const* b, const Packed* const* fb, int32_t* const* out, uint32_t opt);
// sum of products without fusing: nterms separate products (plan1 has terms == 1) added mod q; used as the "not fused" arm of the ablation
void fast_sum_unfused(const KronPlan* plan1, int nterms, const int32_t* const* a, const int32_t* const* b, const Packed* const* fb1, int32_t* out, uint32_t opt);

