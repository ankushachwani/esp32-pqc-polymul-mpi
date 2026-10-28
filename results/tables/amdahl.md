| operation | baseline | baseline total (ms) | share of time in the product steps | product steps: optimised MPI speedup (x) | bound if the products cost nothing (x) | bound at the measured product speedup (x) | measured whole-scheme speedup (x) |
|---|---|---|---|---|---|---|---|
| mlkem768 dec | copy of it, PQClean NTT | 12.83 | 0.10 | 0.89 | 1.11 | 0.99 | 0.98 |
| mlkem768 dec | tuned NTT | 12.02 | 0.04 | 0.31 | 1.04 | 0.92 | 0.92 |
| mlkem768 indcpa | copy of it, PQClean NTT | 1.43 | 0.91 | 0.95 | 11.09 | 0.96 | 0.94 |
| mlkem768 indcpa | tuned NTT | 0.60 | 0.75 | 0.33 | 3.96 | 0.40 | 0.40 |
| mldsa44 sign | copy of it, PQClean NTT | 40.13 | 0.19 | 1.98 | 1.24 | 1.11 | 1.10 |
| mldsa44 sign | tuned NTT | 36.94 | 0.12 | 1.16 | 1.14 | 1.02 | 1.01 |
| mldsa44 sign | tuned NTT, key transform cached | 35.84 | 0.10 | 0.88 | 1.11 | 0.99 | 0.98 |
| falcon512 verify | copy of it, PQClean NTT | 4.84 | 0.28 | 0.42 | 1.40 | 0.72 | 0.72 |
| falcon512 verify | tuned NTT | 4.24 | 0.19 | 0.24 | 1.23 | 0.63 | 0.63 |
| falcon512 verify | tuned NTT, key transform cached | 4.03 | 0.14 | 0.18 | 1.17 | 0.60 | 0.60 |
