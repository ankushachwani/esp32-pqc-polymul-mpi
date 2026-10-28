// ML-KEM-768 pieces. The PQClean functions are used unchanged; kem_dec_var is a copy of crypto_kem_dec / indcpa_dec with the one product replaced.
#include <string.h>
#include "schemes.h"
#include "ml-kem-768/clean/params.h"
#include "ml-kem-768/clean/poly.h"
#include "ml-kem-768/clean/polyvec.h"
#include "ml-kem-768/clean/kem.h"
#include "ml-kem-768/clean/indcpa.h"
#include "ml-kem-768/clean/reduce.h"
#include "ml-kem-768/clean/verify.h"
#include "ml-kem-768/clean/symmetric.h"
#include "fips202.h"

#define P(x) PQCLEAN_MLKEM768_CLEAN_##x

// ---- one-product baselines
static poly ka, kb, kbh, kw, kr;
static void kem_load(const int32_t* a, const int32_t* b) {
  for (int i = 0; i < KYBER_N; i++) { ka.coeffs[i] = (int16_t)a[i]; kb.coeffs[i] = (int16_t)b[i]; }
  kbh = kb; P(poly_ntt)(&kbh);
}
static void kem_full(void) { poly x = ka, y = kb; P(poly_ntt)(&x); P(poly_ntt)(&y); P(poly_basemul_montgomery)(&kr, &x, &y); P(poly_invntt_tomont)(&kr); P(poly_reduce)(&kr); }
static void kem_pre(void) { poly y = ka; P(poly_ntt)(&y); P(poly_basemul_montgomery)(&kr, &y, &kbh); P(poly_invntt_tomont)(&kr); P(poly_reduce)(&kr); }
static void kem_get(int32_t* out) { for (int i = 0; i < KYBER_N; i++) { int32_t v = kr.coeffs[i]; out[i] = v < 0 ? v + KYBER_Q : v; } }
static void kem_ntt(void) { kw = ka; P(poly_ntt)(&kw); }
static void kem_pw(void) { P(poly_basemul_montgomery)(&kr, &kbh, &kb); }
static void kem_intt(void) { kw = kr; P(poly_invntt_tomont)(&kw); }
const CpuPoly CPU_MLKEM = { kem_load, kem_full, kem_pre, kem_get, kem_ntt, kem_pw, kem_intt };

// ---- whole scheme
int kem_keypair(uint8_t* pk, uint8_t* sk) { return P(crypto_kem_keypair)(pk, sk); }
int kem_enc(uint8_t* ct, uint8_t* ss, const uint8_t* pk) { return P(crypto_kem_enc)(ct, ss, pk); }
int kem_dec_ref(uint8_t* ss, const uint8_t* ct, const uint8_t* sk) { return P(crypto_kem_dec)(ss, ct, sk); }
void kem_indcpa_dec_ref(uint8_t* m, const uint8_t* ct, const uint8_t* sk) { P(indcpa_dec)(m, ct, sk); }

// The stored secret is NTT(s) in [0, q). invntt_tomont returns s * 2^16, so one Montgomery reduction takes the factor back out.
void kem_sk_std(const uint8_t* sk, int32_t* s_std) {
  polyvec s; P(polyvec_frombytes)(&s, sk);
  for (int i = 0; i < KYBER_K; i++) {
    P(poly_invntt_tomont)(&s.vec[i]);
    for (int j = 0; j < KYBER_N; j++) s_std[i * KYBER_N + j] = P(montgomery_reduce)(s.vec[i].coeffs[j]);
  }
}

void kem_indcpa_dec_var(uint8_t* m, const uint8_t* ct, const uint8_t* sk, const int32_t* s_std, kron_mul_fn mul, const KronPlan* plan) {
  polyvec b; poly v, mp;
  P(polyvec_decompress)(&b, ct);
  P(poly_decompress)(&v, ct + KYBER_POLYVECCOMPRESSEDBYTES);
  if (!mul) {
    polyvec skpv; P(polyvec_frombytes)(&skpv, sk);
    P(polyvec_ntt)(&b);
    P(polyvec_basemul_acc_montgomery)(&mp, &skpv, &b);
    P(poly_invntt_tomont)(&mp);
  } else {
    int32_t acc[KYBER_N] = {0}, u[KYBER_N], t[KYBER_N];
    for (int i = 0; i < KYBER_K; i++) {
      for (int j = 0; j < KYBER_N; j++) u[j] = b.vec[i].coeffs[j];
      mul(plan, u, s_std + i * KYBER_N, t);
      for (int j = 0; j < KYBER_N; j++) acc[j] += t[j];
    }
    for (int j = 0; j < KYBER_N; j++) mp.coeffs[j] = (int16_t)(acc[j] % KYBER_Q);
  }
  P(poly_sub)(&mp, &v, &mp);
  P(poly_reduce)(&mp);
  P(poly_tomsg)(m, &mp);
}

int kem_dec_var(uint8_t* ss, const uint8_t* ct, const uint8_t* sk, const int32_t* s_std, kron_mul_fn mul, const KronPlan* plan) {
  uint8_t buf[2 * KYBER_SYMBYTES], kr2[2 * KYBER_SYMBYTES], cmp[KYBER_CIPHERTEXTBYTES + KYBER_SYMBYTES];
  const uint8_t* pk = sk + KYBER_INDCPA_SECRETKEYBYTES;
  kem_indcpa_dec_var(buf, ct, sk, s_std, mul, plan);
  memcpy(buf + KYBER_SYMBYTES, sk + KYBER_SECRETKEYBYTES - 2 * KYBER_SYMBYTES, KYBER_SYMBYTES);
  hash_g(kr2, buf, 2 * KYBER_SYMBYTES);
  P(indcpa_enc)(cmp, buf, pk, kr2 + KYBER_SYMBYTES);
  const int fail = P(verify)(ct, cmp, KYBER_CIPHERTEXTBYTES);
  rkprf(ss, sk + KYBER_SECRETKEYBYTES - KYBER_SYMBYTES, ct);
  P(cmov)(ss, kr2, KYBER_SYMBYTES, (uint8_t)(1 - fail));
  return 0;
}

void kem_indcpa_dec_fn(uint8_t* m, const uint8_t* ct, kem_dot_fn dot, void* ctx) {
  polyvec b; poly v, mp; int32_t u[KYBER_K * KYBER_N], t[KYBER_N];
  P(polyvec_decompress)(&b, ct);
  P(poly_decompress)(&v, ct + KYBER_POLYVECCOMPRESSEDBYTES);
  for (int i = 0; i < KYBER_K; i++) for (int j = 0; j < KYBER_N; j++) u[i * KYBER_N + j] = b.vec[i].coeffs[j];
  dot(ctx, u, t);
  for (int j = 0; j < KYBER_N; j++) mp.coeffs[j] = (int16_t)t[j];
  P(poly_sub)(&mp, &v, &mp);
  P(poly_reduce)(&mp);
  P(poly_tomsg)(m, &mp);
}

int kem_dec_fn(uint8_t* ss, const uint8_t* ct, const uint8_t* sk, kem_dot_fn dot, void* ctx) {
  uint8_t buf[2 * KYBER_SYMBYTES], kr2[2 * KYBER_SYMBYTES], cmp[KYBER_CIPHERTEXTBYTES + KYBER_SYMBYTES];
  const uint8_t* pk = sk + KYBER_INDCPA_SECRETKEYBYTES;
  kem_indcpa_dec_fn(buf, ct, dot, ctx);
  memcpy(buf + KYBER_SYMBYTES, sk + KYBER_SECRETKEYBYTES - 2 * KYBER_SYMBYTES, KYBER_SYMBYTES);
  hash_g(kr2, buf, 2 * KYBER_SYMBYTES);
  P(indcpa_enc)(cmp, buf, pk, kr2 + KYBER_SYMBYTES);
  const int fail = P(verify)(ct, cmp, KYBER_CIPHERTEXTBYTES);
  rkprf(ss, sk + KYBER_SECRETKEYBYTES - KYBER_SYMBYTES, ct);
  P(cmov)(ss, kr2, KYBER_SYMBYTES, (uint8_t)(1 - fail));
  return 0;
}
