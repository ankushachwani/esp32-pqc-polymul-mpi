| case | mbedtls API (us) | + limb I/O (us) | + direct driver (us) | + overlap (us) | optimised (us) | optimised plan |
|---|---|---|---|---|---|---|
| mlkem_x_cbd | 1902.32 | 1100.08 | 745.97 | 561.94 | 437.76 | 3 chunks |
| mlkem_full | 3638.81 | 2067.45 | 1463.19 | 1009.11 | 910.52 | 4 chunks |
| mldsa_c_s | 878.28 | 554.82 | 387.09 | 307.03 | 193.24 | 3 chunks |
| mldsa_c_t0 | 1996.28 | 1113.22 | 757.79 | 567.33 | 450.51 | 3 chunks |
| mldsa_full | 7809.63 | 4262.84 | 2907.04 | 2139.03 | 1908.80 | 6 chunks |
| falcon512_s2_h | 14226.55 | 7910.10 | 5508.81 | 3660.87 | 3245.90 | 8 chunks |
