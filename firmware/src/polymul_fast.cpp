#include <Arduino.h>
#include <stdlib.h>
#include <string.h>
#include "mpi_hw.h"
#include "polymul_fast.h"
#include "polymul_hw.h"
#include "kron_fast.h"

#define FA g_A            // chunk buffers, index = term * c + chunk
#define FB g_B
#define zbuf g_zbuf[0]
#define facc g_acc
#define fpa g_pa
#define fpb g_pb
static uint32_t zacc[4][KRON_ZWORDS];
static int32_t sumA[KRON_MAX_N], sumB[KRON_MAX_N];

Packed* packed_new(const KronPlan* p, const int32_t* b) {
  Packed* x = (Packed*)malloc(sizeof(Packed)); if (!x) return nullptr;
  x->words = (uint32_t*)malloc((size_t)p->c * p->hw * 4);
  x->pre = (p->oa) ? malloc((size_t)(p->n + 1) * (p->narrow ? 4 : 8)) : nullptr;
  if (!x->words || (p->oa && !x->pre)) { free(x->words); free(x->pre); free(x); return nullptr; }
  for (int j = 0; j < p->c; j++) kron_pack(p, b, p->ob, j, x->words + j * p->hw);
  if (p->oa) kron_prefix1(p, b, x->pre);
  return x;
}
void packed_free(Packed* x) { if (x) { free(x->words); free(x->pre); free(x); } }
void* prefix_sum_new(const KronPlan* p, const int32_t* const* b, int terms) {
  if (!p->oa) return nullptr;
  void* r = malloc((size_t)(p->n + 1) * (p->narrow ? 4 : 8)); kron_sumvec(b, terms, p->n, sumB); kron_prefix1(p, sumB, r); return r;
}

void fast_mul(const KronPlan* p, const int32_t* const* a, const int32_t* const* b, const Packed* const* fb, const void* pre_b, int32_t* out, uint32_t opt) {
  const uint32_t hw = p->hw; const int c = p->c, T = p->terms, S = c * T * c;
  const bool pre = opt & OPT_PREPACK, xres = opt & OPT_XRES, fc = opt & OPT_FASTCPU;
  if (T > 1 && c > 4) return;                                          // zacc holds four partial products
  memset(facc, 0, kron_acc_bytes(p)); memset(zbuf + 2 * hw, 0, 16);
  for (int j = 0; j < c; j++) memset(zacc[j & 3] + 2 * hw, 0, 16);
  int cntA[4] = {0, 0, 0, 0}, cntB[4] = {0, 0, 0, 0};
  auto bptr = [&](int t, int j) -> const uint32_t* { return (pre && fb && fb[t]) ? fb[t]->words + j * hw : FB + (t * c + j) * hw; };
  auto need = [&](int s) {
    const int i = s / (T * c), r = s % (T * c), t = r / c, j = r % c;
    while (cntA[t] <= i) { (fc ? kron_pack_fast : kron_pack)(p, a[t], p->oa, cntA[t], FA + (t * c + cntA[t]) * hw); cntA[t]++; }
    if (!(pre && fb && fb[t])) while (cntB[t] <= j) { (fc ? kron_pack_fast : kron_pack)(p, b[t], p->ob, cntB[t], FB + (t * c + cntB[t]) * hw); cntB[t]++; }
  };
  int lastX = -1;
  auto issue = [&](int s) {
    const int i = s / (T * c), r = s % (T * c), t = r / c, j = r % c; const int xid = t * c + i;
    if (!(xres && xid == lastX)) { mpihw::load_x(FA + xid * hw, hw, hw); lastX = xid; }
    mpihw::load_y(bptr(t, j), hw, hw, !(opt & OPT_NOCLEAR) || s == 0); mpihw::start(hw);
  };
  need(0); issue(0);
  const void* pbp = nullptr;
  for (int s = 0; s < S; s++) {
    if (s + 1 < S) need(s + 1);
    if (s == 0) {                                                       // overlapped with the first multiply
      const int32_t* sa = a[0]; const int32_t* sb = b[0];
      if (T > 1) { kron_sumvec(a, T, p->n, sumA); sa = sumA; if (p->oa && !(pre && pre_b)) { kron_sumvec(b, T, p->n, sumB); sb = sumB; } }
      if (p->ob) kron_prefix1(p, sa, fpa);
      if (p->oa) { if (pre && T == 1 && fb && fb[0]) pbp = fb[0]->pre; else if (pre && pre_b) pbp = pre_b; else { kron_prefix1(p, sb, fpb); pbp = fpb; } }
    }
    const int r = s % (T * c), t = r / c, j = r % c, i = s / (T * c);
    uint32_t* z = T == 1 ? zbuf : zacc[j];
    mpihw::wait();
    if (t == 0) mpihw::read_z_fast(z, 2 * hw); else mpihw::read_z_add(z, 2 * hw);
    if (s + 1 < S) issue(s + 1);
    if (t == T - 1) (fc ? kron_accum_fast : kron_accum)(p, z, i, j, facc);
  }
  (fc ? kron_finish_fast : kron_finish)(p, facc, fpa, pbp, out);
}

void fast_mul_shared(const KronPlan* p, const int32_t* a, int nb, const int32_t* const* b, const Packed* const* fb, int32_t* const* out, uint32_t opt) {
  const uint32_t hw = p->hw; const int c = p->c, S = c * nb * c; const uint32_t accw = 2 * c * p->m;
  const bool pre = opt & OPT_PREPACK, xres = opt & OPT_XRES, fc = opt & OPT_FASTCPU;
  if (!(opt & OPT_SHARE) || !p->narrow || nb > 4 || accw * 4 * nb > KRON_MAX_ACC * 8) {                       // fall back to separate products
    const int32_t* aa[1] = {a};
    for (int t = 0; t < nb; t++) { const int32_t* bb[1] = {b[t]}; const Packed* ff[1] = {fb ? fb[t] : nullptr}; fast_mul(p, aa, bb, ff, nullptr, out[t], opt); }
    return;
  }
  memset(facc, 0, accw * 4 * nb); memset(zbuf + 2 * hw, 0, 16);
  int cntA = 0, cntB[4] = {0, 0, 0, 0};
  auto bptr = [&](int t, int j) -> const uint32_t* { return (pre && fb && fb[t]) ? fb[t]->words + j * hw : FB + (t * c + j) * hw; };
  auto need = [&](int s) {
    const int i = s / (nb * c), r = s % (nb * c), t = r / c, j = r % c;
    while (cntA <= i) { (fc ? kron_pack_fast : kron_pack)(p, a, p->oa, cntA, FA + cntA * hw); cntA++; }
    if (!(pre && fb && fb[t])) while (cntB[t] <= j) { (fc ? kron_pack_fast : kron_pack)(p, b[t], p->ob, cntB[t], FB + (t * c + cntB[t]) * hw); cntB[t]++; }
  };
  int lastX = -1;
  auto issue = [&](int s) {
    const int i = s / (nb * c), r = s % (nb * c), t = r / c, j = r % c;
    if (!(xres && i == lastX)) { mpihw::load_x(FA + i * hw, hw, hw); lastX = i; }
    mpihw::load_y(bptr(t, j), hw, hw, !(opt & OPT_NOCLEAR) || s == 0); mpihw::start(hw);
  };
  need(0); issue(0);
  const void* pbp[4] = {nullptr, nullptr, nullptr, nullptr};
  for (int s = 0; s < S; s++) {
    if (s + 1 < S) need(s + 1);
    if (s == 0) {
      if (p->ob) kron_prefix1(p, a, fpa);
      if (p->oa) for (int t = 0; t < nb; t++) { if (pre && fb && fb[t]) pbp[t] = fb[t]->pre; else { /* raw b: needs its own prefix buffer */ pbp[t] = nullptr; } }
    }
    const int i = s / (nb * c), r = s % (nb * c), t = r / c, j = r % c;
    mpihw::wait(); mpihw::read_z_fast(zbuf, 2 * hw);
    if (s + 1 < S) issue(s + 1);
    (fc ? kron_accum_fast : kron_accum)(p, zbuf, i, j, (uint32_t*)facc + t * accw);
    if (i == c - 1 && j == c - 1) {                                     // polynomial t complete: finish it while the unit works on the next one
      if (p->oa && !pbp[t]) { static int32_t pbt[KRON_MAX_N + 1]; kron_prefix1(p, b[t], pbt); (fc ? kron_finish_fast : kron_finish)(p, (uint32_t*)facc + t * accw, fpa, pbt, out[t]); }
      else (fc ? kron_finish_fast : kron_finish)(p, (uint32_t*)facc + t * accw, fpa, pbp[t], out[t]);
    }
  }
}

void fast_sum_unfused(const KronPlan* plan1, int nterms, const int32_t* const* a, const int32_t* const* b, const Packed* const* fb1, int32_t* out, uint32_t opt) {
  static int32_t t1[KRON_MAX_N]; const uint32_t q = plan1->cs->q; const int n = plan1->n;
  for (int t = 0; t < nterms; t++) {
    const int32_t* aa[1] = {a[t]}; const int32_t* bb[1] = {b[t]}; const Packed* ff[1] = {fb1 ? fb1[t] : nullptr};
    int32_t* dst = t == 0 ? out : t1;
    fast_mul(plan1, aa, bb, ff, nullptr, dst, opt);
    if (t) for (int i = 0; i < n; i++) { int32_t v = out[i] + t1[i]; if (v >= (int32_t)q) v -= q; out[i] = v; }
  }
}
