#include <stdlib.h>
#include <string.h>
#include "schemes.h"
#include "polymul_fast.h"

struct KemFast { int kind; KronPlan pT, p1; Packed* packT[3]; void* presum; Packed* pack1[3]; int32_t s[3 * 256]; uint32_t opt; size_t bytes; };
struct DsaFast { int kind; KronPlan pcs, pct; Packed* ps[8]; Packed* pt[4]; int32_t s1[1024], s2[1024], t0[1024]; uint32_t opt; size_t bytes; };
struct FalFast { int kind; KronPlan p; Packed* ph; int32_t h[512]; uint32_t opt; size_t bytes; };

static size_t packed_bytes(const KronPlan* p, bool pre) { return (size_t)p->c * p->hw * 4 + (pre ? (size_t)(p->n + 1) * (p->narrow ? 4 : 8) : 0); }

void* fast_kem_setup(const int32_t* s, int c, uint32_t opt) {
  KemFast* k = (KemFast*)calloc(1, sizeof *k); k->kind = 1; k->opt = opt; memcpy(k->s, s, sizeof k->s);
  const KronCase* cs = &KRON_CASES[CASE_MLKEM_X_CBD];
  kron_plan_terms(cs, c, 3, &k->pT); kron_plan(cs, c, &k->p1);
  const int32_t* bs[3] = {k->s, k->s + 256, k->s + 512};
  for (int t = 0; t < 3; t++) { k->packT[t] = packed_new(&k->pT, bs[t]); k->pack1[t] = packed_new(&k->p1, bs[t]); k->bytes += packed_bytes(&k->pT, k->pT.oa) + packed_bytes(&k->p1, k->p1.oa); }
  k->presum = prefix_sum_new(&k->pT, bs, 3); if (k->presum) k->bytes += (size_t)(k->pT.n + 1) * 4;
  return k;
}
void fast_kem_dot(void* ctx, const int32_t* u, int32_t* out) {
  KemFast* k = (KemFast*)ctx; const int32_t* a[3] = {u, u + 256, u + 512}; const int32_t* b[3] = {k->s, k->s + 256, k->s + 512};
  if (k->opt & OPT_FUSE) fast_mul(&k->pT, a, b, k->packT, k->presum, out, k->opt);
  else fast_sum_unfused(&k->p1, 3, a, b, k->pack1, out, k->opt);
}

void* fast_dsa_setup(const int32_t* s1, const int32_t* s2, const int32_t* t0, int c_cs, int c_ct0, uint32_t opt) {
  DsaFast* d = (DsaFast*)calloc(1, sizeof *d); d->kind = 2; d->opt = opt;
  memcpy(d->s1, s1, sizeof d->s1); memcpy(d->s2, s2, sizeof d->s2); memcpy(d->t0, t0, sizeof d->t0);
  kron_plan(&KRON_CASES[CASE_MLDSA_C_S], c_cs, &d->pcs); kron_plan(&KRON_CASES[CASE_MLDSA_C_T0], c_ct0, &d->pct);
  for (int i = 0; i < 4; i++) {
    d->ps[i] = packed_new(&d->pcs, d->s1 + 256 * i); d->ps[4 + i] = packed_new(&d->pcs, d->s2 + 256 * i); d->pt[i] = packed_new(&d->pct, d->t0 + 256 * i);
    d->bytes += 2 * packed_bytes(&d->pcs, d->pcs.oa) + packed_bytes(&d->pct, d->pct.oa);
  }
  return d;
}
void fast_dsa_prod(void* ctx, const int32_t* c, int which, int32_t* out) {
  DsaFast* d = (DsaFast*)ctx; const int32_t* b[4]; const Packed* f[4]; int32_t* o[4];
  const KronPlan* p = which == 2 ? &d->pct : &d->pcs; const int32_t* base = which == 0 ? d->s1 : which == 1 ? d->s2 : d->t0;
  for (int i = 0; i < 4; i++) { b[i] = base + 256 * i; f[i] = which == 2 ? d->pt[i] : d->ps[which * 4 + i]; o[i] = out + 256 * i; }
  fast_mul_shared(p, c, 4, b, f, o, d->opt);
}

void* fast_fal_setup(const int32_t* h, int c, uint32_t opt) {
  FalFast* f = (FalFast*)calloc(1, sizeof *f); f->kind = 3; f->opt = opt; memcpy(f->h, h, sizeof f->h);
  kron_plan(&KRON_CASES[CASE_FALCON_S2_H], c, &f->p); f->ph = packed_new(&f->p, f->h); f->bytes = packed_bytes(&f->p, f->p.oa);
  return f;
}
void fast_fal_prod(void* ctx, const int32_t* h, const int32_t* s2, int32_t* out) {
  FalFast* f = (FalFast*)ctx; (void)h; const int32_t* a[1] = {s2}; const int32_t* b[1] = {f->h}; const Packed* pk[1] = {f->ph};
  fast_mul(&f->p, a, b, pk, nullptr, out, f->opt);
}

size_t fast_ctx_bytes(void* ctx) { return *(int*)ctx == 1 ? ((KemFast*)ctx)->bytes : *(int*)ctx == 2 ? ((DsaFast*)ctx)->bytes : ((FalFast*)ctx)->bytes; }
void fast_free(void* ctx) {
  if (!ctx) return;
  if (*(int*)ctx == 1) { KemFast* k = (KemFast*)ctx; for (int t = 0; t < 3; t++) { packed_free(k->packT[t]); packed_free(k->pack1[t]); } free(k->presum); }
  else if (*(int*)ctx == 2) { DsaFast* d = (DsaFast*)ctx; for (int i = 0; i < 8; i++) packed_free(d->ps[i]); for (int i = 0; i < 4; i++) packed_free(d->pt[i]); }
  else { FalFast* f = (FalFast*)ctx; packed_free(f->ph); }
  free(ctx);
}
