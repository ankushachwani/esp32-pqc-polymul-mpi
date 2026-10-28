// The tuned NTT (ntt_tuned.c) behind the same CpuPoly interface as the PQClean baselines.
#include <string.h>
#include "schemes.h"
#include "ntt_tuned.h"

static TNtt tn[3]; static int tn_ready = 0;
static void init_all(void) { if (!tn_ready) { tntt_init(&tn[TN_KEM], TN_KEM); tntt_init(&tn[TN_DSA], TN_DSA); tntt_init(&tn[TN_FALCON], TN_FALCON); tn_ready = 1; } }

static int32_t ta[512], tb[512], tah[512], tr[512], tw[512];
static const TNtt* cur;

static void load_k(int kind, const int32_t* a, const int32_t* b) {
  init_all(); cur = &tn[kind];
  memcpy(ta, a, cur->n * 4); memcpy(tb, b, cur->n * 4);
  for (int i = 0; i < cur->n; i++) if (tb[i] < 0 && kind == TN_FALCON) tb[i] += cur->q;     // keep signed operands as they are; the transform accepts them
  memcpy(tah, ta, cur->n * 4); tntt_fwd(cur, tah);
}
static void load_kem(const int32_t* a, const int32_t* b) { load_k(TN_KEM, a, b); }
static void load_dsa(const int32_t* a, const int32_t* b) { load_k(TN_DSA, a, b); }
static void load_fal(const int32_t* a, const int32_t* b) { load_k(TN_FALCON, a, b); }
static void t_full(void) { int32_t x[512], y[512]; memcpy(x, ta, cur->n * 4); memcpy(y, tb, cur->n * 4); tntt_fwd(cur, x); tntt_fwd(cur, y); tntt_mul(cur, tr, x, y); tntt_inv(cur, tr); }
static void t_pre(void) { int32_t y[512]; memcpy(y, tb, cur->n * 4); tntt_fwd(cur, y); tntt_mul(cur, tr, tah, y); tntt_inv(cur, tr); }
static void t_get(int32_t* out) { memcpy(out, tr, cur->n * 4); }
static void t_ntt(void) { memcpy(tw, ta, cur->n * 4); tntt_fwd(cur, tw); }
static void t_pw(void) { tntt_mul(cur, tr, tah, tb); }
static void t_intt(void) { memcpy(tw, tr, cur->n * 4); tntt_inv(cur, tw); }

const CpuPoly CPUT_MLKEM = { load_kem, t_full, t_pre, t_get, t_ntt, t_pw, t_intt };
const CpuPoly CPUT_MLDSA = { load_dsa, t_full, t_pre, t_get, t_ntt, t_pw, t_intt };
const CpuPoly CPUT_FALCON = { load_fal, t_full, t_pre, t_get, t_ntt, t_pw, t_intt };
const CpuPoly* cpu_tuned_for_case(int c) { return c <= CASE_MLKEM_FULL ? &CPUT_MLKEM : c <= CASE_MLDSA_FULL ? &CPUT_MLDSA : &CPUT_FALCON; }
