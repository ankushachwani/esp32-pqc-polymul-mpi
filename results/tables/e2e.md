| scheme | op | variant | mean_us | std_us | vs PQClean | vs NTT copy | us per rejection pass | output identical | reps |
|---|---|---|---|---|---|---|---|---|---|
| mlkem768 | keygen | PQClean as shipped | 9909.00 | 42.50 | 1.00 | 1.00 |  | yes | 80 |
| mlkem768 | enc | PQClean as shipped | 11109.50 | 42.00 | 1.00 | 1.00 |  | yes | 80 |
| mlkem768 | dec | PQClean as shipped | 12714.80 | 55.60 | 1.00 | 1.00 |  | yes | 80 |
| mlkem768 | dec | copy of it, NTT | 12718.60 | 51.40 | 1.00 | 1.00 |  | yes | 80 |
| mlkem768 | dec | product on MPI, mbedtls API | 17536.30 | 72.00 | 1.38 | 1.38 |  | yes | 80 |
| mlkem768 | dec | product on MPI, + limb I/O | 14812.20 | 69.20 | 1.16 | 1.16 |  | yes | 80 |
| mlkem768 | dec | product on MPI, + direct driver | 13875.70 | 56.80 | 1.09 | 1.09 |  | yes | 80 |
| mlkem768 | dec | product on MPI, + overlap | 13362.10 | 49.30 | 1.05 | 1.05 |  | yes | 80 |
| mlkem768 | indcpa | PQClean as shipped | 1426.60 | 4.80 | 1.00 | 1.00 |  | yes | 320 |
| mlkem768 | indcpa | copy of it, NTT | 1426.40 | 4.40 | 1.00 | 1.00 |  | yes | 320 |
| mlkem768 | indcpa | product on MPI, mbedtls API | 6103.60 | 13.40 | 4.28 | 4.28 |  | yes | 320 |
| mlkem768 | indcpa | product on MPI, + limb I/O | 3454.60 | 9.50 | 2.42 | 2.42 |  | yes | 320 |
| mlkem768 | indcpa | product on MPI, + direct driver | 2441.00 | 5.70 | 1.71 | 1.71 |  | yes | 320 |
| mlkem768 | indcpa | product on MPI, + overlap | 1910.00 | 7.00 | 1.34 | 1.34 |  | yes | 320 |
| mldsa44 | verify | PQClean as shipped | 22071.80 | 13.60 | 1.00 | 1.00 |  | yes | 32 |
| mldsa44 | sign | PQClean as shipped | 39818.20 | 17219.20 | 1.00 | 0.99 | 19909.10 | yes | 32 |
| mldsa44 | sign | copy of it, NTT | 40037.70 | 17346.90 | 1.01 | 1.00 | 20018.85 | yes | 32 |
| mldsa44 | sign | product on MPI, mbedtls API | 51737.40 | 22253.30 | 1.30 | 1.29 | 25868.70 | yes | 32 |
| mldsa44 | sign | product on MPI, + limb I/O | 43893.20 | 19409.80 | 1.10 | 1.10 | 21946.60 | yes | 32 |
| mldsa44 | sign | product on MPI, + direct driver | 40460.60 | 17947.20 | 1.02 | 1.01 | 20230.30 | yes | 32 |
| mldsa44 | sign | product on MPI, + overlap | 38762.20 | 17345.80 | 0.97 | 0.97 | 19381.10 | yes | 32 |
| falcon512 | sign | PQClean as shipped | 365923.50 | 1128.10 | 1.00 | 1.00 |  | yes | 8 |
| falcon512 | verify | PQClean as shipped | 4937.90 | 52.80 | 1.00 | 1.04 |  | yes | 128 |
| falcon512 | verify | copy of it, NTT | 4742.10 | 36.60 | 0.96 | 1.00 |  | yes | 128 |
| falcon512 | verify | product on MPI, mbedtls API | 17678.60 | 40.50 | 3.58 | 3.73 |  | yes | 128 |
| falcon512 | verify | product on MPI, + limb I/O | 11373.20 | 36.20 | 2.30 | 2.40 |  | yes | 128 |
| falcon512 | verify | product on MPI, + direct driver | 8933.70 | 38.10 | 1.81 | 1.88 |  | yes | 128 |
| falcon512 | verify | product on MPI, + overlap | 7190.40 | 24.20 | 1.46 | 1.52 |  | yes | 128 |
