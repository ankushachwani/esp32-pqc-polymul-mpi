#include "kron.h"
#include <string.h>

static int bitlen64(uint64_t x) { int b = 0; while (x) { b++; x >>= 1; } return b; }
static uint64_t absmax(int32_t lo, int32_t hi) { const int64_t a = lo < 0 ? -(int64_t)lo : lo, b = hi < 0 ? -(int64_t)hi : hi; return (uint64_t)(a > b ? a : b); }

int kron_plan(const KronCase* cs, int c, KronPlan* p) { return kron_plan_terms(cs, c, 1, p); }

int kron_plan_terms(const KronCase* cs, int c, int terms, KronPlan* p) {
  if (c < 1 || c > KRON_MAX_CHUNKS) return -1;
  p->terms = terms; p->cs = cs; p->n = cs->n; p->c = c; p->m = (cs->n + c - 1) / c;
  p->oa = cs->amin < 0 ? (uint32_t)-cs->amin : 0; p->ob = cs->bmin < 0 ? (uint32_t)-cs->bmin : 0;
  const uint64_t da = (uint64_t)((int64_t)cs->amax + p->oa), db = (uint64_t)((int64_t)cs->bmax + p->ob);
  p->w = bitlen64((uint64_t)terms * p->m * da * db); if (p->w < 1) p->w = 1;
  const uint32_t words = ((uint32_t)p->m * p->w + 31) / 32;
  p->hw = (words + 15) & ~15u;
  if (p->hw > KRON_MAX_WORDS) return -1;
  if (p->w > 57) return -1;                       // slot extraction reads at most three words
  // largest magnitude any accumulator slot, prefix sum or shift term can reach; two of them are subtracted, so stay below 2^30
  const uint64_t n = (uint64_t)cs->n, ma = absmax(cs->amin, cs->amax), mb = absmax(cs->bmin, cs->bmax);
  const uint64_t bound = (uint64_t)terms * (n * da * db + p->ob * n * ma + p->oa * n * mb + (uint64_t)p->oa * p->ob * n);
  p->narrow = bound < (1ull << 30) && p->w <= 32;
  return 0;
}

void kron_pack(const KronPlan* p, const int32_t* v, uint32_t off, int chunk, uint32_t* out) {
  memset(out, 0, p->hw * 4);
  const int base = chunk * p->m;
  const int cnt = base + p->m <= p->n ? p->m : p->n - base;
  uint32_t pos = 0;
  for (int i = 0; i < cnt; i++, pos += p->w) {
    const uint32_t x = (uint32_t)(v[base + i] + (int32_t)off), sh = pos & 31, wi = pos >> 5;
    out[wi] |= x << sh;
    if (sh) { const uint32_t hi = x >> (32 - sh); if (hi) out[wi + 1] |= hi; }
  }
}

uint32_t kron_acc_bytes(const KronPlan* p) { return (uint32_t)(2 * p->c * p->m) * (p->narrow ? 4 : 8); }

static inline uint64_t get_slot(const uint32_t* z, uint32_t pos, int w) {
  const uint32_t wi = pos >> 5, sh = pos & 31;
  uint64_t x = (((uint64_t)z[wi + 1] << 32) | z[wi]) >> sh;
  if (w + (int)sh > 64) x |= (uint64_t)z[wi + 2] << (64 - sh);
  return x & ((1ULL << w) - 1);
}

void kron_accum(const KronPlan* p, const uint32_t* z, int ci, int cj, void* acc) {
  const int cnt = 2 * p->m - 1; const int w = p->w;
  if (p->narrow) {
    uint32_t* d = (uint32_t*)acc + (ci + cj) * p->m;
    const uint32_t mask = w == 32 ? 0xFFFFFFFFu : (1u << w) - 1;
    uint32_t pos = 0;
    for (int k = 0; k < cnt; k++, pos += w) {
      const uint32_t wi = pos >> 5, sh = pos & 31;
      const uint32_t x = (z[wi] >> sh) | ((z[wi + 1] << 1) << (31 - sh));      // two shifts so that sh = 0 needs no special case
      d[k] += x & mask;
    }
  } else {
    uint64_t* d = (uint64_t*)acc + (ci + cj) * p->m;
    uint32_t pos = 0;
    for (int k = 0; k < cnt; k++, pos += w) d[k] += get_slot(z, pos, w);
  }
}

void kron_prefix(const KronPlan* p, const int32_t* a, const int32_t* b, void* pa, void* pb) {
  const int n = p->n;
  if (p->narrow) {
    if (p->ob) { int32_t* r = (int32_t*)pa; int32_t s = 0; r[0] = 0; for (int i = 0; i < n; i++) { s += a[i]; r[i + 1] = s; } }
    if (p->oa) { int32_t* r = (int32_t*)pb; int32_t s = 0; r[0] = 0; for (int i = 0; i < n; i++) { s += b[i]; r[i + 1] = s; } }
  } else {
    if (p->ob) { int64_t* r = (int64_t*)pa; int64_t s = 0; r[0] = 0; for (int i = 0; i < n; i++) { s += a[i]; r[i + 1] = s; } }
    if (p->oa) { int64_t* r = (int64_t*)pb; int64_t s = 0; r[0] = 0; for (int i = 0; i < n; i++) { s += b[i]; r[i + 1] = s; } }
  }
}

void kron_prefix1(const KronPlan* p, const int32_t* v, void* pre) {
  const int n = p->n;
  if (p->narrow) { int32_t* r = (int32_t*)pre; int32_t s = 0; r[0] = 0; for (int i = 0; i < n; i++) { s += v[i]; r[i + 1] = s; } }
  else { int64_t* r = (int64_t*)pre; int64_t s = 0; r[0] = 0; for (int i = 0; i < n; i++) { s += v[i]; r[i + 1] = s; } }
}

static inline int32_t red64(int64_t d, uint32_t q) {
  int32_t r;
  if (d >= INT32_MIN && d <= INT32_MAX) r = (int32_t)d % (int32_t)q;
  else r = (int32_t)(d % (int64_t)q);
  return r < 0 ? r + (int32_t)q : r;
}

// Slot k of the linear product after the shifts are removed. With i the index into a and j = k - i into b:
//   k < n:  i = 0..k            window sum of a = pa[k+1],            of b = pb[k+1],            terms k+1
//   k >= n: i = k-n+1..n-1      window sum of a = pa[n] - pa[k-n+1],  of b = pb[n] - pb[k-n+1],  terms 2n-1-k
#define UNSHIFT_LOOP(T, ACC, PA, PB, P)                                                                                     \
  for (int k = 0; k < 2 * n - 1; k++) {                                                                                      \
    T sa = 0, sb = 0, cnt;                                                                                                   \
    if (k < n) { if (ob) sa = PA[k + 1]; if (oa) sb = PB[k + 1]; cnt = k + 1; }                                              \
    else       { if (ob) sa = PA[n] - PA[k - n + 1]; if (oa) sb = PB[n] - PB[k - n + 1]; cnt = 2 * n - 1 - k; }              \
    P[k] = ACC[k] - (T)ob * sa - (T)oa * sb - (T)oa * (T)ob * cnt * (T)terms;                                                           \
  }

static union { int32_t n32[2 * KRON_MAX_N]; int64_t n64[2 * KRON_MAX_N]; } scratch;      // shifted-out linear product, shared by both paths

void kron_finish(const KronPlan* p, const void* acc, const void* pa, const void* pb, int32_t* out) {
  const int n = p->n; const uint32_t q = p->cs->q; const uint32_t oa = p->oa, ob = p->ob; const int terms = p->terms;
  if (p->narrow) {
    int32_t* P = scratch.n32;
    const uint32_t* A = (const uint32_t*)acc; const int32_t* PA = (const int32_t*)pa; const int32_t* PB = (const int32_t*)pb;
    UNSHIFT_LOOP(int32_t, A, PA, PB, P)
    P[2 * n - 1] = 0;
    for (int k = 0; k < n; k++) { int32_t r = (P[k] - P[k + n]) % (int32_t)q; out[k] = r < 0 ? r + (int32_t)q : r; }
  } else {
    int64_t* P = scratch.n64;
    const uint64_t* A = (const uint64_t*)acc; const int64_t* PA = (const int64_t*)pa; const int64_t* PB = (const int64_t*)pb;
    UNSHIFT_LOOP(int64_t, A, PA, PB, P)
    P[2 * n - 1] = 0;
    for (int k = 0; k < n; k++) out[k] = red64(P[k] - P[k + n], q);
  }
}

void kron_ref_negacyclic(int n, uint32_t q, const int32_t* a, const int32_t* b, int32_t* out) {
  int64_t t[2 * KRON_MAX_N + 8];
  for (int i = 0; i < 2 * n; i++) t[i] = 0;
  for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) t[i + j] += (int64_t)a[i] * b[j];
  for (int k = 0; k < n; k++) out[k] = red64(t[k] - t[k + n], q);
}

void kron_sw_mul(const uint32_t* x, const uint32_t* y, uint32_t hw, uint32_t* z) {
  memset(z, 0, 2 * hw * 4);
  for (uint32_t i = 0; i < hw; i++) {
    uint64_t carry = 0;
    for (uint32_t j = 0; j < hw; j++) { const uint64_t t = (uint64_t)x[i] * y[j] + z[i + j] + carry; z[i + j] = (uint32_t)t; carry = t >> 32; }
    z[i + hw] = (uint32_t)carry;
  }
}

void kron_sumvec(const int32_t* const* v, int k, int n, int32_t* out) {
  for (int i = 0; i < n; i++) { int32_t s = 0; for (int t = 0; t < k; t++) s += v[t][i]; out[i] = s; }
}

#ifndef KRON_NO_SW
void kron_mul_sw_sum(const KronPlan* p, const int32_t* const* a, const int32_t* const* b, int32_t* out) {
  static uint32_t A[4][KRON_MAX_CHUNKS * KRON_MAX_WORDS], B[4][KRON_MAX_CHUNKS * KRON_MAX_WORDS], z[KRON_ZWORDS], zs[KRON_ZWORDS];
  static uint64_t acc[KRON_MAX_ACC]; static int64_t pa[KRON_MAX_N + 1], pb[KRON_MAX_N + 1]; static int32_t sa[KRON_MAX_N], sb[KRON_MAX_N];
  memset(acc, 0, kron_acc_bytes(p));
  for (int t = 0; t < p->terms; t++) for (int i = 0; i < p->c; i++) { kron_pack(p, a[t], p->oa, i, A[t] + i * p->hw); kron_pack(p, b[t], p->ob, i, B[t] + i * p->hw); }
  for (int i = 0; i < p->c; i++) for (int j = 0; j < p->c; j++) {
    memset(zs, 0, sizeof zs);
    for (int t = 0; t < p->terms; t++) {
      kron_sw_mul(A[t] + i * p->hw, B[t] + j * p->hw, p->hw, z);
      uint64_t carry = 0; for (uint32_t k = 0; k < 2 * p->hw; k++) { const uint64_t v = (uint64_t)zs[k] + z[k] + carry; zs[k] = (uint32_t)v; carry = v >> 32; }
    }
    kron_accum(p, zs, i, j, acc);
  }
  kron_sumvec(a, p->terms, p->n, sa); kron_sumvec(b, p->terms, p->n, sb);
  kron_prefix(p, sa, sb, pa, pb);
  kron_finish(p, acc, pa, pb, out);
}

void kron_mul_sw(const KronPlan* p, const int32_t* a, const int32_t* b, int32_t* out) {
  static uint32_t A[KRON_MAX_CHUNKS * KRON_MAX_WORDS], B[KRON_MAX_CHUNKS * KRON_MAX_WORDS], z[KRON_ZWORDS];
  static uint64_t acc[KRON_MAX_ACC]; static int64_t pa[KRON_MAX_N + 1], pb[KRON_MAX_N + 1];
  memset(acc, 0, kron_acc_bytes(p));
  for (int i = 0; i < p->c; i++) { kron_pack(p, a, p->oa, i, A + i * p->hw); kron_pack(p, b, p->ob, i, B + i * p->hw); }
  for (int i = 0; i < p->c; i++) for (int j = 0; j < p->c; j++) {
    kron_sw_mul(A + i * p->hw, B + j * p->hw, p->hw, z); memset(z + 2 * p->hw, 0, 4 * 4); kron_accum(p, z, i, j, acc);
  }
  kron_prefix(p, a, b, pa, pb);
  kron_finish(p, acc, pa, pb, out);
}
#endif
