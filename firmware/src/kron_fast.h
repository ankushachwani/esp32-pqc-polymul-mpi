#pragma once
#include "kron.h"
#ifdef __cplusplus
extern "C" {
#endif
void kron_accum_fast(const KronPlan* p, const uint32_t* z, int ci, int cj, void* acc);
void kron_pack_fast(const KronPlan* p, const int32_t* v, uint32_t off, int chunk, uint32_t* out);
void kron_finish_fast(const KronPlan* p, const void* acc, const void* pa, const void* pb, int32_t* out);
#ifdef __cplusplus
}
#endif
