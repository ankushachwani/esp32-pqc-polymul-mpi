// Benchmark firmware: packed polynomial products on the ESP32 MPI unit against the PQClean NTT, and whole-scheme operations with the product swapped.
// Line protocol over the serial port (115200 baud). Every command ends its reply with a line "END"; "ERR ..." reports a failure.
//
//   INFO                                    chip, clock, free heap
//   PLAN <case> <c>                         plan for c chunks per operand (slot bits, words per operand, multiplies)
//   CHECK <case> <c> <stage> <nvec>         bit-exactness of a stage against the O(n^2) reference on random and extreme vectors
//   CHECKCPU <case> <nvec>                  same for the NTT baseline
//   BENCH <case> <c> <stage> <reps>         latency of one polynomial product; stage 0..3 = S_MBEDTLS..S_OVERLAP, 4 = NTT full, 5 = NTT with operand a pre-transformed, 6/7 = the same with the tuned NTT
//   FAST <kind> <case> <c> <opt> <reps>     optimised path (polymul_fast.cpp); kind single | sum (3 terms, ML-KEM decrypt shape) | shared (4 products with a common operand);
//                                           opt = sum of OPT_PREPACK 1, OPT_XRES 2, OPT_FUSE 4, OPT_SHARE 8; bit-exact check before the timing
//   HWONLY <case> <c> <xres> <reps>         only the unit's work for a plan (load, start, wait, read) with no CPU work: the floor for that plan
//   PROF <case> <c> <reps>                  where the time goes in S_DRIVER
//   COMP <case> <reps>                      NTT parts: forward, pointwise, inverse
//   HWLAT                                   multiplier cost per operand size: mbedtls vs driver, load / compute / read, and sizes beyond 2048 bits through mbedtls
//   ACQ <reps>                              cost of acquiring and releasing the unit
//   E2E <scheme> <op> <variant> <reps> <c1> <c2>
//         scheme kem | dsa | falcon;  variant ref (PQClean as shipped), cpu (copy of it, NTT), s0..s3 (product on the MPI unit, first four stages),
//         t / tc (tuned NTT; the key's transform redone per call / cached), f<N> (optimised MPI path, N = OPT_* switches, key prepacked)
//         kem:   op dec | indcpa | keygen | enc      c1 = plan for the u*s product
//         dsa:   op sign | verify                    c1 = plan c*s1/c*s2, c2 = plan c*t0
//         falcon op verify | sign                    c1 = plan for h*s2
// The scheme work runs in a task with a large stack (ML-DSA signing and Falcon key generation need it).
#include <Arduino.h>
#include <math.h>
#include <string.h>
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "mbedtls/bignum.h"
#include "mpi_hw.h"
#include "kron.h"
#include "schemes.h"
#include "polymul_hw.h"
#include "polymul_fast.h"
#include "fast_hooks.h"

extern "C" void rb_seed(uint64_t s);
static const double MHZ = 240.0;
static inline uint32_t cyc() { return ESP.getCycleCount(); }

static uint32_t rs = 0x2545F491;
static uint32_t rnd() { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static int32_t rin(int32_t lo, int32_t hi) { return lo + (int32_t)(rnd() % (uint32_t)(hi - lo + 1)); }
static void fillvec(int32_t* v, int n, int32_t lo, int32_t hi, int mode) {       // mode 0 random, 1 all lo, 2 all hi, 3 alternating
  for (int i = 0; i < n; i++) v[i] = mode == 0 ? rin(lo, hi) : mode == 1 ? lo : mode == 2 ? hi : (i & 1 ? lo : hi);
}

static int find_case(const char* s) { for (int i = 0; i < N_CASES; i++) if (!strcmp(KRON_CASES[i].name, s)) return i; return -1; }
static bool hw_held = false;
static void hw_on() { if (!hw_held) { if (!mpihw::acquire()) Serial.println("ERR acquire"); hw_held = true; } }
static void hw_off() { if (hw_held) { mpihw::release(); hw_held = false; } }
static void hw_for_stage(int stage) { if (stage_needs_hw(stage)) hw_on(); else hw_off(); }

static int get_plan(int ci, int c, KronPlan* p) {
  if (ci < 0) { Serial.println("ERR case"); return -1; }
  if (kron_plan(&KRON_CASES[ci], c, p)) { Serial.println("ERR plan infeasible"); return -1; }
  return 0;
}

// The static DRAM segment is full and the heap is fragmented once the worker's stack exists (largest free block about 18 KB), so command scratch is taken in
// pieces of at most 8 KB and given back when the command ends.
static void* g_alloc[32]; static int g_nalloc;
static void* pool_alloc(size_t bytes) { void* r = malloc(bytes); if (r && g_nalloc < 32) g_alloc[g_nalloc++] = r; else if (!r) Serial.println("ERR out of memory"); return r; }
static void pool_release() { while (g_nalloc) free(g_alloc[--g_nalloc]); }
static int32_t* g_vec;                                           // 4 x 512 scratch for the check commands
static void vec_init() { if (!g_vec) g_vec = (int32_t*)pool_alloc(4 * 512 * 4); }
static int32_t (*va)[512], (*vb)[512];                              // on the heap: the static DRAM segment is too small for them
static void make_pool(int ci) {
  const KronCase& k = KRON_CASES[ci];
  if (!va) { va = (int32_t(*)[512])pool_alloc(4 * 512 * 4); vb = (int32_t(*)[512])pool_alloc(4 * 512 * 4); }
  for (int s = 0; s < 4; s++) { fillvec(va[s], k.n, k.amin, k.amax, 0); fillvec(vb[s], k.n, k.bmin, k.bmax, 0); }
}

static void cmd_plan(int ci, int c) {
  KronPlan p; if (get_plan(ci, c, &p)) return;
  Serial.printf("PLAN case=%s n=%d q=%u c=%d m=%d slot_bits=%d hw_words=%u operand_bits=%u multiplies=%d\n", KRON_CASES[ci].name, p.n, (unsigned)p.cs->q, p.c, p.m, p.w,
                (unsigned)p.hw, (unsigned)p.hw * 32, p.c * p.c);
}

static void cmd_check(int ci, int c, int stage, int nvec) {
  KronPlan p; if (get_plan(ci, c, &p)) return;
  const KronCase& k = KRON_CASES[ci]; vec_init(); int32_t *a = g_vec, *b = g_vec + 512, *ref = g_vec + 1024, *got = g_vec + 1536;
  hw_for_stage(stage); kron_mul_fn f = polymul_for_stage(stage); int bad = 0, tot = 0;
  for (int t = 0; t < nvec + 16; t++) {
    int ma = 0, mb = 0; if (t >= nvec) { ma = 1 + (t - nvec) % 4; mb = 1 + (t - nvec) / 4 % 4; }          // 16 extreme-value pairs after the random ones
    fillvec(a, k.n, k.amin, k.amax, ma); fillvec(b, k.n, k.bmin, k.bmax, mb);
    kron_ref_negacyclic(k.n, k.q, a, b, ref); f(&p, a, b, got); tot++;
    if (memcmp(ref, got, k.n * 4)) bad++;
  }
  Serial.printf("CHK case=%s c=%d stage=%s vectors=%d bad=%d\n", k.name, c, STAGE_NAME[stage], tot, bad);
}

static void cmd_checkcpu(int ci, int nvec) {
  const KronCase& k = KRON_CASES[ci]; vec_init(); int32_t *a = g_vec, *b = g_vec + 512, *ref = g_vec + 1024, *got = g_vec + 1536;
  for (int impl = 0; impl < 2; impl++) {
    const CpuPoly* cp = impl ? cpu_tuned_for_case(ci) : cpu_poly_for_case(ci); int bad = 0, tot = 0;
    rs = 0x2545F491 + impl * 0;                                   // same vectors for both implementations
    for (int t = 0; t < nvec + 16; t++) {
      int ma = 0, mb = 0; if (t >= nvec) { ma = 1 + (t - nvec) % 4; mb = 1 + (t - nvec) / 4 % 4; }
      fillvec(a, k.n, k.amin, k.amax, ma); fillvec(b, k.n, k.bmin, k.bmax, mb);
      kron_ref_negacyclic(k.n, k.q, a, b, ref); cp->load(a, b);
      cp->full(); cp->get(got); if (memcmp(ref, got, k.n * 4)) bad++;
      cp->pre(); cp->get(got); if (memcmp(ref, got, k.n * 4)) bad++; tot += 2;
    }
    Serial.printf("CHKCPU case=%s impl=%s vectors=%d bad=%d\n", k.name, impl ? "tuned" : "pqclean", tot, bad);
  }
}

struct Stats { double sum = 0, sq = 0, mn = 1e18, mx = 0; int n = 0;
  void add(uint32_t cycles) { const double us = cycles / MHZ; sum += us; sq += us * us; if (us < mn) mn = us; if (us > mx) mx = us; n++; }
  double mean() const { return sum / n; } double sd() const { const double m = mean(); const double v = sq / n - m * m; return v > 0 ? sqrt(v) : 0; } };

static void cmd_bench(int ci, int c, int stage, int reps) {
  const KronCase& k = KRON_CASES[ci]; make_pool(ci); static int32_t out[512]; Stats st;
  if (stage <= S_OVERLAP) {
    KronPlan p; if (get_plan(ci, c, &p)) return;
    hw_for_stage(stage); kron_mul_fn f = polymul_for_stage(stage);
    f(&p, va[0], vb[0], out);
    for (int r = 0; r < reps; r++) { const int s = r & 3; const uint32_t t0 = cyc(); f(&p, va[s], vb[s], out); st.add(cyc() - t0); }
    Serial.printf("R case=%s c=%d stage=%s reps=%d mean_us=%.2f min_us=%.2f max_us=%.2f std_us=%.2f multiplies=%d slot_bits=%d hw_words=%u\n", k.name, c, STAGE_NAME[stage], reps, st.mean(),
                  st.mn, st.mx, st.sd(), p.c * p.c, p.w, (unsigned)p.hw);
  } else {
    const CpuPoly* cp = stage >= 6 ? cpu_tuned_for_case(ci) : cpu_poly_for_case(ci); hw_off();
    const bool full = stage == 4 || stage == 6;
    cp->load(va[0], vb[0]); if (full) cp->full(); else cp->pre();
    for (int r = 0; r < reps; r++) {
      cp->load(va[r & 3], vb[r & 3]);
      const uint32_t t0 = cyc(); if (full) cp->full(); else cp->pre(); st.add(cyc() - t0);
    }
    const char* nm = stage == 4 ? "ntt_full" : stage == 5 ? "ntt_pre" : stage == 6 ? "tuned_full" : "tuned_pre";
    Serial.printf("R case=%s c=0 stage=%s reps=%d mean_us=%.2f min_us=%.2f max_us=%.2f std_us=%.2f multiplies=0 slot_bits=0 hw_words=0\n", k.name, nm, reps,
                  st.mean(), st.mn, st.mx, st.sd());
  }
}

static void cmd_prof(int ci, int c, int reps) {
  KronPlan p; if (get_plan(ci, c, &p)) return;
  make_pool(ci); static int32_t out[512]; hw_on(); polymul_driver_profiled(&p, va[0], vb[0], out);
  double s[7] = {0}; double tot = 0;
  for (int r = 0; r < reps; r++) {
    const uint32_t t0 = cyc(); polymul_driver_profiled(&p, va[r & 3], vb[r & 3], out); tot += cyc() - t0;
    s[0] += g_prof.pack; s[1] += g_prof.prefix; s[2] += g_prof.load; s[3] += g_prof.compute; s[4] += g_prof.read; s[5] += g_prof.accum; s[6] += g_prof.finish;
  }
  const double k = 1.0 / MHZ / reps;
  Serial.printf("P case=%s c=%d reps=%d pack_us=%.2f prefix_us=%.2f load_us=%.2f compute_us=%.2f read_us=%.2f accum_us=%.2f finish_us=%.2f total_us=%.2f\n", KRON_CASES[ci].name, c, reps,
                s[0] * k, s[1] * k, s[2] * k, s[3] * k, s[4] * k, s[5] * k, s[6] * k, tot * k);
}

static void cmd_comp(int ci, int reps) {
  const KronCase& k = KRON_CASES[ci]; const CpuPoly* cp = cpu_poly_for_case(ci); make_pool(ci); hw_off();
  cp->load(va[0], vb[0]); cp->full(); cp->pre(); cp->ntt(); cp->pointwise(); cp->intt();
  uint32_t tn = 0, tp = 0, ti = 0, tf = 0, tq = 0;
  for (int r = 0; r < reps; r++) {
    cp->load(va[r & 3], vb[r & 3]);
    uint32_t t0 = cyc(); cp->ntt(); tn += cyc() - t0;
    t0 = cyc(); cp->pointwise(); tp += cyc() - t0;
    t0 = cyc(); cp->intt(); ti += cyc() - t0;
    t0 = cyc(); cp->full(); tf += cyc() - t0;
    t0 = cyc(); cp->pre(); tq += cyc() - t0;
  }
  const double f = 1.0 / MHZ / reps;
  Serial.printf("C case=%s reps=%d ntt_us=%.2f pointwise_us=%.2f intt_us=%.2f full_us=%.2f pre_us=%.2f\n", k.name, reps, tn * f, tp * f, ti * f, tf * f, tq * f);
  const CpuPoly* ct = cpu_tuned_for_case(ci); ct->load(va[0], vb[0]); ct->full(); ct->pre(); ct->ntt(); ct->pointwise(); ct->intt();
  tn = tp = ti = tf = tq = 0;
  for (int r = 0; r < reps; r++) {
    ct->load(va[r & 3], vb[r & 3]);
    uint32_t t0 = cyc(); ct->ntt(); tn += cyc() - t0;
    t0 = cyc(); ct->pointwise(); tp += cyc() - t0;
    t0 = cyc(); ct->intt(); ti += cyc() - t0;
    t0 = cyc(); ct->full(); tf += cyc() - t0;
    t0 = cyc(); ct->pre(); tq += cyc() - t0;
  }
  Serial.printf("CT case=%s reps=%d ntt_us=%.2f pointwise_us=%.2f intt_us=%.2f full_us=%.2f pre_us=%.2f\n", k.name, reps, tn * f, tp * f, ti * f, tf * f, tq * f);
}

// ---------------------------------------------------------------- optimised path
static int32_t* g_out;
static void sum_mod(int32_t* acc, const int32_t* v, int n, uint32_t q) { for (int i = 0; i < n; i++) { int32_t x = acc[i] + v[i]; if (x >= (int32_t)q) x -= q; acc[i] = x; } }

// fill a (or b) of one trial; mode 0 random, 1..3 extreme patterns
static void cmd_fast(const char* kind, int ci, int c, int opt, int reps) {
  const KronCase& k = KRON_CASES[ci]; const int n = k.n; vec_init(); make_pool(ci);
  if (!g_out) g_out = (int32_t*)pool_alloc(8 * 256 * 4 > 2 * 512 * 4 ? 8 * 256 * 4 : 2 * 512 * 4);
  const bool sh = !strcmp(kind, "shared"); int32_t* out = g_out; int32_t* ref = g_out + (sh ? 4 * 256 : 512); int32_t* tmp = g_vec + 1536;
  const int ostride = 256;
  hw_on(); Stats st; int bad = 0, checks = 0;
  const bool single = !strcmp(kind, "single"), sum = !strcmp(kind, "sum"), shared = !strcmp(kind, "shared");
  if (!single && !sum && !shared) { Serial.println("ERR kind"); return; }
  const int T = sum ? 3 : 1, NB = shared ? 4 : 1;
  KronPlan pT, p1; if (kron_plan_terms(&k, c, T, &pT) || kron_plan(&k, c, &p1)) { Serial.println("ERR plan infeasible"); return; }
  if (shared && !pT.narrow) { Serial.println("ERR shared needs a narrow plan"); return; }
  // fixed operands
  int32_t* fixb = (int32_t*)pool_alloc(4 * n * 4);                    // the fixed b vectors (secret-like)
  const Packed* pk1[4] = {0, 0, 0, 0}; const Packed* pkT[4] = {0, 0, 0, 0}; void* presum = nullptr;
  auto build_fixed = [&](int32_t* const* bsrc) {
    for (int t = 0; t < 4; t++) { packed_free((Packed*)pk1[t]); packed_free((Packed*)pkT[t]); pk1[t] = pkT[t] = nullptr; }
    free(presum); presum = nullptr;
    const int nf = sum ? 3 : shared ? 4 : 1;
    for (int t = 0; t < nf; t++) { pk1[t] = packed_new(&p1, bsrc[t]); if (sum) pkT[t] = packed_new(&pT, bsrc[t]); }
    if (sum) presum = prefix_sum_new(&pT, (const int32_t* const*)bsrc, 3);
  };
  auto run = [&](int32_t* const* as, int32_t* const* bs, uint32_t o) {
    if (single) { const int32_t* aa[1] = {as[0]}; const int32_t* bb[1] = {bs[0]}; const Packed* ff[1] = {pk1[0]}; fast_mul(&p1, aa, bb, ff, nullptr, out, o); }
    else if (sum) {
      if (o & OPT_FUSE) fast_mul(&pT, (const int32_t* const*)as, (const int32_t* const*)bs, pkT, presum, out, o);
      else fast_sum_unfused(&p1, 3, (const int32_t* const*)as, (const int32_t* const*)bs, pk1, out, o);
    } else { int32_t* oo[4] = {out, out + ostride, out + 2 * ostride, out + 3 * ostride}; fast_mul_shared(&p1, as[0], 4, (const int32_t* const*)bs, pk1, oo, o); }
  };
  auto reference = [&](int32_t* const* as, int32_t* const* bs) {
    if (single) kron_ref_negacyclic(n, k.q, as[0], bs[0], ref);
    else if (sum) { memset(ref, 0, n * 4); for (int t = 0; t < 3; t++) { kron_ref_negacyclic(n, k.q, as[t], bs[t], tmp); sum_mod(ref, tmp, n, k.q); } }
    else for (int t = 0; t < 4; t++) kron_ref_negacyclic(n, k.q, as[0], bs[t], ref + t * ostride);
  };
  auto same = [&]() { if (single || sum) return !memcmp(out, ref, n * 4); for (int t = 0; t < 4; t++) if (memcmp(out + t * ostride, ref + t * ostride, n * 4)) return false; return true; };
  // bit-exactness: random and extreme operands, fresh fixed operands for each trial
  int32_t* av[4]; int32_t* bv[4]; int32_t* trial = (int32_t*)pool_alloc(4 * n * 4);
  if (!trial || !fixb || !g_out || !g_vec || !va || !vb) { Serial.println("ERR scratch"); return; }
  for (int t = 0; t < 24; t++) {
    const int mode = t < 12 ? 0 : 1 + (t - 12) % 3, modeb = t < 12 ? 0 : 1 + (t - 12) / 4 % 3;
    for (int u = 0; u < 4; u++) { av[u] = trial + u * n; bv[u] = fixb + u * n; fillvec(av[u], n, k.amin, k.amax, mode); fillvec(bv[u], n, k.bmin, k.bmax, modeb); }
    build_fixed(bv); reference(av, bv); run(av, bv, opt); checks++; if (!same()) bad++;
  }
  // timing: fixed operands as in the real use (built once), varying operand rotates over the pool
  for (int u = 0; u < 4; u++) { bv[u] = fixb + u * n; memcpy(bv[u], vb[u], n * 4); }
  build_fixed(bv);
  int32_t* arot[4];
  for (int u = 0; u < 4; u++) arot[u] = va[u];
  run(arot, bv, opt);
  for (int r = 0; r < reps; r++) {
    for (int u = 0; u < 4; u++) arot[u] = va[(r + u) & 3];
    const uint32_t t0 = cyc(); run(arot, bv, opt); st.add(cyc() - t0);
  }
  Serial.printf("F kind=%s case=%s c=%d opt=%d reps=%d mean_us=%.2f min_us=%.2f max_us=%.2f std_us=%.2f checks=%d bad=%d\n", kind, k.name, c, opt, reps, st.mean(), st.mn, st.mx, st.sd(), checks, bad);
  for (int t = 0; t < 4; t++) { packed_free((Packed*)pk1[t]); packed_free((Packed*)pkT[t]); } free(presum);
}

static void cmd_hwonly(int ci, int c, int xres, int reps) {
  KronPlan p; if (get_plan(ci, c, &p)) return;
  const uint32_t hw = p.hw; static uint32_t* x = nullptr; if (!x) x = (uint32_t*)malloc(2 * 64 * 4);
  for (uint32_t i = 0; i < hw; i++) { x[i] = rnd() & 0x7fffffff; x[64 + i] = rnd() & 0x7fffffff; }
  hw_on(); static uint32_t z[KRON_ZWORDS]; Stats st;
  for (int r = -1; r < reps; r++) {
    const uint32_t t0 = cyc();
    for (int i = 0; i < p.c; i++) for (int j = 0; j < p.c; j++) {
      if (!(xres && j > 0)) mpihw::load_x(x, hw, hw);
      mpihw::load_y(x + 64, hw, hw); mpihw::start(hw); mpihw::wait(); mpihw::read_z_fast(z, 2 * hw);
    }
    if (r >= 0) st.add(cyc() - t0);
  }
  Serial.printf("W case=%s c=%d xres=%d reps=%d mean_us=%.2f min_us=%.2f max_us=%.2f multiplies=%d\n", KRON_CASES[ci].name, c, xres, reps, st.mean(), st.mn, st.mx, p.c * p.c);
}

static uint32_t sw_a[96], sw_b[96], sw_ref[200], sw_z[200];
static void cmd_hwlat() {
  hw_off();
  const int sizes[] = {512, 1024, 1536, 2048, 2560, 3072};
  for (int bits : sizes) {
    const int w = bits / 32;
    for (int i = 0; i < w; i++) { sw_a[i] = rnd() ^ (rnd() << 7); sw_b[i] = rnd() ^ (rnd() << 7); }
    sw_a[w - 1] |= 0x80000000u; sw_b[w - 1] |= 0x80000000u;
    mbedtls_mpi A, B, Z; mbedtls_mpi_init(&A); mbedtls_mpi_init(&B); mbedtls_mpi_init(&Z);
    mbedtls_mpi_read_binary_le(&A, (uint8_t*)sw_a, w * 4); mbedtls_mpi_read_binary_le(&B, (uint8_t*)sw_b, w * 4);
    mbedtls_mpi_mul_mpi(&Z, &A, &B);
    uint32_t t0 = cyc(); for (int r = 0; r < 100; r++) mbedtls_mpi_mul_mpi(&Z, &A, &B); const double tm = (cyc() - t0) / MHZ / 100;
    memset(sw_z, 0, sizeof sw_z); mbedtls_mpi_write_binary_le(&Z, (uint8_t*)sw_z, w * 8);
    // software reference for exactness (64-bit schoolbook)
    memset(sw_ref, 0, sizeof sw_ref);
    for (int i = 0; i < w; i++) { uint64_t carry = 0; for (int j = 0; j < w; j++) { uint64_t t = (uint64_t)sw_a[i] * sw_b[j] + sw_ref[i + j] + carry; sw_ref[i + j] = (uint32_t)t; carry = t >> 32; } sw_ref[i + w] = (uint32_t)carry; }
    const int mb_ok = !memcmp(sw_ref, sw_z, w * 8);
    mbedtls_mpi_free(&A); mbedtls_mpi_free(&B); mbedtls_mpi_free(&Z);
    if (bits <= 2048) {
      hw_on(); const uint32_t hw = mpihw::hw_words(w); static uint32_t z[130]; uint32_t tl = 0, tc = 0, tr = 0; const int R = 100; int ok = 1;
      for (int r = 0; r < R; r++) {
        uint32_t u0 = cyc(); mpihw::load(sw_a, w, sw_b, w, hw); uint32_t u1 = cyc(); mpihw::start(hw); mpihw::wait(); uint32_t u2 = cyc(); mpihw::read_z_fast(z, 2 * w); uint32_t u3 = cyc();
        tl += u1 - u0; tc += u2 - u1; tr += u3 - u2; if (r == 0) ok = !memcmp(z, sw_ref, w * 8);
      }
      hw_off();
      Serial.printf("H bits=%d mbedtls_us=%.2f direct_us=%.2f load_us=%.2f compute_us=%.2f read_us=%.2f mbedtls_exact=%d direct_exact=%d\n", bits, tm, (tl + tc + tr) / MHZ / R, tl / MHZ / R, tc / MHZ / R,
                    tr / MHZ / R, mb_ok, ok);
    } else {
      Serial.printf("H bits=%d mbedtls_us=%.2f direct_us=-1 load_us=-1 compute_us=-1 read_us=-1 mbedtls_exact=%d direct_exact=-1\n", bits, tm, mb_ok);
    }
  }
}

static void cmd_acq(int reps) {
  hw_off(); uint32_t t0 = cyc(); for (int r = 0; r < reps; r++) { mpihw::acquire(); mpihw::release(); }
  Serial.printf("A acquire_release_us=%.2f reps=%d\n", (cyc() - t0) / MHZ / reps, reps);
}

// ---------------------------------------------------------------- whole-scheme runs
// variants: ref (PQClean as shipped), cpu (copy of it), s0..s3 (old stages), t / tc (tuned NTT, key transform per call / cached), f<N> (optimised path with switches N)
static int parse_variant(const char* s) {
  if (!strcmp(s, "ref")) return -2; if (!strcmp(s, "cpu")) return -1; if (!strcmp(s, "t")) return 10; if (!strcmp(s, "tc")) return 11;
  if (s[0] == 's' && s[1] >= '0' && s[1] <= '3' && !s[2]) return s[1] - '0';
  if (s[0] == 'f' && s[1]) return 100 + atoi(s + 1);
  return -3;
}
extern "C" uint32_t prof_now(void) { return cyc(); }
static void report(const char* scheme, const char* op, const char* variant, int reps, const Stats& st, int ok, double prod_us, size_t mem, const char* extra) {
  Serial.printf("E scheme=%s op=%s variant=%s reps=%d mean_us=%.1f min_us=%.1f max_us=%.1f std_us=%.1f ok=%d prod_us=%.1f mem_bytes=%u %s\n", scheme, op, variant, reps, st.mean(), st.mn, st.mx, st.sd(), ok,
                prod_us, (unsigned)mem, extra);
}

static uint8_t kpk[KEM_PK], ksk[KEM_SK], kct[4][KEM_CT], kss[4][KEM_SS]; static int32_t ks_std[3 * 256]; static bool kem_ready = false;
static void kem_setup() {
  if (kem_ready) return;
  rb_seed(7); kem_keypair(kpk, ksk); kem_sk_std(ksk, ks_std);
  for (int i = 0; i < 4; i++) { kem_enc(kct[i], kss[i], kpk); if (i >= 2) kct[i][17 + i] ^= 0x10; }      // two valid ciphertexts, two corrupted (implicit rejection)
  kem_ready = true;
}
static void e2e_kem(const char* op, const char* var, int reps, int c1) {
  kem_setup(); const int v = parse_variant(var); if (v == -3) { Serial.println("ERR variant"); return; }
  KronPlan p; if (v >= 0 && v < 4) { if (get_plan(CASE_MLKEM_X_CBD, c1, &p)) return; hw_for_stage(v); } else if (v >= 100) hw_on(); else hw_off();
  kron_mul_fn f = (v >= 0 && v < 4) ? polymul_for_stage(v) : nullptr; const KronPlan* pp = (v >= 0 && v < 4) ? &p : nullptr;
  void* ctx = nullptr; kem_dot_fn dot = nullptr; size_t mem = 0;
  if (v == 10 || v == 11) { ctx = tuned_kem_setup(ks_std); dot = tuned_kem_dot; }
  if (v >= 100) { ctx = fast_kem_setup(ks_std, c1, (uint32_t)(v - 100)); dot = fast_kem_dot; mem = fast_ctx_bytes(ctx); }
  Stats st; int ok = 1; uint8_t ss[KEM_SS], m1[32], m2[32]; double prod = 0;
  const bool dec = !strcmp(op, "dec"), indcpa = !strcmp(op, "indcpa");
  if (!dec && !indcpa) {
    if (v != -2) { Serial.println("ERR keygen/enc only exist as ref"); return; }
    static uint8_t pk[KEM_PK], sk[KEM_SK], ct[KEM_CT];
    for (int r = 0; r < reps; r++) { rb_seed(100 + r); const uint32_t t0 = cyc(); if (!strcmp(op, "keygen")) kem_keypair(pk, sk); else kem_enc(ct, ss, kpk); st.add(cyc() - t0); }
    report("mlkem768", op, var, reps, st, 1, -1, 0, ""); return;
  }
  for (int r = 0; r < reps; r++) {
    const int i = r & 3; uint32_t t0; g_prod_cycles = 0;
    if (dec) {
      t0 = cyc();
      if (v == -2) kem_dec_ref(ss, kct[i], ksk); else if (dot) kem_dec_fn(ss, kct[i], ksk, dot, ctx); else kem_dec_var(ss, kct[i], ksk, ks_std, f, pp);
      st.add(cyc() - t0); prod += g_prod_cycles / MHZ;
      uint8_t ref[KEM_SS]; kem_dec_ref(ref, kct[i], ksk); if (memcmp(ref, ss, 32)) ok = 0;
      if (i < 2 && memcmp(ss, kss[i], 32)) ok = 0;
    } else {
      t0 = cyc();
      if (v == -2) kem_indcpa_dec_ref(m1, kct[i], ksk); else if (dot) kem_indcpa_dec_fn(m1, kct[i], dot, ctx); else kem_indcpa_dec_var(m1, kct[i], ksk, ks_std, f, pp);
      st.add(cyc() - t0); prod += g_prod_cycles / MHZ;
      kem_indcpa_dec_ref(m2, kct[i], ksk); if (memcmp(m1, m2, 32)) ok = 0;
    }
  }
  report("mlkem768", op, var, reps, st, ok, v == -2 ? -1 : prod / reps, mem, "");
  if (v == 10 || v == 11) tuned_free(ctx); if (v >= 100) fast_free(ctx);
}

static uint8_t dpk[DSA_PK], dsk[DSA_SK]; static bool dsa_ready = false; static uint8_t dmsg[4][48];
static uint8_t* dsig_ref[4]; static size_t dsig_len[4];
static void dsa_setup() {
  if (dsa_ready) return;
  rb_seed(11); dsa_keypair(dpk, dsk);
  for (int i = 0; i < 4; i++) { for (int j = 0; j < 48; j++) dmsg[i][j] = (uint8_t)(i * 31 + j * 7 + 1); rb_seed(500 + i); dsa_sign_ref(dsig_ref[i], &dsig_len[i], dmsg[i], 48, dsk); }
  dsa_ready = true;
}
static uint8_t dsig[DSA_SIG];
static void e2e_dsa(const char* op, const char* var, int reps, int c1, int c2) {
  dsa_setup(); const int v = parse_variant(var); if (v == -3) { Serial.println("ERR variant"); return; }
  KronPlan pcs, pct; if (v >= 0 && v < 4) { if (get_plan(CASE_MLDSA_C_S, c1, &pcs) || get_plan(CASE_MLDSA_C_T0, c2, &pct)) return; hw_for_stage(v); } else if (v >= 100) hw_on(); else hw_off();
  kron_mul_fn f = (v >= 0 && v < 4) ? polymul_for_stage(v) : nullptr; Stats st; int ok = 1; int attempts_total = 0; size_t sl; double prod = 0; size_t mem = 0;
  if (!strcmp(op, "verify")) {
    if (v != -2) { Serial.println("ERR verify only exists as ref"); return; }
    for (int r = 0; r < reps; r++) { const int i = r & 3; const uint32_t t0 = cyc(); const int rc = dsa_verify(dsig_ref[i], dsig_len[i], dmsg[i], 48, dpk); st.add(cyc() - t0); if (rc) ok = 0; }
    report("mldsa44", op, var, reps, st, ok, -1, 0, "attempts=0"); return;
  }
  void* ctx = nullptr; dsa_ops ops = {nullptr, nullptr, nullptr}; int32_t *sv = nullptr, *sv2 = nullptr, *sv3 = nullptr;
  if (v == 10 || v == 11 || v >= 100) {
    sv = (int32_t*)pool_alloc(1024 * 4); sv2 = (int32_t*)pool_alloc(1024 * 4); sv3 = (int32_t*)pool_alloc(1024 * 4); dsa_unpack_secret(dsk, sv, sv2, sv3);
    if (v == 10 || v == 11) { ctx = tuned_dsa_setup(v == 11, sv, sv2, sv3); ops.ctx = ctx; ops.begin = tuned_dsa_begin; ops.prod = tuned_dsa_prod; }
    else { ctx = fast_dsa_setup(sv, sv2, sv3, c1, c2, (uint32_t)(v - 100)); mem = fast_ctx_bytes(ctx); ops.ctx = ctx; ops.begin = nullptr; ops.prod = fast_dsa_prod; }
  }
  for (int r = 0; r < reps; r++) {
    const int i = r & 3; int att = 1; g_prod_cycles = 0;
    rb_seed(500 + i);
    const uint32_t t0 = cyc();
    if (v == -2) dsa_sign_ref(dsig, &sl, dmsg[i], 48, dsk);
    else if (ctx) dsa_sign_ops(dsig, &sl, dmsg[i], 48, dsk, &ops, &att);
    else dsa_sign_var(dsig, &sl, dmsg[i], 48, dsk, f, &pcs, &pct, &att);
    st.add(cyc() - t0); prod += g_prod_cycles / MHZ;
    if (sl != dsig_len[i] || memcmp(dsig, dsig_ref[i], sl)) ok = 0;
    attempts_total += att;
  }
  char ex[48]; snprintf(ex, sizeof ex, "attempts=%d", attempts_total);
  report("mldsa44", op, var, reps, st, ok, v == -2 ? -1 : prod / reps, mem, v == -2 ? "attempts=na" : ex);
  if (v == 10 || v == 11) tuned_dsa_free(ctx); if (v >= 100) fast_free(ctx);
}

static uint8_t fpk[FAL_PK], fsk[FAL_SK], fsig[4][FAL_SIG + 8]; static size_t fslen[4]; static uint8_t fmsg[4][48]; static bool fal_ready = false;
static void fal_setup() {
  if (fal_ready) return;
  rb_seed(21); const uint32_t t0 = cyc(); falcon_keypair(fpk, fsk); const double kg = (cyc() - t0) / MHZ / 1000.0;
  for (int i = 0; i < 4; i++) { for (int j = 0; j < 48; j++) fmsg[i][j] = (uint8_t)(i * 17 + j * 5 + 3); rb_seed(900 + i); falcon_sign(fsig[i], &fslen[i], fmsg[i], 48, fsk); }
  Serial.printf("F keygen_ms=%.1f\n", kg); fal_ready = true;
}
static void e2e_falcon(const char* op, const char* var, int reps, int c1) {
  fal_setup(); const int v = parse_variant(var); if (v == -3) { Serial.println("ERR variant"); return; }
  Stats st; int ok = 1; double prod = 0; size_t mem = 0;
  if (!strcmp(op, "sign")) {
    if (v != -2) { Serial.println("ERR sign only exists as ref"); return; }
    static uint8_t sg[FAL_SIG + 8]; size_t sl;
    for (int r = 0; r < reps; r++) { rb_seed(900 + (r & 3)); const uint32_t t0 = cyc(); falcon_sign(sg, &sl, fmsg[r & 3], 48, fsk); st.add(cyc() - t0); }
    report("falcon512", op, var, reps, st, 1, -1, 0, ""); return;
  }
  KronPlan p; if (v >= 0 && v < 4) { if (get_plan(CASE_FALCON_S2_H, c1, &p)) return; hw_for_stage(v); } else if (v >= 100) hw_on(); else hw_off();
  kron_mul_fn f = (v >= 0 && v < 4) ? polymul_for_stage(v) : nullptr;
  void* ctx = nullptr; fal_prod_fn prodfn = nullptr; int32_t* H = nullptr;
  if (v == 10 || v == 11 || v >= 100) {
    H = (int32_t*)pool_alloc(512 * 4); falcon_decode_h(fpk, H);
    if (v == 10 || v == 11) { ctx = tuned_fal_setup(v == 11, H); prodfn = tuned_fal_prod; }
    else { ctx = fast_fal_setup(H, c1, (uint32_t)(v - 100)); prodfn = fast_fal_prod; mem = fast_ctx_bytes(ctx); }
  }
  for (int r = 0; r < reps; r++) {
    const int i = r & 3; uint8_t m[48]; memcpy(m, fmsg[i], 48); const bool tamper = r & 4; if (tamper) m[r % 48] ^= 1;     // half of the runs verify a tampered message (must reject)
    g_prod_cycles = 0; const uint32_t t0 = cyc(); int rc;
    if (v == -2) rc = falcon_verify_ref(fsig[i], fslen[i], m, 48, fpk);
    else if (prodfn) rc = falcon_verify_fn(fsig[i], fslen[i], m, 48, fpk, prodfn, ctx);
    else rc = falcon_verify_var(fsig[i], fslen[i], m, 48, fpk, f, v >= 0 ? &p : nullptr);
    st.add(cyc() - t0); prod += g_prod_cycles / MHZ;
    if ((rc == 0) == tamper) ok = 0;
  }
  report("falcon512", op, var, reps, st, ok, v == -2 ? -1 : prod / reps, mem, "");
  if (v == 10 || v == 11) tuned_free(ctx); if (v >= 100) fast_free(ctx);
}

// ---------------------------------------------------------------- command loop
static char* tok(char** s) { while (**s == ' ') (*s)++; if (!**s) return nullptr; char* t = *s; while (**s && **s != ' ') (*s)++; if (**s) { **s = 0; (*s)++; } return t; }
static void handle(char* line) {
  pool_release(); va = vb = nullptr; g_vec = nullptr; g_out = nullptr;                    // command scratch is per command
  char* s = line; char* cmd = tok(&s); if (!cmd) return;
  char* a[8] = {0}; for (int i = 0; i < 8; i++) a[i] = tok(&s);
  auto I = [&](int i) { return a[i] ? atoi(a[i]) : 0; };
  if (!strcmp(cmd, "INFO")) {
    Serial.printf("CHIP model=%s rev=%d cores=%d cpu_mhz=%d heap_free=%u heap_largest=%u sdk=%s\n", ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores(), ESP.getCpuFreqMHz(),
                  (unsigned)ESP.getFreeHeap(), (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_8BIT), ESP.getSdkVersion());
  } else if (!strcmp(cmd, "PLAN") && a[0]) cmd_plan(find_case(a[0]), I(1));
  else if (!strcmp(cmd, "CHECK") && a[3]) cmd_check(find_case(a[0]), I(1), I(2), I(3));
  else if (!strcmp(cmd, "CHECKCPU") && a[1]) cmd_checkcpu(find_case(a[0]), I(1));
  else if (!strcmp(cmd, "BENCH") && a[3]) { const int ci = find_case(a[0]); if (ci < 0) Serial.println("ERR case"); else cmd_bench(ci, I(1), I(2), I(3)); }
  else if (!strcmp(cmd, "FAST") && a[4]) { const int ci = find_case(a[1]); if (ci < 0) Serial.println("ERR case"); else cmd_fast(a[0], ci, I(2), I(3), I(4)); }
  else if (!strcmp(cmd, "HWONLY") && a[3]) { const int ci = find_case(a[0]); if (ci < 0) Serial.println("ERR case"); else cmd_hwonly(ci, I(1), I(2), I(3)); }
  else if (!strcmp(cmd, "PROF") && a[2]) cmd_prof(find_case(a[0]), I(1), I(2));
  else if (!strcmp(cmd, "COMP") && a[1]) { const int ci = find_case(a[0]); if (ci < 0) Serial.println("ERR case"); else cmd_comp(ci, I(1)); }
  else if (!strcmp(cmd, "HWLAT")) cmd_hwlat();
  else if (!strcmp(cmd, "ACQ")) cmd_acq(I(0) ? I(0) : 100);
  else if (!strcmp(cmd, "E2E") && a[3]) {
    if (!strcmp(a[0], "kem")) e2e_kem(a[1], a[2], I(3), I(4) ? I(4) : 3);
    else if (!strcmp(a[0], "dsa")) e2e_dsa(a[1], a[2], I(3), I(4) ? I(4) : 2, I(5) ? I(5) : 3);
    else if (!strcmp(a[0], "falcon")) e2e_falcon(a[1], a[2], I(3), I(4) ? I(4) : 8);
    else Serial.println("ERR scheme");
  } else Serial.println("ERR unknown command");
  hw_off();
  Serial.println("END");
}

static void worker(void*) {
  static char buf[160]; int n = 0;
  Serial.println("READY pqc-mpi");
  for (;;) {
    while (Serial.available()) {
      const int ch = Serial.read();
      if (ch == '\n' || ch == '\r') { if (n) { buf[n] = 0; n = 0; handle(buf); } }
      else if (n < (int)sizeof buf - 1) buf[n++] = (char)ch;
    }
    vTaskDelay(1);
  }
}

void setup() {
  Serial.begin(115200); delay(300);
  for (int i = 0; i < 4; i++) dsig_ref[i] = (uint8_t*)malloc(DSA_SIG);
  cpu_tuned_prepare();
  xTaskCreatePinnedToCore(worker, "pqc", 98304, nullptr, 1, nullptr, 1);
}
void loop() { vTaskDelay(1000); }
