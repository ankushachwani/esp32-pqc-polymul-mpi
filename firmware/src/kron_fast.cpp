// Faster pack / unpack / unshift for narrow plans (slot width <= 32, 32-bit accumulators). 32 slots occupy exactly w words, so the bit position of
// slot j inside a group of 32 is a compile-time constant once the width is a template parameter and the group is unrolled: every shift becomes an
// immediate and the extraction needs no shift-amount setup. The generic versions in kron.c stay as the reference (host tests compare the two).
#include <string.h>
#include "kron.h"
#include "kron_fast.h"

template <int W> static void accum_w(const uint32_t* z, uint32_t* d, int cnt) {
  constexpr uint32_t MASK = W == 32 ? 0xFFFFFFFFu : ((1u << W) - 1);
  int k = 0;
  for (; k + 32 <= cnt; k += 32, z += W, d += 32) {
#pragma GCC unroll 32
    for (int j = 0; j < 32; j++) {
      const int wi = (j * W) >> 5, sh = (j * W) & 31;
      uint32_t x = z[wi] >> sh;
      if (sh + W > 32) x |= z[wi + 1] << (32 - sh);
      d[j] += x & MASK;
    }
  }
  uint32_t pos = 0;
  for (; k < cnt; k++, pos += W, d++) {
    const uint32_t wi = pos >> 5, sh = pos & 31;
    *d += ((z[wi] >> sh) | ((z[wi + 1] << 1) << (31 - sh))) & MASK;
  }
}

template <int W> static void pack_w(uint32_t* out, const int32_t* v, int cnt, uint32_t off) {
  int k = 0;
  for (; k + 32 <= cnt; k += 32, v += 32, out += W) {
#pragma GCC unroll 32
    for (int j = 0; j < 32; j++) {
      const int wi = (j * W) >> 5, sh = (j * W) & 31;
      const uint32_t x = (uint32_t)(v[j] + (int32_t)off);
      out[wi] |= x << sh;
      if (sh + W > 32) out[wi + 1] |= x >> (32 - sh);
    }
  }
  uint32_t pos = 0;                                  // fewer than 32 slots left: generic
  for (int t = 0; k < cnt; k++, t++, pos += W) {
    const uint32_t x = (uint32_t)(v[t] + (int32_t)off), sh = pos & 31, wi = pos >> 5;
    out[wi] |= x << sh;
    if (sh) { const uint32_t hi = x >> (32 - sh); if (hi) out[wi + 1] |= hi; }
  }
}

#define WIDTHS(X) X(1) X(2) X(3) X(4) X(5) X(6) X(7) X(8) X(9) X(10) X(11) X(12) X(13) X(14) X(15) X(16) X(17) X(18) X(19) X(20) X(21) X(22) X(23) X(24) X(25) X(26) X(27) X(28) X(29) X(30) X(31) X(32)

extern "C" void kron_accum_fast(const KronPlan* p, const uint32_t* z, int ci, int cj, void* acc) {
  if (!p->narrow) { kron_accum(p, z, ci, cj, acc); return; }
  uint32_t* d = (uint32_t*)acc + (ci + cj) * p->m; const int cnt = 2 * p->m - 1;
  switch (p->w) {
#define X(W) case W: accum_w<W>(z, d, cnt); return;
    WIDTHS(X)
#undef X
  }
}

extern "C" void kron_pack_fast(const KronPlan* p, const int32_t* v, uint32_t off, int chunk, uint32_t* out) {
  if (!p->narrow) { kron_pack(p, v, off, chunk, out); return; }
  memset(out, 0, p->hw * 4);
  const int base = chunk * p->m; const int cnt = base + p->m <= p->n ? p->m : p->n - base;
  switch (p->w) {
#define X(W) case W: pack_w<W>(out, v + base, cnt, off); return;
    WIDTHS(X)
#undef X
  }
}

// Unshift, fold and reduce in one pass (no 2n scratch array): with k < n,
//   d_k = acc[k] - acc[k+n] - ob*(2*pa[k+1] - pa[n]) - oa*(2*pb[k+1] - pb[n]) - terms*oa*ob*(2k + 2 - n)
// all in modular 32-bit arithmetic (the true value fits in 32 bits for narrow plans).
extern "C" void kron_finish_fast(const KronPlan* p, const void* acc, const void* pa, const void* pb, int32_t* out) {
  if (!p->narrow) { kron_finish(p, acc, pa, pb, out); return; }
  const int n = p->n; const int32_t q = (int32_t)p->cs->q; const uint32_t* A = (const uint32_t*)acc; const int32_t* PA = (const int32_t*)pa; const int32_t* PB = (const int32_t*)pb;
  const uint32_t ob = p->ob, oa = p->oa, oo = (uint32_t)p->terms * oa * ob;
  const uint32_t pan = ob ? (uint32_t)PA[n] : 0, pbn = oa ? (uint32_t)PB[n] : 0;
  for (int k = 0; k < n; k++) {
    uint32_t d = A[k] - A[k + n];
    if (ob) d -= ob * (2u * (uint32_t)PA[k + 1] - pan);
    if (oa) d -= oa * (2u * (uint32_t)PB[k + 1] - pbn);
    if (oo) d -= oo * (uint32_t)(2 * k + 2 - n);
    const int32_t r = (int32_t)d % q;
    out[k] = r < 0 ? r + q : r;
  }
}
