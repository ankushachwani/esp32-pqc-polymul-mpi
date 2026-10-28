// Scheme-level code on top of PQClean (clean C): the NTT baselines for one polynomial product, and the three whole-scheme operations
// that have an accelerated variant. Portable C, shared by the host tests and the firmware. The product itself is a kron_mul_fn callback.
#ifndef SCHEMES_H
#define SCHEMES_H
#include <stddef.h>
#include <stdint.h>
#include "kron.h"
#ifdef __cplusplus
extern "C" {
#endif

// Cycle counter for the in-situ timing of the product steps inside the scheme variants (firmware: ESP.getCycleCount; host tests: 0).
uint32_t prof_now(void);
extern uint32_t g_prod_cycles;          // cycles spent in the replaceable product steps since it was last cleared

// Polynomial-product cases (ranges are the guaranteed ranges of the real operands, see host/kron_proto.py)
enum { CASE_MLKEM_X_CBD, CASE_MLKEM_FULL, CASE_MLDSA_C_S, CASE_MLDSA_C_T0, CASE_MLDSA_FULL, CASE_FALCON_S2_H, N_CASES };
extern const KronCase KRON_CASES[N_CASES];

// NTT-based product of the same two polynomials, split into the parts the scheme code uses. load() copies the operands into the scheme's own
// types outside the timed region; full() = transform both, pointwise, inverse; pre() = the same with the second operand already transformed
// (it is, in load()); in every case the second operand is the fixed one; get() returns the product in [0, q). ntt()/pointwise()/intt() are the parts on their own.
typedef struct { void (*load)(const int32_t* a, const int32_t* b); void (*full)(void); void (*pre)(void); void (*get)(int32_t* out);
                 void (*ntt)(void); void (*pointwise)(void); void (*intt)(void); } CpuPoly;
extern const CpuPoly CPU_MLKEM, CPU_MLDSA, CPU_FALCON;
const CpuPoly* cpu_poly_for_case(int c);
extern const CpuPoly CPUT_MLKEM, CPUT_MLDSA, CPUT_FALCON;      // same interface, tuned NTT (ntt_tuned.c)
const CpuPoly* cpu_tuned_for_case(int c);
void cpu_tuned_prepare(void);           // allocate its tables now (heap is fragmented later)

// Product hooks for the optimised and tuned variants. Each receives a context holding whatever was prepared for the key (packed secret, transformed secret).
typedef void (*kem_dot_fn)(void* ctx, const int32_t* u /* 3 x 256 */, int32_t* out /* 256, in [0, q) */);                 // out = sum_i s_i * u_i
typedef struct {
  void* ctx;
  void (*begin)(void* ctx, const int32_t* s1 /* 4 x 256 */, const int32_t* s2 /* 4 x 256 */, const int32_t* t0 /* 4 x 256 */);   // called once per signature, after the key is unpacked
  void (*prod)(void* ctx, const int32_t* c, int which /* 0: c*s1, 1: c*s2, 2: c*t0 */, int32_t* out /* 4 x 256, in [0, q) */);
} dsa_ops;
typedef void (*fal_prod_fn)(void* ctx, const int32_t* h /* 512, in [0, q) */, const int32_t* s2 /* 512 */, int32_t* out /* 512, in [0, q) */);

// Tuned-NTT versions of the hooks (cpu_tuned.c). setup() is outside the timed region; `cached` = the transform of the key material is done in setup
// and kept (otherwise it is redone inside every call, as PQClean does for ML-DSA and Falcon).
void* tuned_kem_setup(const int32_t* s_std /* 3 x 256 */);
void tuned_kem_dot(void* ctx, const int32_t* u, int32_t* out);
void* tuned_dsa_setup(int cached, const int32_t* s1, const int32_t* s2, const int32_t* t0);
void tuned_dsa_begin(void* ctx, const int32_t* s1, const int32_t* s2, const int32_t* t0);
void tuned_dsa_prod(void* ctx, const int32_t* c, int which, int32_t* out);
void* tuned_fal_setup(int cached, const int32_t* h);
void tuned_fal_prod(void* ctx, const int32_t* h, const int32_t* s2, int32_t* out);
void tuned_free(void* ctx);
void tuned_dsa_free(void* ctx);          // for a context from tuned_dsa_setup

// ML-KEM-768
#define KEM_PK 1184
#define KEM_SK 2400
#define KEM_CT 1088
#define KEM_SS 32
int kem_keypair(uint8_t* pk, uint8_t* sk);
int kem_enc(uint8_t* ct, uint8_t* ss, const uint8_t* pk);
int kem_dec_ref(uint8_t* ss, const uint8_t* ct, const uint8_t* sk);                 // PQClean as shipped
void kem_sk_std(const uint8_t* sk, int32_t* s_std);                                  // secret vector from NTT domain to coefficients (3 x 256, in [-2, 2])
// Decapsulation with the decryption product s^T u done in coefficient form. mul == NULL runs the same code with the NTT.
int kem_dec_var(uint8_t* ss, const uint8_t* ct, const uint8_t* sk, const int32_t* s_std, kron_mul_fn mul, const KronPlan* plan);
int kem_dec_fn(uint8_t* ss, const uint8_t* ct, const uint8_t* sk, kem_dot_fn dot, void* ctx);
void kem_indcpa_dec_fn(uint8_t* m, const uint8_t* ct, kem_dot_fn dot, void* ctx);
void kem_indcpa_dec_ref(uint8_t* m, const uint8_t* ct, const uint8_t* sk);
void kem_indcpa_dec_var(uint8_t* m, const uint8_t* ct, const uint8_t* sk, const int32_t* s_std, kron_mul_fn mul, const KronPlan* plan);

// ML-DSA-44
#define DSA_PK 1312
#define DSA_SK 2560
#define DSA_SIG 2420
int dsa_keypair(uint8_t* pk, uint8_t* sk);
int dsa_sign_ref(uint8_t* sig, size_t* siglen, const uint8_t* m, size_t mlen, const uint8_t* sk);
// Signing with c*s1, c*s2, c*t0 in coefficient form. mul == NULL runs the same code with the NTT. attempts = passes through the rejection loop.
int dsa_sign_var(uint8_t* sig, size_t* siglen, const uint8_t* m, size_t mlen, const uint8_t* sk, kron_mul_fn mul, const KronPlan* plan_cs,
                 const KronPlan* plan_ct0, int* attempts);
int dsa_sign_ops(uint8_t* sig, size_t* siglen, const uint8_t* m, size_t mlen, const uint8_t* sk, const dsa_ops* ops, int* attempts);
void dsa_unpack_secret(const uint8_t* sk, int32_t* s1, int32_t* s2, int32_t* t0);          // coefficient-form secret vectors, for preparing a key
int dsa_verify(const uint8_t* sig, size_t siglen, const uint8_t* m, size_t mlen, const uint8_t* pk);

// Falcon-512
#define FAL_PK 897
#define FAL_SK 1281
#define FAL_SIG 752
int falcon_keypair(uint8_t* pk, uint8_t* sk);
int falcon_sign(uint8_t* sig, size_t* siglen, const uint8_t* m, size_t mlen, const uint8_t* sk);
int falcon_verify_ref(const uint8_t* sig, size_t siglen, const uint8_t* m, size_t mlen, const uint8_t* pk);
// Verify with h*s2 in coefficient form (no NTT of the public key). mul == NULL: the same code with the NTT path.
int falcon_verify_var(const uint8_t* sig, size_t siglen, const uint8_t* m, size_t mlen, const uint8_t* pk, kron_mul_fn mul, const KronPlan* plan);

int falcon_verify_fn(const uint8_t* sig, size_t siglen, const uint8_t* m, size_t mlen, const uint8_t* pk, fal_prod_fn prod, void* ctx);
void falcon_decode_h(const uint8_t* pk, int32_t* h);                                      // public key polynomial, coefficients in [0, q)

#ifdef __cplusplus
}
#endif
#endif
