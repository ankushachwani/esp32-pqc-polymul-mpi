| scheme | op | variant | mean_us | std_us | vs PQClean | vs NTT copy | us per rejection pass | product steps (us) | prepacked key (bytes) | output identical | reps |
|---|---|---|---|---|---|---|---|---|---|---|---|
| mlkem768 | keygen | PQClean as shipped | 9992.20 | 43.80 | 1.00 | 1.00 |  |  | 0 | yes | 80 |
| mlkem768 | enc | PQClean as shipped | 11198.40 | 37.20 | 1.00 | 1.00 |  |  | 0 | yes | 80 |
| mlkem768 | dec | PQClean as shipped | 12826.70 | 45.30 | 1.00 | 1.00 |  |  | 0 | yes | 80 |
| mlkem768 | dec | copy of it, PQClean NTT | 12826.20 | 43.00 | 1.00 | 1.00 |  | 1298.60 | 0 | yes | 80 |
| mlkem768 | dec | tuned NTT | 12021.90 | 41.30 | 0.94 | 0.94 |  | 456.50 | 0 | yes | 80 |
| mlkem768 | dec | MPI: mbedtls API | 17823.30 | 60.50 | 1.39 | 1.39 |  | 6152.00 | 0 | yes | 80 |
| mlkem768 | dec | MPI: + limb I/O | 15146.60 | 53.50 | 1.18 | 1.18 |  | 3487.70 | 0 | yes | 80 |
| mlkem768 | dec | MPI: + direct driver | 13959.70 | 57.50 | 1.09 | 1.09 |  | 2342.20 | 0 | yes | 80 |
| mlkem768 | dec | MPI: + overlap | 13406.20 | 59.20 | 1.05 | 1.05 |  | 1787.20 | 0 | yes | 80 |
| mlkem768 | dec | MPI engine, no switch | 13268.20 | 67.50 | 1.03 | 1.03 |  | 1659.10 | 4608 | yes | 80 |
| mlkem768 | dec | MPI optimised (all switches) | 13129.10 | 68.90 | 1.02 | 1.02 |  | 1480.30 | 4608 | yes | 80 |
| mlkem768 | dec | MPI optimised (all but the fused sum) | 13114.30 | 30.00 | 1.02 | 1.02 |  | 1461.30 | 4608 | yes | 80 |
| mlkem768 | indcpa | PQClean as shipped | 1426.70 | 5.70 | 1.00 | 1.00 |  |  | 0 | yes | 320 |
| mlkem768 | indcpa | copy of it, PQClean NTT | 1426.60 | 5.20 | 1.00 | 1.00 |  | 1298.00 | 0 | yes | 320 |
| mlkem768 | indcpa | tuned NTT | 599.40 | 4.50 | 0.42 | 0.42 |  | 448.10 | 0 | yes | 320 |
| mlkem768 | indcpa | MPI: mbedtls API | 6153.00 | 17.40 | 4.31 | 4.31 |  | 6024.80 | 0 | yes | 320 |
| mlkem768 | indcpa | MPI: + limb I/O | 3487.70 | 14.10 | 2.44 | 2.44 |  | 3359.20 | 0 | yes | 320 |
| mlkem768 | indcpa | MPI: + direct driver | 2426.50 | 7.20 | 1.70 | 1.70 |  | 2297.80 | 0 | yes | 320 |
| mlkem768 | indcpa | MPI: + overlap | 1874.30 | 7.70 | 1.31 | 1.31 |  | 1745.50 | 0 | yes | 320 |
| mlkem768 | indcpa | MPI engine, no switch | 1772.00 | 8.70 | 1.24 | 1.24 |  | 1620.80 | 4608 | yes | 320 |
| mlkem768 | indcpa | MPI optimised (all switches) | 1577.80 | 9.90 | 1.11 | 1.11 |  | 1418.30 | 4608 | yes | 320 |
| mlkem768 | indcpa | MPI optimised (all but the fused sum) | 1514.60 | 10.90 | 1.06 | 1.06 |  | 1361.20 | 4608 | yes | 320 |
| mldsa44 | verify | PQClean as shipped | 22201.20 | 14.80 | 1.00 | 1.00 |  |  | 0 | yes | 32 |
| mldsa44 | sign | PQClean as shipped | 40114.90 | 17349.40 | 1.00 | 1.00 | 20057.45 |  | 0 | yes | 32 |
| mldsa44 | sign | copy of it, PQClean NTT | 40128.50 | 17359.70 | 1.00 | 1.00 | 20064.25 | 7778.10 | 0 | yes | 32 |
| mldsa44 | sign | tuned NTT | 36940.60 | 16385.90 | 0.92 | 0.92 | 18470.30 | 4574.50 | 0 | yes | 32 |
| mldsa44 | sign | tuned NTT, key transform cached | 35845.00 | 16386.10 | 0.89 | 0.89 | 17922.50 | 3480.80 | 0 | yes | 32 |
| mldsa44 | sign | MPI: mbedtls API | 52270.30 | 22433.10 | 1.30 | 1.30 | 26135.15 | 19737.60 | 0 | yes | 32 |
| mldsa44 | sign | MPI: + limb I/O | 44377.30 | 19541.80 | 1.11 | 1.11 | 22188.65 | 11892.20 | 0 | yes | 32 |
| mldsa44 | sign | MPI: + direct driver | 40648.40 | 18028.00 | 1.01 | 1.01 | 20324.20 | 8154.00 | 0 | yes | 32 |
| mldsa44 | sign | MPI: + overlap | 38813.30 | 17316.20 | 0.97 | 0.97 | 19406.65 | 6337.10 | 0 | yes | 32 |
| mldsa44 | sign | MPI engine, no switch | 39394.10 | 17826.70 | 0.98 | 0.98 | 19697.05 | 6958.80 | 18480 | yes | 32 |
| mldsa44 | sign | MPI optimised (all switches) | 36452.80 | 16105.00 | 0.91 | 0.91 | 18226.40 | 3933.30 | 18480 | yes | 32 |
| falcon512 | sign | PQClean as shipped | 365977.10 | 1122.40 | 1.00 | 1.00 |  |  | 0 | yes | 8 |
| falcon512 | verify | PQClean as shipped | 4984.60 | 48.20 | 1.00 | 1.03 |  |  | 0 | yes | 128 |
| falcon512 | verify | copy of it, PQClean NTT | 4841.50 | 30.40 | 0.97 | 1.00 |  | 1373.80 | 0 | yes | 128 |
| falcon512 | verify | tuned NTT | 4239.40 | 21.00 | 0.85 | 0.88 |  | 785.20 | 0 | yes | 128 |
| falcon512 | verify | tuned NTT, key transform cached | 4034.00 | 16.90 | 0.81 | 0.83 |  | 584.20 | 0 | yes | 128 |
| falcon512 | verify | MPI: mbedtls API | 17740.50 | 41.10 | 3.56 | 3.66 |  | 14277.90 | 0 | yes | 128 |
| falcon512 | verify | MPI: + limb I/O | 11411.60 | 37.00 | 2.29 | 2.36 |  | 7952.60 | 0 | yes | 128 |
| falcon512 | verify | MPI: + direct driver | 8956.00 | 34.70 | 1.80 | 1.85 |  | 5515.60 | 0 | yes | 128 |
| falcon512 | verify | MPI: + overlap | 7225.40 | 29.10 | 1.45 | 1.49 |  | 3704.10 | 0 | yes | 128 |
| falcon512 | verify | MPI engine, no switch | 6842.30 | 41.20 | 1.37 | 1.41 |  | 3402.00 | 6152 | yes | 128 |
| falcon512 | verify | MPI optimised (all switches) | 6703.80 | 32.50 | 1.34 | 1.38 |  | 3253.30 | 6152 | yes | 128 |
