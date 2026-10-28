| case | PQClean NTT full (us) | PQClean NTT, key side transformed (us) | tuned NTT full (us) | tuned NTT, key side transformed (us) | mbedtls API (us) | + overlap (us) | optimised (us) | mbedtls API / optimised | optimised / PQClean full | optimised / PQClean pre | optimised / tuned full | optimised / tuned pre | mismatches |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| mlkem_x_cbd | 871.77 | 664.11 | 303.77 | 230.50 | 1902.32 | 561.94 | 437.76 | 4.35 | 0.50 | 0.66 | 1.44 | 1.90 | 0 |
| mlkem_full | 871.80 | 663.92 | 303.86 | 230.48 | 3638.81 | 1009.11 | 910.52 | 4.00 | 1.04 | 1.37 | 3.00 | 3.95 | 0 |
| mldsa_c_s | 738.28 | 525.61 | 376.23 | 285.08 | 878.28 | 307.03 | 193.24 | 4.55 | 0.26 | 0.37 | 0.51 | 0.68 | 0 |
| mldsa_c_t0 | 738.24 | 525.50 | 376.25 | 285.07 | 1996.28 | 567.33 | 450.51 | 4.43 | 0.61 | 0.86 | 1.20 | 1.58 | 0 |
| mldsa_full | 738.30 | 525.54 | 376.27 | 285.07 | 7809.63 | 2139.03 | 1908.80 | 4.09 | 2.59 | 3.63 | 5.07 | 6.70 | 0 |
| falcon512_s2_h | 1304.43 | 987.13 | 776.74 | 578.02 | 14226.55 | 3660.87 | 3245.90 | 4.38 | 2.49 | 3.29 | 4.18 | 5.62 | 0 |
