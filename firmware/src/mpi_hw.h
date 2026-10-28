// Direct driver for the ESP32 RSA/MPI multiplier (the "Z = X * Y" mode), bypassing mbedtls. Same register sequence as ESP-IDF v4.4
// components/mbedtls/port/esp32/bignum.c (esp_mpi_mul_mpi_hw_op), but:
//   * the unit is enabled once per layer instead of once per multiply (no clock gating, power-down toggling or lock per call),
//   * operands are plain little-endian word arrays (no mbedtls_mpi bookkeeping: bitlen scans, grow, copy),
//   * the multiply is asynchronous: start() returns immediately, so the CPU can work on the previous product or the next operands while the
//     hardware runs; wait() polls the completion flag,
//   * status polling and result reads use the same DPORT-safe accessors as mbedtls (esp_dport_access_read_buffer for the bulk result read).
// Only one CPU may use the unit between acquire() and release() (nothing else in the firmware does).
#pragma once
#include <stdint.h>
#include "soc/hwcrypto_reg.h"
#include "soc/dport_reg.h"
#include "driver/periph_ctrl.h"
#include "esp32/dport_access.h"

namespace mpihw {

static inline uint32_t hw_words(uint32_t words) { return (words + 15u) & ~15u; }                                                       // operands are padded to 512-bit blocks
static const uint32_t MAX_WORDS = 64;                                                                                                  // 2048-bit operands, 4096-bit product

static inline bool acquire() {
  periph_module_enable(PERIPH_RSA_MODULE);
  DPORT_REG_CLR_BIT(DPORT_RSA_PD_CTRL_REG, DPORT_RSA_PD);
  for (uint32_t n = 2000000; n; n--) if (DPORT_REG_READ(RSA_CLEAN_REG) == 1) return true;     // same read as ESP-IDF
  return false;
}
static inline void release() {
  DPORT_REG_SET_BIT(DPORT_RSA_PD_CTRL_REG, DPORT_RSA_PD);
  periph_module_disable(PERIPH_RSA_MODULE);
}

// Load X (xw words) into the X block and Y (yw words) into the upper half of the Z block, both zero-extended to hw words; zero the lower half of
// the Z block (the previous product lives there). hw must be hw_words(max(xw, yw)).
static inline void load(const uint32_t* x, uint32_t xw, const uint32_t* y, uint32_t yw, uint32_t hw) {
  volatile uint32_t* xb = (volatile uint32_t*)RSA_MEM_X_BLOCK_BASE;
  volatile uint32_t* zb = (volatile uint32_t*)RSA_MEM_Z_BLOCK_BASE;
  uint32_t i = 0;
  for (; i < xw; i++) xb[i] = x[i];
  for (; i < hw; i++) xb[i] = 0;
  for (i = 0; i < hw; i++) zb[i] = 0;
  for (i = 0; i < yw; i++) zb[hw + i] = y[i];
  for (; i < hw; i++) zb[hw + i] = 0;
}
// The two halves of load(): X alone, and Y with the Z-block clearing. A sequence of products that share X only needs the first once.
static inline void load_x(const uint32_t* x, uint32_t xw, uint32_t hw) {
  volatile uint32_t* xb = (volatile uint32_t*)RSA_MEM_X_BLOCK_BASE;
  uint32_t i = 0;
  for (; i < xw; i++) xb[i] = x[i];
  for (; i < hw; i++) xb[i] = 0;
}
static inline void load_y(const uint32_t* y, uint32_t yw, uint32_t hw) {
  volatile uint32_t* zb = (volatile uint32_t*)RSA_MEM_Z_BLOCK_BASE;
  uint32_t i;
  for (i = 0; i < hw; i++) zb[i] = 0;
  for (i = 0; i < yw; i++) zb[hw + i] = y[i];
  for (; i < hw; i++) zb[hw + i] = 0;
}
static inline void start(uint32_t hw) {
  DPORT_REG_WRITE(RSA_M_DASH_REG, 0);
  DPORT_REG_WRITE(RSA_MULT_MODE_REG, ((hw * 2) / 16) + 7);       // number of 512-bit blocks of the result, plus 7
  DPORT_REG_WRITE(RSA_INTERRUPT_REG, 1);                          // clear the done flag (this write also orders the operand stores before the start)
  DPORT_REG_WRITE(RSA_MULT_START_REG, 1);
}
static inline bool done() { return DPORT_REG_READ(RSA_INTERRUPT_REG) == 1; }
static inline bool wait(uint32_t max_polls = 2000000) {
  while (DPORT_REG_READ(RSA_INTERRUPT_REG) != 1) { if (!--max_polls) return false; }
  DPORT_REG_WRITE(RSA_INTERRUPT_REG, 1); return true;
}
static inline void read_z(uint32_t* z, uint32_t zw) { esp_dport_access_read_buffer(z, RSA_MEM_Z_BLOCK_BASE, zw); }
// Plain loads: valid while no other CPU touches DPORT space concurrently (single-core inference; the other core idles).
static inline void read_z_fast(uint32_t* z, uint32_t zw) {
  const volatile uint32_t* p = (const volatile uint32_t*)RSA_MEM_Z_BLOCK_BASE;
  uint32_t i = 0;
  for (; i + 4 <= zw; i += 4) { const uint32_t a = p[i], b = p[i + 1], c = p[i + 2], d = p[i + 3]; z[i] = a; z[i + 1] = b; z[i + 2] = c; z[i + 3] = d; }
  for (; i < zw; i++) z[i] = p[i];
}

// z += product, as one multi-word integer addition (the slots are wide enough that the sum never carries between slots, so this is a slot-wise sum)
static inline void read_z_add(uint32_t* z, uint32_t zw) {
  const volatile uint32_t* p = (const volatile uint32_t*)RSA_MEM_Z_BLOCK_BASE;
  uint32_t carry = 0;
  for (uint32_t i = 0; i < zw; i++) { const uint32_t v = p[i], s = z[i] + v, c1 = s < v, s2 = s + carry; carry = c1 | (s2 < carry); z[i] = s2; }
}

}  // namespace mpihw
