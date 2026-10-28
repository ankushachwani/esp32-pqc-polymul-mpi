#include <Arduino.h>
#include <string.h>
#include "mbedtls/bignum.h"
#include "mpi_hw.h"
#include "polymul_hw.h"

const char* const STAGE_NAME[N_STAGES] = {"mbedtls_api", "limb_io", "direct_driver", "overlap"};
bool stage_needs_hw(int s) { return s >= S_DRIVER; }
Prof g_prof;

static uint32_t A[KRON_MAX_CHUNKS * KRON_MAX_WORDS], B[KRON_MAX_CHUNKS * KRON_MAX_WORDS];
static uint32_t zbuf[2][KRON_ZWORDS];
static uint64_t acc[KRON_MAX_ACC];
static int64_t pa[513], pb[513];

static inline uint32_t cyc() { return ESP.getCycleCount(); }

// ---- S_MBEDTLS
static mbedtls_mpi mx, my, mz;
static bool m_init = false;
static void m_setup() { if (!m_init) { mbedtls_mpi_init(&mx); mbedtls_mpi_init(&my); mbedtls_mpi_init(&mz); m_init = true; } }

static void mul_mbedtls(const KronPlan* p, const int32_t* a, const int32_t* b, int32_t* out) {
  m_setup();
  memset(acc, 0, sizeof acc);
  for (int i = 0; i < p->c; i++) { kron_pack(p, a, p->oa, i, A + i * p->hw); kron_pack(p, b, p->ob, i, B + i * p->hw); }
  memset(zbuf[0] + 2 * p->hw, 0, 16);
  for (int i = 0; i < p->c; i++) for (int j = 0; j < p->c; j++) {
    mbedtls_mpi_read_binary_le(&mx, (const uint8_t*)(A + i * p->hw), p->hw * 4);
    mbedtls_mpi_read_binary_le(&my, (const uint8_t*)(B + j * p->hw), p->hw * 4);
    mbedtls_mpi_mul_mpi(&mz, &mx, &my);
    mbedtls_mpi_write_binary_le(&mz, (uint8_t*)zbuf[0], p->hw * 8);
    kron_accum(p, zbuf[0], i, j, acc);
  }
  kron_prefix(a, p->n, pa); kron_prefix(b, p->n, pb);
  kron_finish(p, acc, pa, pb, out);
}

// ---- S_LIMB: operands live in mpi structs whose limbs the packer writes directly; the product limbs are read in place
static mbedtls_mpi ma[KRON_MAX_CHUNKS], mb[KRON_MAX_CHUNKS];
static bool l_init = false;
static void l_setup(int hw) {
  if (!l_init) { for (int i = 0; i < KRON_MAX_CHUNKS; i++) { mbedtls_mpi_init(&ma[i]); mbedtls_mpi_init(&mb[i]); } m_setup(); l_init = true; }
  mbedtls_mpi_grow(&mz, 2 * hw + 4);
}
static void mul_limb(const KronPlan* p, const int32_t* a, const int32_t* b, int32_t* out) {
  l_setup(p->hw);
  memset(acc, 0, sizeof acc);
  for (int i = 0; i < p->c; i++) {
    mbedtls_mpi_grow(&ma[i], p->hw); mbedtls_mpi_grow(&mb[i], p->hw);
    memset(ma[i].p, 0, ma[i].n * 4); memset(mb[i].p, 0, mb[i].n * 4); ma[i].s = 1; mb[i].s = 1;
    kron_pack(p, a, p->oa, i, (uint32_t*)ma[i].p); kron_pack(p, b, p->ob, i, (uint32_t*)mb[i].p);
  }
  for (int i = 0; i < p->c; i++) for (int j = 0; j < p->c; j++) {
    memset(mz.p, 0, mz.n * 4);
    mbedtls_mpi_mul_mpi(&mz, &ma[i], &mb[j]);
    mz.p[2 * p->hw] = mz.p[2 * p->hw + 1] = mz.p[2 * p->hw + 2] = 0;
    kron_accum(p, (const uint32_t*)mz.p, i, j, acc);
  }
  kron_prefix(a, p->n, pa); kron_prefix(b, p->n, pb);
  kron_finish(p, acc, pa, pb, out);
}

// ---- S_DRIVER
static void mul_driver(const KronPlan* p, const int32_t* a, const int32_t* b, int32_t* out) {
  const uint32_t hw = p->hw;
  memset(acc, 0, sizeof acc); memset(zbuf[0] + 2 * hw, 0, 16);
  for (int i = 0; i < p->c; i++) { kron_pack(p, a, p->oa, i, A + i * hw); kron_pack(p, b, p->ob, i, B + i * hw); }
  for (int i = 0; i < p->c; i++) for (int j = 0; j < p->c; j++) {
    mpihw::load(A + i * hw, hw, B + j * hw, hw, hw); mpihw::start(hw); mpihw::wait(); mpihw::read_z_fast(zbuf[0], 2 * hw);
    kron_accum(p, zbuf[0], i, j, acc);
  }
  kron_prefix(a, p->n, pa); kron_prefix(b, p->n, pb);
  kron_finish(p, acc, pa, pb, out);
}

void polymul_driver_profiled(const KronPlan* p, const int32_t* a, const int32_t* b, int32_t* out) {
  const uint32_t hw = p->hw; uint32_t t0, t1;
  memset(&g_prof, 0, sizeof g_prof);
  memset(acc, 0, sizeof acc); memset(zbuf[0] + 2 * hw, 0, 16);
  t0 = cyc();
  for (int i = 0; i < p->c; i++) { kron_pack(p, a, p->oa, i, A + i * hw); kron_pack(p, b, p->ob, i, B + i * hw); }
  g_prof.pack = cyc() - t0;
  for (int i = 0; i < p->c; i++) for (int j = 0; j < p->c; j++) {
    t0 = cyc(); mpihw::load(A + i * hw, hw, B + j * hw, hw, hw); t1 = cyc(); g_prof.load += t1 - t0;
    mpihw::start(hw); mpihw::wait(); t0 = cyc(); g_prof.compute += t0 - t1;
    mpihw::read_z_fast(zbuf[0], 2 * hw); t1 = cyc(); g_prof.read += t1 - t0;
    kron_accum(p, zbuf[0], i, j, acc); g_prof.accum += cyc() - t1;
  }
  t0 = cyc(); kron_prefix(a, p->n, pa); kron_prefix(b, p->n, pb); g_prof.prefix = cyc() - t0;
  t0 = cyc(); kron_finish(p, acc, pa, pb, out); g_prof.finish = cyc() - t0;
}

// ---- S_OVERLAP
static void mul_overlap(const KronPlan* p, const int32_t* a, const int32_t* b, int32_t* out) {
  const uint32_t hw = p->hw; const int c = p->c, T = c * c;
  memset(acc, 0, sizeof acc); memset(zbuf[0] + 2 * hw, 0, 16); memset(zbuf[1] + 2 * hw, 0, 16);
  int packed_a = 0, packed_b = 0;                                       // chunks 0 .. packed-1 are built
  auto need = [&](int t) { const int i = t / c, j = t % c;
    while (packed_a <= i) { kron_pack(p, a, p->oa, packed_a, A + packed_a * hw); packed_a++; }
    while (packed_b <= j) { kron_pack(p, b, p->ob, packed_b, B + packed_b * hw); packed_b++; } };
  auto issue = [&](int t) { const int i = t / c, j = t % c; mpihw::load(A + i * hw, hw, B + j * hw, hw, hw); mpihw::start(hw); };
  need(0); issue(0);
  for (int t = 0; t < T; t++) {
    if (t + 1 < T) need(t + 1);                                         // CPU work while multiply t runs
    if (t == 0) { kron_prefix(a, p->n, pa); kron_prefix(b, p->n, pb); }
    if (t > 0) kron_accum(p, zbuf[(t - 1) & 1], (t - 1) / c, (t - 1) % c, acc);
    mpihw::wait(); mpihw::read_z_fast(zbuf[t & 1], 2 * hw);
    if (t + 1 < T) issue(t + 1);
  }
  kron_accum(p, zbuf[(T - 1) & 1], (T - 1) / c, (T - 1) % c, acc);
  kron_finish(p, acc, pa, pb, out);
}

kron_mul_fn polymul_for_stage(int s) {
  switch (s) { case S_MBEDTLS: return mul_mbedtls; case S_LIMB: return mul_limb; case S_DRIVER: return mul_driver; default: return mul_overlap; }
}
