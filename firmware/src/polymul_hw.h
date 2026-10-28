// The four ways of running the packed polynomial product on the ESP32, one per optimisation step of the RSA project:
//   S_MBEDTLS  public mbedtls API: operands copied in with read_binary, product copied out with write_binary
//   S_LIMB     operands packed straight into the mpi limbs, product read straight out of them (still mbedtls_mpi_mul_mpi)
//   S_DRIVER   direct register driver (mpi_hw.h), synchronous: load, start, wait, read
//   S_OVERLAP  as S_DRIVER but the CPU packs the next operands / unpacks the previous product while the multiplier runs
// S_DRIVER and S_OVERLAP expect the unit to be acquired (mpihw::acquire) around a batch of calls; S_MBEDTLS and S_LIMB expect it released.
#pragma once
#include "kron.h"
enum Stage { S_MBEDTLS = 0, S_LIMB = 1, S_DRIVER = 2, S_OVERLAP = 3, N_STAGES = 4 };
extern const char* const STAGE_NAME[N_STAGES];
kron_mul_fn polymul_for_stage(int stage);
bool stage_needs_hw(int stage);

struct Prof { uint32_t pack, prefix, load, compute, read, accum, finish; };   // CPU cycles, S_DRIVER only
extern Prof g_prof;
void polymul_driver_profiled(const KronPlan* p, const int32_t* a, const int32_t* b, int32_t* out);
