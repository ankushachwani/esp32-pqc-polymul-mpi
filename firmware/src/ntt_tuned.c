#include "ntt_tuned.h"
#include <string.h>
#if defined(ESP_PLATFORM) || defined(ARDUINO)
#include "esp_attr.h"
#else
#define IRAM_ATTR
#endif

static uint32_t powmod(uint32_t b, uint32_t e, uint32_t q) { uint64_t r = 1, x = b % q; while (e) { if (e & 1) r = r * x % q; x = x * x % q; e >>= 1; } return (uint32_t)r; }
static int brv(int x, int bits) { int r = 0; for (int i = 0; i < bits; i++) r |= ((x >> i) & 1) << (bits - 1 - i); return r; }
static int32_t to_mont(uint32_t x, uint32_t q) { return (int32_t)((uint64_t)x * (((1ull << 32)) % q) % q); }

void tntt_init(TNtt* t, int kind) {
  memset(t, 0, sizeof *t); t->kind = kind;
  uint32_t q, psi; int zl;
  if (kind == TN_KEM) { q = 3329; psi = 17; t->n = 256; t->layers = 7; zl = 7; }
  else if (kind == TN_DSA) { q = 8380417; psi = 1753; t->n = 256; t->layers = 8; zl = 8; }
  else {
    q = 12289; t->n = 512; t->layers = 9; zl = 9; psi = 0;
    for (uint32_t g = 2; g < 100; g++) { const uint32_t r = powmod(g, (q - 1) / 1024, q); if (powmod(r, 512, q) == q - 1) { psi = r; break; } }
  }
  t->q = (int32_t)q;
  uint32_t inv = q; for (int i = 0; i < 5; i++) inv *= 2 - q * inv;          // q^-1 mod 2^32 by Newton
  t->qinv = (int32_t)inv;
  const int cnt = 1 << zl;
  for (int i = 0; i < cnt && i < 512; i++) t->zeta[i] = to_mont(powmod(psi, (uint32_t)brv(i, zl), q), q);
  const uint32_t ninv = powmod(1u << t->layers, q - 2, q), r2 = (uint32_t)((((1ull << 32) % q) * ((1ull << 32) % q)) % q);
  t->f = (int32_t)((uint64_t)ninv * r2 % q);
}

static inline int32_t mulsh(int32_t a, int32_t b) { return (int32_t)(((int64_t)a * b) >> 32); }
static inline int32_t mont(int32_t a, int32_t b, int32_t q, int32_t qinv) {
  const int32_t lo = a * b, hi = mulsh(a, b), m = lo * qinv;
  return hi - mulsh(m, q);
}

IRAM_ATTR void tntt_fwd(const TNtt* t, int32_t* a) {
  const int32_t q = t->q, qi = t->qinv, n = t->n; int k = 0;
  const int stop = t->kind == TN_KEM ? 2 : 1;
  if (t->kind == TN_KEM) k = 0;
  for (int len = n / 2; len >= stop; len >>= 1) {
    for (int start = 0; start < n; start += 2 * len) {
      const int32_t z = t->zeta[++k];
      int32_t* lo = a + start; int32_t* hi = a + start + len;
      for (int j = 0; j < len; j++) {
        const int32_t y = hi[j], x = lo[j];
        const int32_t tt = mont(z, y, q, qi);
        hi[j] = x - tt; lo[j] = x + tt;
      }
    }
  }
}

IRAM_ATTR void tntt_inv(const TNtt* t, int32_t* a) {
  const int32_t q = t->q, qi = t->qinv, n = t->n; int k = (t->kind == TN_KEM) ? 128 : n;
  const int start_len = t->kind == TN_KEM ? 2 : 1;
  for (int len = start_len; len < n; len <<= 1) {
    for (int start = 0; start < n; start += 2 * len) {
      const int32_t z = t->kind == TN_KEM ? t->zeta[--k] : -t->zeta[--k];
      int32_t* lo = a + start; int32_t* hi = a + start + len;
      for (int j = 0; j < len; j++) {
        const int32_t x = lo[j], y = hi[j];
        lo[j] = x + y;
        hi[j] = mont(z, t->kind == TN_KEM ? y - x : x - y, q, qi);
      }
    }
    if (t->kind == TN_DSA && len == 8) for (int j = 0; j < n; j++) { const int32_t v = a[j]; a[j] = v - ((v + (1 << 22)) >> 23) * q; }   // keep the sums inside 32 bits
  }
  for (int j = 0; j < n; j++) { int32_t v = mont(t->f, a[j], q, qi); a[j] = v + ((v >> 31) & q); }
}

IRAM_ATTR void tntt_mul(const TNtt* t, int32_t* r, const int32_t* a, const int32_t* b) {
  const int32_t q = t->q, qi = t->qinv;
  if (t->kind != TN_KEM) { for (int i = 0; i < t->n; i++) r[i] = mont(a[i], b[i], q, qi); return; }
  for (int i = 0; i < 64; i++) {
    const int32_t z = t->zeta[64 + i];
    for (int h = 0; h < 2; h++) {
      const int o = 4 * i + 2 * h; const int32_t zz = h ? -z : z;
      const int32_t a0 = a[o], a1 = a[o + 1], b0 = b[o], b1 = b[o + 1];
      r[o] = mont(mont(a1, b1, q, qi), zz, q, qi) + mont(a0, b0, q, qi);
      r[o + 1] = mont(a0, b1, q, qi) + mont(a1, b0, q, qi);
    }
  }
}

IRAM_ATTR void tntt_mul_acc(const TNtt* t, int32_t* r, const int32_t* a, const int32_t* b) {
  const int32_t q = t->q, qi = t->qinv;
  if (t->kind != TN_KEM) { for (int i = 0; i < t->n; i++) r[i] += mont(a[i], b[i], q, qi); return; }
  for (int i = 0; i < 64; i++) {
    const int32_t z = t->zeta[64 + i];
    for (int h = 0; h < 2; h++) {
      const int o = 4 * i + 2 * h; const int32_t zz = h ? -z : z;
      const int32_t a0 = a[o], a1 = a[o + 1], b0 = b[o], b1 = b[o + 1];
      r[o] += mont(mont(a1, b1, q, qi), zz, q, qi) + mont(a0, b0, q, qi);
      r[o + 1] += mont(a0, b1, q, qi) + mont(a1, b0, q, qi);
    }
  }
}
