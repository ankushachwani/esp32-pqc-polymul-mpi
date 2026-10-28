// Host stand-in for mpi_hw.h: the same calls, the product computed in software into a Z block that behaves like the unit's (Y in the upper half
// of Z, lower half must be cleared by the caller, X untouched by the multiply). Also provides the scratch arrays polymul_hw.cpp defines on the board.
#pragma once
#include <stdint.h>
#include <string.h>
#include "kron.h"
extern "C" void kron_sw_mul(const uint32_t* x, const uint32_t* y, uint32_t hw, uint32_t* z);
#ifdef MOCK_DEFS
#define MOCK_EXTERN
#else
#define MOCK_EXTERN extern
#endif
MOCK_EXTERN uint32_t g_A[KRON_MAX_CHUNKS * KRON_MAX_WORDS], g_B[KRON_MAX_CHUNKS * KRON_MAX_WORDS], g_zbuf[2][KRON_ZWORDS];
MOCK_EXTERN uint64_t g_acc[KRON_MAX_ACC];
MOCK_EXTERN int64_t g_pa[KRON_MAX_N + 1], g_pb[KRON_MAX_N + 1];
namespace mpihw {
static uint32_t Xb[128], Zb[128]; static uint32_t pending_hw = 0;
static inline bool acquire() { return true; }
static inline void release() {}
static inline void load_x(const uint32_t* x, uint32_t xw, uint32_t hw) { uint32_t i = 0; for (; i < xw; i++) Xb[i] = x[i]; for (; i < hw; i++) Xb[i] = 0; }
static inline void load_y(const uint32_t* y, uint32_t yw, uint32_t hw, bool clear_lower = true) {
  if (clear_lower) for (uint32_t i = 0; i < hw; i++) Zb[i] = 0;
  uint32_t i; for (i = 0; i < yw; i++) Zb[hw + i] = y[i]; for (; i < hw; i++) Zb[hw + i] = 0;
}
static inline void start(uint32_t hw) {
  uint32_t y[64], z[128]; for (uint32_t i = 0; i < hw; i++) y[i] = Zb[hw + i];
  kron_sw_mul(Xb, y, hw, z); for (uint32_t i = 0; i < 2 * hw; i++) Zb[i] = z[i];     // lower half was meant to be zero: the unit adds into Z; this mock overwrites
}
static inline bool wait(uint32_t = 0) { return true; }
static inline void read_z_fast(uint32_t* z, uint32_t zw) { for (uint32_t i = 0; i < zw; i++) z[i] = Zb[i]; }
static inline void read_z_add(uint32_t* z, uint32_t zw) { uint32_t carry = 0; for (uint32_t i = 0; i < zw; i++) { const uint32_t v = Zb[i], s = z[i] + v, c1 = s < v, s2 = s + carry; carry = c1 | (s2 < carry); z[i] = s2; } }
}
