#include "schemes.h"
// Operand ranges are the guaranteed ranges in the schemes, see the notes in host/kron_proto.py.
// The first operand is the one that varies from call to call, the second the fixed one (key, matrix, secret); the optimised path prepacks the second.
const KronCase KRON_CASES[N_CASES] = {
  {"mlkem_x_cbd",   256, 3329u,    0,       3328,    -2,      2},
  {"mlkem_full",    256, 3329u,    0,       3328,    0,       3328},
  {"mldsa_c_s",     256, 8380417u, -1,      1,       -2,      2},
  {"mldsa_c_t0",    256, 8380417u, -1,      1,       -4095,   4096},
  {"mldsa_full",    256, 8380417u, -131071, 131072,  0,       8380416},
  {"falcon512_s2_h", 512, 12289u,  -2047,   2047,    0,       12288},
};
const CpuPoly* cpu_poly_for_case(int c) { return c <= CASE_MLKEM_FULL ? &CPU_MLKEM : c <= CASE_MLDSA_FULL ? &CPU_MLDSA : &CPU_FALCON; }
