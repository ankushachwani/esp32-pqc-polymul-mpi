// Host-side checks of everything in firmware/src that does not touch the multiplier: packing, unpacking, shift removal, the NTT baselines,
// and the three whole-scheme variants, with a schoolbook big-integer multiply standing in for the MPI unit.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "schemes.h"

void rb_seed(uint64_t s);
static uint64_t st = 88172645463325252ull;
static uint32_t rnd(void) { st ^= st << 13; st ^= st >> 7; st ^= st << 17; return (uint32_t)(st >> 11); }
static int32_t rin(int32_t lo, int32_t hi) { return lo + (int32_t)(rnd() % (uint32_t)(hi - lo + 1)); }
static int fails = 0;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static void fillvec(int32_t* v, int n, int32_t lo, int32_t hi, int mode) {
  for (int i = 0; i < n; i++) v[i] = mode == 0 ? rin(lo, hi) : mode == 1 ? lo : mode == 2 ? hi : (i & 1 ? lo : hi);
}

static int best_c(const KronCase* cs, KronPlan* p) {
  int best = -1, bm = 1 << 30; KronPlan q;
  for (int c = 1; c <= KRON_MAX_CHUNKS; c++) if (kron_plan(cs, c, &q) == 0 && c * c < bm) { bm = c * c; best = c; }
  kron_plan(cs, best, p); return best;
}

static void test_polymul(void) {
  static int32_t a[512], b[512], ref[512], got[512], cpu[512];
  for (int ci = 0; ci < N_CASES; ci++) {
    const KronCase* cs = &KRON_CASES[ci]; const CpuPoly* cp = cpu_poly_for_case(ci);
    int plans = 0, vecs = 0;
    for (int c = 1; c <= KRON_MAX_CHUNKS; c++) {
      KronPlan p; if (kron_plan(cs, c, &p)) continue; plans++;
      for (int t = 0; t < 40; t++) {
        const int ma = t < 30 ? 0 : 1 + (t - 30) % 3, mb = t < 30 ? 0 : 1 + (t - 30) / 3 % 3;
        fillvec(a, cs->n, cs->amin, cs->amax, ma); fillvec(b, cs->n, cs->bmin, cs->bmax, mb);
        kron_ref_negacyclic(cs->n, cs->q, a, b, ref); kron_mul_sw(&p, a, b, got); vecs++;
        CHECK(!memcmp(ref, got, cs->n * 4), "%s c=%d vec %d kron != reference", cs->name, c, t);
        if (c == 1 || t == 0) {
          cp->load(a, b); cp->full(); cp->get(cpu); CHECK(!memcmp(ref, cpu, cs->n * 4), "%s NTT full != reference", cs->name);
          cp->pre(); cp->get(cpu); CHECK(!memcmp(ref, cpu, cs->n * 4), "%s NTT pre != reference", cs->name);
        }
        if (c == 1 || c == 2 || t >= 30) {
          const CpuPoly* tp = cpu_tuned_for_case(ci);
          tp->load(a, b); tp->full(); tp->get(cpu); CHECK(!memcmp(ref, cpu, cs->n * 4), "%s tuned NTT full != reference (c=%d t=%d)", cs->name, c, t);
          tp->pre(); tp->get(cpu); CHECK(!memcmp(ref, cpu, cs->n * 4), "%s tuned NTT pre != reference", cs->name);
        }
      }
    }
    printf("polymul %-15s plans %2d, vectors %4d\n", cs->name, plans, vecs);
  }
}

static void test_kem(void) {
  static uint8_t pk[KEM_PK], sk[KEM_SK], ct[KEM_CT], ss1[32], ss2[32], ss3[32], ss4[32], m1[32], m2[32], m3[32];
  static int32_t s_std[3 * 256];
  KronPlan plan; best_c(&KRON_CASES[CASE_MLKEM_X_CBD], &plan);
  rb_seed(7); kem_keypair(pk, sk); kem_sk_std(sk, s_std);
  for (int i = 0; i < 3 * 256; i++) CHECK(s_std[i] >= -2 && s_std[i] <= 2, "s_std out of range at %d: %d", i, s_std[i]);
  int n = 0;
  for (int t = 0; t < 60; t++) {
    kem_enc(ct, ss1, pk);
    if (t >= 20) { for (int k = 0; k < 1 + t % 5; k++) ct[rnd() % KEM_CT] ^= 1u << (rnd() & 7); }     // implicit-rejection path
    kem_dec_ref(ss2, ct, sk); kem_dec_var(ss3, ct, sk, s_std, NULL, NULL); kem_dec_var(ss4, ct, sk, s_std, kron_mul_sw, &plan);
    CHECK(!memcmp(ss2, ss3, 32), "KEM replica (NTT) != PQClean, t=%d", t); CHECK(!memcmp(ss2, ss4, 32), "KEM kron != PQClean, t=%d", t);
    if (t < 20) CHECK(!memcmp(ss1, ss2, 32), "KEM decaps != encaps secret");
    kem_indcpa_dec_ref(m1, ct, sk); kem_indcpa_dec_var(m2, ct, sk, s_std, NULL, NULL); kem_indcpa_dec_var(m3, ct, sk, s_std, kron_mul_sw, &plan);
    CHECK(!memcmp(m1, m2, 32) && !memcmp(m1, m3, 32), "indcpa_dec differs, t=%d", t); n++;
  }
  printf("ml-kem-768 decaps variants agree on %d ciphertexts (20 valid, 40 corrupted)\n", n);
}

static void test_dsa(void) {
  static uint8_t pk[DSA_PK], sk[DSA_SK], s1[DSA_SIG], s2[DSA_SIG], s3[DSA_SIG]; uint8_t msg[64];
  KronPlan pcs, pct; best_c(&KRON_CASES[CASE_MLDSA_C_S], &pcs); best_c(&KRON_CASES[CASE_MLDSA_C_T0], &pct);
  rb_seed(11); dsa_keypair(pk, sk); int tot = 0, max_att = 0;
  for (int t = 0; t < 25; t++) {
    for (int i = 0; i < 64; i++) msg[i] = (uint8_t)rnd();
    size_t l1, l2, l3; int a2 = 0, a3 = 0;
    rb_seed(100 + t); dsa_sign_ref(s1, &l1, msg, 1 + t * 2, sk);
    rb_seed(100 + t); dsa_sign_var(s2, &l2, msg, 1 + t * 2, sk, NULL, NULL, NULL, &a2);
    rb_seed(100 + t); dsa_sign_var(s3, &l3, msg, 1 + t * 2, sk, kron_mul_sw, &pcs, &pct, &a3);
    CHECK(l1 == l2 && l1 == l3 && !memcmp(s1, s2, l1), "ML-DSA replica (NTT) signature differs, t=%d", t);
    CHECK(!memcmp(s1, s3, l1), "ML-DSA kron signature differs, t=%d", t);
    CHECK(a2 == a3, "attempt counts differ"); if (a3 > max_att) max_att = a3; tot += a3;
    CHECK(dsa_verify(s3, l3, msg, 1 + t * 2, pk) == 0, "ML-DSA signature does not verify, t=%d", t);
  }
  printf("ml-dsa-44 signatures identical for 25 messages (rejection-loop passes: total %d, max %d)\n", tot, max_att);
}

static void test_falcon(void) {
  static uint8_t pk[FAL_PK], sk[FAL_SK], sig[FAL_SIG + 8]; uint8_t msg[48];
  KronPlan plan; best_c(&KRON_CASES[CASE_FALCON_H_S2], &plan);
  rb_seed(21); falcon_keypair(pk, sk); int acc = 0, rej = 0;
  for (int t = 0; t < 8; t++) {
    for (int i = 0; i < 48; i++) msg[i] = (uint8_t)rnd();
    size_t sl; falcon_sign(sig, &sl, msg, 48, sk);
    const int r0 = falcon_verify_ref(sig, sl, msg, 48, pk), r1 = falcon_verify_var(sig, sl, msg, 48, pk, NULL, NULL), r2 = falcon_verify_var(sig, sl, msg, 48, pk, kron_mul_sw, &plan);
    CHECK(r0 == 0 && r1 == 0 && r2 == 0, "Falcon valid signature rejected (%d %d %d)", r0, r1, r2); acc++;
    msg[t % 48] ^= 1;
    const int q0 = falcon_verify_ref(sig, sl, msg, 48, pk), q1 = falcon_verify_var(sig, sl, msg, 48, pk, NULL, NULL), q2 = falcon_verify_var(sig, sl, msg, 48, pk, kron_mul_sw, &plan);
    CHECK(q0 != 0 && q1 != 0 && q2 != 0, "Falcon tampered message accepted"); rej++;
    msg[t % 48] ^= 1; sig[60 + t] ^= 0x04;                                       // damaged signature: the three must agree
    const int d0 = falcon_verify_ref(sig, sl, msg, 48, pk), d1 = falcon_verify_var(sig, sl, msg, 48, pk, NULL, NULL), d2 = falcon_verify_var(sig, sl, msg, 48, pk, kron_mul_sw, &plan);
    CHECK((d0 == 0) == (d1 == 0) && (d0 == 0) == (d2 == 0), "Falcon damaged-signature verdicts differ");
  }
  printf("falcon-512: %d valid signatures accepted, %d tampered messages rejected, all three verifiers agree\n", acc, rej);
}

int main(void) {
  test_polymul(); test_kem(); test_dsa(); test_falcon();
  printf(fails ? "%d FAILURES\n" : "all host tests passed\n", fails);
  return fails != 0;
}
