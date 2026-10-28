// The tuned NTT (ntt_tuned.c) behind the same CpuPoly interface as the PQClean baselines.
#include <string.h>
#include "schemes.h"
#include "ntt_tuned.h"

static int32_t *ta, *tb, *tah, *tr, *tw;                 // heap, allocated at first use
static TNtt tn[3]; static int tn_ready = 0;
#include <stdlib.h>
static void init_all(void) { if (!tn_ready) { ta = (int32_t*)malloc(512 * 4); tb = (int32_t*)malloc(512 * 4); tah = (int32_t*)malloc(512 * 4); tr = (int32_t*)malloc(512 * 4); tw = (int32_t*)malloc(512 * 4); tntt_init(&tn[TN_KEM], TN_KEM); tntt_init(&tn[TN_DSA], TN_DSA); tntt_init(&tn[TN_FALCON], TN_FALCON); tn_ready = 1; } }

static const TNtt* cur;

static void load_k(int kind, const int32_t* a, const int32_t* b) {
  init_all(); cur = &tn[kind];
  memcpy(ta, a, cur->n * 4); memcpy(tb, b, cur->n * 4);
  memcpy(tah, tb, cur->n * 4); tntt_fwd(cur, tah);                  // the second operand is the fixed one: transformed in load()
}
static void load_kem(const int32_t* a, const int32_t* b) { load_k(TN_KEM, a, b); }
static void load_dsa(const int32_t* a, const int32_t* b) { load_k(TN_DSA, a, b); }
static void load_fal(const int32_t* a, const int32_t* b) { load_k(TN_FALCON, a, b); }
static void t_full(void) { int32_t x[512], y[512]; memcpy(x, ta, cur->n * 4); memcpy(y, tb, cur->n * 4); tntt_fwd(cur, x); tntt_fwd(cur, y); tntt_mul(cur, tr, x, y); tntt_inv(cur, tr); }
static void t_pre(void) { int32_t y[512]; memcpy(y, ta, cur->n * 4); tntt_fwd(cur, y); tntt_mul(cur, tr, tah, y); tntt_inv(cur, tr); }
static void t_get(int32_t* out) { memcpy(out, tr, cur->n * 4); }
static void t_ntt(void) { memcpy(tw, ta, cur->n * 4); tntt_fwd(cur, tw); }
static void t_pw(void) { tntt_mul(cur, tr, tah, ta); }
static void t_intt(void) { memcpy(tw, tr, cur->n * 4); tntt_inv(cur, tw); }

const CpuPoly CPUT_MLKEM = { load_kem, t_full, t_pre, t_get, t_ntt, t_pw, t_intt };
const CpuPoly CPUT_MLDSA = { load_dsa, t_full, t_pre, t_get, t_ntt, t_pw, t_intt };
const CpuPoly CPUT_FALCON = { load_fal, t_full, t_pre, t_get, t_ntt, t_pw, t_intt };
const CpuPoly* cpu_tuned_for_case(int c) { return c <= CASE_MLKEM_FULL ? &CPUT_MLKEM : c <= CASE_MLDSA_FULL ? &CPUT_MLDSA : &CPUT_FALCON; }

// ---- scheme hooks
typedef struct { int32_t shat[3 * 256]; } KemT;
void* tuned_kem_setup(const int32_t* s) {
  init_all(); KemT* k = (KemT*)malloc(sizeof *k);
  for (int i = 0; i < 3; i++) { memcpy(k->shat + i * 256, s + i * 256, 256 * 4); tntt_fwd(&tn[TN_KEM], k->shat + i * 256); }
  return k;
}
void tuned_kem_dot(void* ctx, const int32_t* u, int32_t* out) {
  const KemT* k = (const KemT*)ctx; int32_t x[256], r[256]; memset(r, 0, sizeof r);
  for (int i = 0; i < 3; i++) { memcpy(x, u + i * 256, sizeof x); tntt_fwd(&tn[TN_KEM], x); tntt_mul_acc(&tn[TN_KEM], r, k->shat + i * 256, x); }
  tntt_inv(&tn[TN_KEM], r); memcpy(out, r, sizeof r);
}

typedef struct { int cached; int32_t* sh[12]; int32_t chat[256]; } DsaT;      // twelve separate 1 KB vectors: the heap has no large free block on the board
static void dsa_transform(DsaT* d, const int32_t* s1, const int32_t* s2, const int32_t* t0) {
  for (int i = 0; i < 12; i++) { const int32_t* src = i < 4 ? s1 + i * 256 : i < 8 ? s2 + (i - 4) * 256 : t0 + (i - 8) * 256; memcpy(d->sh[i], src, 256 * 4); tntt_fwd(&tn[TN_DSA], d->sh[i]); }
}
void* tuned_dsa_setup(int cached, const int32_t* s1, const int32_t* s2, const int32_t* t0) {
  init_all(); DsaT* d = (DsaT*)malloc(sizeof *d); d->cached = cached; for (int i = 0; i < 12; i++) d->sh[i] = (int32_t*)malloc(256 * 4);
  if (cached) dsa_transform(d, s1, s2, t0); return d;
}
void tuned_dsa_begin(void* ctx, const int32_t* s1, const int32_t* s2, const int32_t* t0) { DsaT* d = (DsaT*)ctx; if (!d->cached) dsa_transform(d, s1, s2, t0); }
void tuned_dsa_prod(void* ctx, const int32_t* c, int which, int32_t* out) {
  DsaT* d = (DsaT*)ctx;
  if (which == 0) { memcpy(d->chat, c, 256 * 4); tntt_fwd(&tn[TN_DSA], d->chat); }
  for (int i = 0; i < 4; i++) { tntt_mul(&tn[TN_DSA], out + i * 256, d->sh[which * 4 + i], d->chat); tntt_inv(&tn[TN_DSA], out + i * 256); }
}

typedef struct { int cached; int32_t hh[512]; } FalT;
void* tuned_fal_setup(int cached, const int32_t* h) {
  init_all(); FalT* f = (FalT*)malloc(sizeof *f); f->cached = cached; if (cached) { memcpy(f->hh, h, 512 * 4); tntt_fwd(&tn[TN_FALCON], f->hh); } return f;
}
void tuned_fal_prod(void* ctx, const int32_t* h, const int32_t* s2, int32_t* out) {
  FalT* f = (FalT*)ctx; int32_t x[512];
  if (!f->cached) { memcpy(f->hh, h, 512 * 4); tntt_fwd(&tn[TN_FALCON], f->hh); }
  memcpy(x, s2, sizeof x); tntt_fwd(&tn[TN_FALCON], x); tntt_mul(&tn[TN_FALCON], out, x, f->hh); tntt_inv(&tn[TN_FALCON], out);
}
void tuned_free(void* ctx) { free(ctx); }      /* (a DsaT leaves its twelve vectors to the next free of the heap on purpose: freed by tuned_dsa_free) */
void tuned_dsa_free(void* ctx) { DsaT* d = (DsaT*)ctx; for (int i = 0; i < 12; i++) free(d->sh[i]); free(d); }

void cpu_tuned_prepare(void) { init_all(); }
