#include "kron.h"
#include <string.h>

static int bitlen64(uint64_t x) { int b = 0; while (x) { b++; x >>= 1; } return b; }

int kron_plan(const KronCase* cs, int c, KronPlan* p) {
  if (c < 1 || c > KRON_MAX_CHUNKS) return -1;
  p->cs = cs; p->n = cs->n; p->c = c; p->m = (cs->n + c - 1) / c;
  p->oa = cs->amin < 0 ? (uint32_t)-cs->amin : 0; p->ob = cs->bmin < 0 ? (uint32_t)-cs->bmin : 0;
  const uint64_t da = (uint64_t)((int64_t)cs->amax + p->oa), db = (uint64_t)((int64_t)cs->bmax + p->ob);
  p->w = bitlen64((uint64_t)p->m * da * db); if (p->w < 1) p->w = 1;
  const uint32_t words = ((uint32_t)p->m * p->w + 31) / 32;
  p->hw = (words + 15) & ~15u;
  if (p->hw > KRON_MAX_WORDS) return -1;
  if (p->w > 57) return -1;                       // slot extraction reads at most three words
  return 0;
}

void kron_pack(const KronPlan* p, const int32_t* v, uint32_t off, int chunk, uint32_t* out) {
  memset(out, 0, p->hw * 4);
  const int base = chunk * p->m;
  uint32_t pos = 0;
  for (int i = 0; i < p->m; i++, pos += p->w) {
    const int idx = base + i;
    if (idx >= p->n) break;
    const uint64_t x = (uint64_t)(uint32_t)(v[idx] + (int32_t)off) << (pos & 31);
    out[pos >> 5] |= (uint32_t)x;
    if (x >> 32) out[(pos >> 5) + 1] |= (uint32_t)(x >> 32);
  }
}

static inline uint64_t get_slot(const uint32_t* z, uint32_t pos, int w) {
  const uint32_t wi = pos >> 5, sh = pos & 31;
  uint64_t x = (((uint64_t)z[wi + 1] << 32) | z[wi]) >> sh;
  if (w + (int)sh > 64) x |= (uint64_t)z[wi + 2] << (64 - sh);
  return x & ((1ULL << w) - 1);
}

void kron_accum(const KronPlan* p, const uint32_t* z, int ci, int cj, uint64_t* acc) {
  uint64_t* d = acc + (ci + cj) * p->m;
  uint32_t pos = 0;
  for (int k = 0; k < 2 * p->m - 1; k++, pos += p->w) d[k] += get_slot(z, pos, p->w);
}

void kron_prefix(const int32_t* v, int n, int64_t* pre) {
  int64_t s = 0; pre[0] = 0;
  for (int i = 0; i < n; i++) { s += v[i]; pre[i + 1] = s; }
}

// slot k of the linear product after the shifts are removed
static inline int64_t unshifted(const KronPlan* p, const uint64_t* acc, const int64_t* pa, const int64_t* pb, int k) {
  const int n = p->n;
  const int lo = k - n + 1 > 0 ? k - n + 1 : 0, hi = k < n - 1 ? k : n - 1;
  int64_t v = (int64_t)acc[k];
  if (p->ob) v -= (int64_t)p->ob * (pa[hi + 1] - pa[lo]);
  if (p->oa) v -= (int64_t)p->oa * (pb[k - lo + 1] - pb[k - hi]);
  if (p->oa && p->ob) v -= (int64_t)p->oa * p->ob * (hi - lo + 1);
  return v;
}

static inline int32_t red(int64_t d, uint32_t q) {
  int32_t r;
  if (d >= INT32_MIN && d <= INT32_MAX) r = (int32_t)d % (int32_t)q;       // the 32-bit divide is much cheaper than __moddi3 on the LX6
  else r = (int32_t)(d % (int64_t)q);
  return r < 0 ? r + (int32_t)q : r;
}

void kron_finish(const KronPlan* p, const uint64_t* acc, const int64_t* pa, const int64_t* pb, int32_t* out) {
  const int n = p->n;
  for (int k = 0; k < n; k++) {
    int64_t d = unshifted(p, acc, pa, pb, k);
    if (k + n <= 2 * n - 2) d -= unshifted(p, acc, pa, pb, k + n);
    out[k] = red(d, p->cs->q);
  }
}

void kron_ref_negacyclic(int n, uint32_t q, const int32_t* a, const int32_t* b, int32_t* out) {
  int64_t t[2 * KRON_MAX_ACC / 2 + 8];
  for (int i = 0; i < 2 * n; i++) t[i] = 0;
  for (int i = 0; i < n; i++) for (int j = 0; j < n; j++) t[i + j] += (int64_t)a[i] * b[j];
  for (int k = 0; k < n; k++) out[k] = red(t[k] - t[k + n], q);
}

void kron_sw_mul(const uint32_t* x, const uint32_t* y, uint32_t hw, uint32_t* z) {
  memset(z, 0, 2 * hw * 4);
  for (uint32_t i = 0; i < hw; i++) {
    uint64_t carry = 0;
    for (uint32_t j = 0; j < hw; j++) { const uint64_t t = (uint64_t)x[i] * y[j] + z[i + j] + carry; z[i + j] = (uint32_t)t; carry = t >> 32; }
    z[i + hw] = (uint32_t)carry;
  }
}

void kron_mul_sw(const KronPlan* p, const int32_t* a, const int32_t* b, int32_t* out) {
  static uint32_t A[KRON_MAX_CHUNKS * KRON_MAX_WORDS], B[KRON_MAX_CHUNKS * KRON_MAX_WORDS], z[KRON_ZWORDS];
  static uint64_t acc[KRON_MAX_ACC]; static int64_t pa[513], pb[513];
  memset(acc, 0, sizeof acc);
  for (int i = 0; i < p->c; i++) { kron_pack(p, a, p->oa, i, A + i * p->hw); kron_pack(p, b, p->ob, i, B + i * p->hw); }
  for (int i = 0; i < p->c; i++) for (int j = 0; j < p->c; j++) {
    kron_sw_mul(A + i * p->hw, B + j * p->hw, p->hw, z); memset(z + 2 * p->hw, 0, 4 * 4); kron_accum(p, z, i, j, acc);
  }
  kron_prefix(a, p->n, pa); kron_prefix(b, p->n, pb);
  kron_finish(p, acc, pa, pb, out);
}
