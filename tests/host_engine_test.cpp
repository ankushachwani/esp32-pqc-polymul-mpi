// Host test of the optimised engine (polymul_fast.cpp) and its scheme hooks, with the multiplier replaced by tests/mock_mpi_hw.h.
// Checks every switch combination against the O(n^2) reference: single products, fused sums, shared-operand batches.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define PQC_HOST 1
extern "C" {
#include "kron.h"
#include "schemes.h"
}
#include "mock_mpi_hw.h"
#include "polymul_fast.h"

extern "C" { const CpuPoly CPU_MLKEM = {}, CPU_MLDSA = {}, CPU_FALCON = {}; }       // cases.c refers to them; not used here
static uint64_t st = 0x2545F4914F6CDD1Dull;
static uint32_t rnd() { st ^= st << 13; st ^= st >> 7; st ^= st << 17; return (uint32_t)(st >> 11); }
static int32_t rin(int32_t lo, int32_t hi) { return lo + (int32_t)(rnd() % (uint32_t)(hi - lo + 1)); }
static void fillvec(int32_t* v, int n, int32_t lo, int32_t hi, int mode) { for (int i = 0; i < n; i++) v[i] = mode == 0 ? rin(lo, hi) : mode == 1 ? lo : mode == 2 ? hi : (i & 1 ? lo : hi); }
static int fails = 0, checks = 0;
#define CHECK(c, ...) do { checks++; if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

int main() {
  static int32_t a[4][512], b[4][512], ref[4][512], tmp[512], out[4][512];
  for (int ci = 0; ci < N_CASES; ci++) {
    const KronCase* cs = &KRON_CASES[ci]; const int n = cs->n;
    for (int c = 1; c <= 8; c++) {
      KronPlan p1; if (kron_plan(cs, c, &p1)) continue;
      for (unsigned opt = 0; opt < 64; opt++) {
        for (int t = 0; t < 6; t++) {
          const int mode = t < 3 ? 0 : t - 2;
          for (int u = 0; u < 4; u++) { fillvec(a[u], n, cs->amin, cs->amax, mode); fillvec(b[u], n, cs->bmin, cs->bmax, mode == 1 ? 2 : mode == 2 ? 1 : mode); }
          // single product
          Packed* pk = packed_new(&p1, b[0]); const int32_t* aa[1] = {a[0]}; const int32_t* bb[1] = {b[0]}; const Packed* ff[1] = {pk};
          kron_ref_negacyclic(n, cs->q, a[0], b[0], ref[0]); fast_mul(&p1, aa, bb, ff, nullptr, out[0], opt);
          CHECK(!memcmp(ref[0], out[0], n * 4), "single %s c=%d opt=%u mode=%d", cs->name, c, opt, mode);
          // sums of 2 and 3 products, fused and not
          for (int T = 2; T <= 3; T++) {
            KronPlan pT; if (kron_plan_terms(cs, c, T, &pT) || c > 4) continue;
            const int32_t *as[3] = {a[0], a[1], a[2]}, *bs[3] = {b[0], b[1], b[2]}; Packed* pkT[3]; Packed* pk1[3];
            for (int k = 0; k < T; k++) { pkT[k] = packed_new(&pT, b[k]); pk1[k] = packed_new(&p1, b[k]); }
            void* ps = prefix_sum_new(&pT, bs, T);
            memset(ref[1], 0, n * 4); for (int k = 0; k < T; k++) { kron_ref_negacyclic(n, cs->q, a[k], b[k], tmp); for (int i = 0; i < n; i++) ref[1][i] = (ref[1][i] + tmp[i]) % cs->q; }
            if (opt & OPT_FUSE) fast_mul(&pT, as, bs, (const Packed* const*)pkT, ps, out[1], opt); else fast_sum_unfused(&p1, T, as, bs, (const Packed* const*)pk1, out[1], opt);
            CHECK(!memcmp(ref[1], out[1], n * 4), "sum of %d, %s c=%d opt=%u mode=%d", T, cs->name, c, opt, mode);
            for (int k = 0; k < T; k++) { packed_free(pkT[k]); packed_free(pk1[k]); } free(ps);
          }
          // four products with a common first operand
          if (p1.narrow) {
            Packed* pks[4]; const int32_t* bp[4]; int32_t* op[4]; for (int k = 0; k < 4; k++) { pks[k] = packed_new(&p1, b[k]); bp[k] = b[k]; op[k] = out[k]; kron_ref_negacyclic(n, cs->q, a[0], b[k], ref[k]); }
            fast_mul_shared(&p1, a[0], 4, bp, (const Packed* const*)pks, op, opt);
            for (int k = 0; k < 4; k++) CHECK(!memcmp(ref[k], out[k], n * 4), "shared[%d] %s c=%d opt=%u mode=%d", k, cs->name, c, opt, mode);
            for (int k = 0; k < 4; k++) packed_free(pks[k]);
          }
          packed_free(pk);
        }
      }
    }
  }
  printf(fails ? "%d engine FAILURES of %d checks\n" : "engine: all %d checks passed\n", fails ? fails : checks, checks);
  return fails != 0;
}
