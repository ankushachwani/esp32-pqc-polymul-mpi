| operation | PQClean (ms) | NTT copy (ms) | mbedtls API (ms) | + limb I/O (ms) | + direct driver (ms) | + overlap (ms) | overlap / NTT copy | outputs identical |
|---|---|---|---|---|---|---|---|---|
| mlkem768 decapsulation | 12.71 | 12.72 | 17.54 | 14.81 | 13.88 | 13.36 | 1.05 | yes |
| mlkem768 decrypt step of decapsulation | 1.43 | 1.43 | 6.10 | 3.45 | 2.44 | 1.91 | 1.34 | yes |
| mldsa44 sign | 39.82 | 40.04 | 51.74 | 43.89 | 40.46 | 38.76 | 0.97 | yes |
| falcon512 verify | 4.94 | 4.74 | 17.68 | 11.37 | 8.93 | 7.19 | 1.52 | yes |
