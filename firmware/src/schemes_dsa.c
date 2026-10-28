// ML-DSA-44 pieces. dsa_sign_var is a copy of PQClean's crypto_sign_signature_ctx (ctx empty) with the three products by the challenge
// (c*s1, c*s2, c*t0) replaceable; everything else is the PQClean call sequence unchanged.
#include <string.h>
#include "schemes.h"
#include "ml-dsa-44/clean/params.h"
#include "ml-dsa-44/clean/poly.h"
#include "ml-dsa-44/clean/polyvec.h"
#include "ml-dsa-44/clean/packing.h"
#include "ml-dsa-44/clean/sign.h"
#include "ml-dsa-44/clean/symmetric.h"
#include "fips202.h"
#include "randombytes.h"

#define P(x) PQCLEAN_MLDSA44_CLEAN_##x

// ---- one-product baseline
static poly da, db, dbh, dw, dr;
static void dsa_load(const int32_t* a, const int32_t* b) {
  for (int i = 0; i < N; i++) { da.coeffs[i] = a[i]; db.coeffs[i] = b[i]; }
  dbh = db; P(poly_ntt)(&dbh);
}
static void dsa_full(void) { poly x = da, y = db; P(poly_ntt)(&x); P(poly_ntt)(&y); P(poly_pointwise_montgomery)(&dr, &x, &y); P(poly_invntt_tomont)(&dr); P(poly_reduce)(&dr); }
static void dsa_pre(void) { poly y = da; P(poly_ntt)(&y); P(poly_pointwise_montgomery)(&dr, &y, &dbh); P(poly_invntt_tomont)(&dr); P(poly_reduce)(&dr); }
static void dsa_get(int32_t* out) { for (int i = 0; i < N; i++) { int32_t v = dr.coeffs[i] % Q; out[i] = v < 0 ? v + Q : v; } }
static void dsa_ntt(void) { dw = da; P(poly_ntt)(&dw); }
static void dsa_pw(void) { P(poly_pointwise_montgomery)(&dr, &dbh, &db); }
static void dsa_intt(void) { dw = dr; P(poly_invntt_tomont)(&dw); }
const CpuPoly CPU_MLDSA = { dsa_load, dsa_full, dsa_pre, dsa_get, dsa_ntt, dsa_pw, dsa_intt };

// ---- whole scheme
int dsa_keypair(uint8_t* pk, uint8_t* sk) { return P(crypto_sign_keypair)(pk, sk); }
int dsa_sign_ref(uint8_t* sig, size_t* siglen, const uint8_t* m, size_t mlen, const uint8_t* sk) { return P(crypto_sign_signature)(sig, siglen, m, mlen, sk); }
int dsa_verify(const uint8_t* sig, size_t siglen, const uint8_t* m, size_t mlen, const uint8_t* pk) { return P(crypto_sign_verify)(sig, siglen, m, mlen, pk); }

// out = c * v (coefficients in [0, q)), for every polynomial of a vector
static void vec_mul(kron_mul_fn mul, const KronPlan* plan, const poly* c, const poly* v, poly* out, int cnt) {
  for (int i = 0; i < cnt; i++) mul(plan, c->coeffs, v[i].coeffs, out[i].coeffs);
}

static int sign_core(uint8_t* sig, size_t* siglen, const uint8_t* m, size_t mlen, const uint8_t* sk, kron_mul_fn mul, const KronPlan* plan_cs,
                     const KronPlan* plan_ct0, const dsa_ops* ops, int* attempts) {
  unsigned int n;
  uint8_t seedbuf[2 * SEEDBYTES + TRBYTES + RNDBYTES + 2 * CRHBYTES];
  uint8_t *rho, *tr, *key, *mu, *rhoprime, *rnd;
  uint16_t nonce = 0;
  polyvecl mat[K], s1, y, z;
  polyveck t0, s2, w1, w0, h;
  poly cp;
  shake256incctx state;
  const uint8_t mdom[2] = {0, 0};
  int tries = 0;

  rho = seedbuf; tr = rho + SEEDBYTES; key = tr + TRBYTES; rnd = key + SEEDBYTES; mu = rnd + RNDBYTES; rhoprime = mu + CRHBYTES;
  P(unpack_sk)(rho, tr, key, &t0, &s1, &s2, sk);

  shake256_inc_init(&state);
  shake256_inc_absorb(&state, tr, TRBYTES);
  shake256_inc_absorb(&state, mdom, 2);
  shake256_inc_absorb(&state, m, mlen);
  shake256_inc_finalize(&state);
  shake256_inc_squeeze(mu, CRHBYTES, &state);
  shake256_inc_ctx_release(&state);

  randombytes(rnd, RNDBYTES);
  shake256(rhoprime, CRHBYTES, key, SEEDBYTES + RNDBYTES + CRHBYTES);

  P(polyvec_matrix_expand)(mat, rho);
  if (ops) { if (ops->begin) ops->begin(ops->ctx, (const int32_t*)s1.vec, (const int32_t*)s2.vec, (const int32_t*)t0.vec); }
  else if (!mul) { P(polyvecl_ntt)(&s1); P(polyveck_ntt)(&s2); P(polyveck_ntt)(&t0); }

rej:
  tries++;
  P(polyvecl_uniform_gamma1)(&y, rhoprime, nonce++);

  z = y;
  P(polyvecl_ntt)(&z);
  P(polyvec_matrix_pointwise_montgomery)(&w1, mat, &z);
  P(polyveck_reduce)(&w1);
  P(polyveck_invntt_tomont)(&w1);

  P(polyveck_caddq)(&w1);
  P(polyveck_decompose)(&w1, &w0, &w1);
  P(polyveck_pack_w1)(sig, &w1);

  shake256_inc_init(&state);
  shake256_inc_absorb(&state, mu, CRHBYTES);
  shake256_inc_absorb(&state, sig, K * POLYW1_PACKEDBYTES);
  shake256_inc_finalize(&state);
  shake256_inc_squeeze(sig, CTILDEBYTES, &state);
  shake256_inc_ctx_release(&state);
  P(poly_challenge)(&cp, sig);
  if (!mul && !ops) P(poly_ntt)(&cp);

  if (ops) ops->prod(ops->ctx, cp.coeffs, 0, (int32_t*)z.vec);
  else if (!mul) { P(polyvecl_pointwise_poly_montgomery)(&z, &cp, &s1); P(polyvecl_invntt_tomont)(&z); }
  else vec_mul(mul, plan_cs, &cp, s1.vec, z.vec, L);
  P(polyvecl_add)(&z, &z, &y);
  P(polyvecl_reduce)(&z);
  if (P(polyvecl_chknorm)(&z, GAMMA1 - BETA)) goto rej;

  if (ops) ops->prod(ops->ctx, cp.coeffs, 1, (int32_t*)h.vec);
  else if (!mul) { P(polyveck_pointwise_poly_montgomery)(&h, &cp, &s2); P(polyveck_invntt_tomont)(&h); }
  else vec_mul(mul, plan_cs, &cp, s2.vec, h.vec, K);
  P(polyveck_sub)(&w0, &w0, &h);
  P(polyveck_reduce)(&w0);
  if (P(polyveck_chknorm)(&w0, GAMMA2 - BETA)) goto rej;

  if (ops) ops->prod(ops->ctx, cp.coeffs, 2, (int32_t*)h.vec);
  else if (!mul) { P(polyveck_pointwise_poly_montgomery)(&h, &cp, &t0); P(polyveck_invntt_tomont)(&h); }
  else vec_mul(mul, plan_ct0, &cp, t0.vec, h.vec, K);
  P(polyveck_reduce)(&h);
  if (P(polyveck_chknorm)(&h, GAMMA2)) goto rej;

  P(polyveck_add)(&w0, &w0, &h);
  n = P(polyveck_make_hint)(&h, &w0, &w1);
  if (n > OMEGA) goto rej;

  P(pack_sig)(sig, sig, &z, &h);
  *siglen = PQCLEAN_MLDSA44_CLEAN_CRYPTO_BYTES;
  if (attempts) *attempts = tries;
  return 0;
}

int dsa_sign_var(uint8_t* sig, size_t* siglen, const uint8_t* m, size_t mlen, const uint8_t* sk, kron_mul_fn mul, const KronPlan* plan_cs,
                 const KronPlan* plan_ct0, int* attempts) { return sign_core(sig, siglen, m, mlen, sk, mul, plan_cs, plan_ct0, NULL, attempts); }
int dsa_sign_ops(uint8_t* sig, size_t* siglen, const uint8_t* m, size_t mlen, const uint8_t* sk, const dsa_ops* ops, int* attempts) {
  return sign_core(sig, siglen, m, mlen, sk, NULL, NULL, NULL, ops, attempts);
}
void dsa_unpack_secret(const uint8_t* sk, int32_t* s1, int32_t* s2, int32_t* t0) {
  uint8_t rho[SEEDBYTES], tr[TRBYTES], key[SEEDBYTES]; polyveck T0, S2; polyvecl S1;
  P(unpack_sk)(rho, tr, key, &T0, &S1, &S2, sk);
  memcpy(s1, S1.vec, sizeof S1); memcpy(s2, S2.vec, sizeof S2); memcpy(t0, T0.vec, sizeof T0);
}
