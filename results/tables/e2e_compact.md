| operation | PQClean (ms) | PQClean NTT copy (ms) | tuned NTT (ms) | tuned NTT, key cached (ms) | MPI mbedtls API (ms) | MPI overlap stage (ms) | MPI optimised (ms) | mbedtls API / optimised | optimised / NTT copy | optimised / tuned NTT | optimised / tuned, key cached | outputs identical |
|---|---|---|---|---|---|---|---|---|---|---|---|---|
| ML-KEM-768 decapsulation | 12.83 | 12.83 | 12.02 |  | 17.82 | 13.41 | 13.11 | 1.36 | 1.02 | 1.09 |  | yes |
| ML-KEM-768 decrypt step | 1.43 | 1.43 | 0.60 |  | 6.15 | 1.87 | 1.51 | 4.06 | 1.06 | 2.53 |  | yes |
| ML-DSA-44 signing | 40.11 | 40.13 | 36.94 | 35.84 | 52.27 | 38.81 | 36.45 | 1.43 | 0.91 | 0.99 | 1.02 | yes |
| Falcon-512 verification | 4.98 | 4.84 | 4.24 | 4.03 | 17.74 | 7.23 | 6.70 | 2.65 | 1.38 | 1.58 | 1.66 | yes |
