// Falcon-512 pieces. vrfy.c is included here (and not compiled on its own) because the mq_* NTT helpers are static in it.
// falcon_verify_var is a copy of PQClean's do_verify with the product h*s2 replaceable.
#include <string.h>
#include "schemes.h"
#include "falcon-512/clean/vrfy.c"
#include "falcon-512/clean/api.h"

#define NONCELEN 40
#define FP(x) PQCLEAN_FALCON512_CLEAN_##x
#define LOGN 9
#define FN 512

// ---- one-product baseline (h_std, h_ntt = NTT(h) in Montgomery form, s2 reduced to [0, q))
static uint16_t fh[FN], fhn[FN], fs[FN], fr[FN], fw[FN];
static void fal_load(const int32_t* a, const int32_t* b) {
  for (int i = 0; i < FN; i++) { fh[i] = (uint16_t)a[i]; int32_t s = b[i]; fs[i] = (uint16_t)(s < 0 ? s + Q : s); }
  memcpy(fhn, fh, sizeof fh); FP(to_ntt_monty)(fhn, LOGN);
}
static void fal_full(void) {
  uint16_t hc[FN]; memcpy(hc, fh, sizeof hc); FP(to_ntt_monty)(hc, LOGN);
  memcpy(fr, fs, sizeof fr); mq_NTT(fr, LOGN); mq_poly_montymul_ntt(fr, hc, LOGN); mq_iNTT(fr, LOGN);
}
static void fal_pre(void) { memcpy(fr, fs, sizeof fr); mq_NTT(fr, LOGN); mq_poly_montymul_ntt(fr, fhn, LOGN); mq_iNTT(fr, LOGN); }
static void fal_get(int32_t* out) { for (int i = 0; i < FN; i++) out[i] = fr[i]; }
static void fal_ntt(void) { memcpy(fw, fs, sizeof fw); mq_NTT(fw, LOGN); }
static void fal_pw(void) { memcpy(fr, fs, sizeof fr); mq_poly_montymul_ntt(fr, fhn, LOGN); }
static void fal_intt(void) { memcpy(fw, fr, sizeof fw); mq_iNTT(fw, LOGN); }
const CpuPoly CPU_FALCON = { fal_load, fal_full, fal_pre, fal_get, fal_ntt, fal_pw, fal_intt };

// ---- whole scheme
int falcon_keypair(uint8_t* pk, uint8_t* sk) { return FP(crypto_sign_keypair)(pk, sk); }
int falcon_sign(uint8_t* sig, size_t* siglen, const uint8_t* m, size_t mlen, const uint8_t* sk) { return FP(crypto_sign_signature)(sig, siglen, m, mlen, sk); }
int falcon_verify_ref(const uint8_t* sig, size_t siglen, const uint8_t* m, size_t mlen, const uint8_t* pk) { return FP(crypto_sign_verify)(sig, siglen, m, mlen, pk); }

int falcon_verify_var(const uint8_t* sig, size_t siglen, const uint8_t* m, size_t mlen, const uint8_t* pk, kron_mul_fn mul, const KronPlan* plan) {
  union { uint8_t b[2 * FN]; uint64_t u64; } tmp;
  uint16_t h[FN], hm[FN];
  int16_t s2[FN];
  inner_shake256_context sc;
  if (siglen < 1 + NONCELEN || sig[0] != 0x30 + 9) return -1;
  const uint8_t* nonce = sig + 1; const uint8_t* sigbuf = sig + 1 + NONCELEN; const size_t sigbuflen = siglen - 1 - NONCELEN;
  if (pk[0] != 0x00 + 9) return -1;
  if (FP(modq_decode)(h, LOGN, pk + 1, FAL_PK - 1) != FAL_PK - 1) return -1;
  if (!mul) FP(to_ntt_monty)(h, LOGN);
  if (sigbuflen == 0) return -1;
  if (FP(comp_decode)(s2, LOGN, sigbuf, sigbuflen) != sigbuflen) return -1;

  inner_shake256_init(&sc);
  inner_shake256_inject(&sc, nonce, NONCELEN);
  inner_shake256_inject(&sc, m, mlen);
  inner_shake256_flip(&sc);
  FP(hash_to_point_ct)(&sc, hm, LOGN, tmp.b);
  inner_shake256_ctx_release(&sc);

  if (!mul) return FP(verify_raw)(hm, s2, h, LOGN, tmp.b) ? 0 : -1;

  // -s1 = h*s2 - c0 mod q, normalised to [-q/2, q/2], then the same norm test
  int32_t a[FN], b[FN], prod[FN];
  int16_t s1[FN];
  for (int i = 0; i < FN; i++) { a[i] = h[i]; b[i] = s2[i]; }
  mul(plan, a, b, prod);
  for (int i = 0; i < FN; i++) {
    int32_t w = prod[i] - (int32_t)hm[i];
    if (w < 0) w += Q;
    if (w > (Q >> 1)) w -= Q;
    s1[i] = (int16_t)w;
  }
  return FP(is_short)(s1, s2, LOGN) ? 0 : -1;
}
