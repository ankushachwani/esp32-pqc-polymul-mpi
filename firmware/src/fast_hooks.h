// Scheme hooks (see schemes.h) running on the optimised path. setup() prepares a key (packs the fixed operands) and is outside the timed region.
#pragma once
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
void* fast_kem_setup(const int32_t* s_std, int c, uint32_t opt);
void fast_kem_dot(void* ctx, const int32_t* u, int32_t* out);
void* fast_dsa_setup(const int32_t* s1, const int32_t* s2, const int32_t* t0, int c_cs, int c_ct0, uint32_t opt);
void fast_dsa_prod(void* ctx, const int32_t* c, int which, int32_t* out);
void* fast_fal_setup(const int32_t* h, int c, uint32_t opt);
void fast_fal_prod(void* ctx, const int32_t* h, const int32_t* s2, int32_t* out);
size_t fast_ctx_bytes(void* ctx);       // memory held by the prepacked key material
void fast_free(void* ctx);
#ifdef __cplusplus
}
#endif
