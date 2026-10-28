# Polynomial products for ML-KEM, ML-DSA and Falcon on the ESP32 RSA/MPI unit

The ESP32 has a hardware big-integer multiplier (the "RSA/MPI" unit, up to 2048 x 2048 bits). In an earlier team course project (Cryptonite) we ran neural-network layers on it as
big-integer multiplies (Kronecker packing) and got a 3.9x speed-up from three changes to the data path around the multiply, while still ending up
slower than a plain CPU kernel. This repo tries the same idea on post-quantum cryptography: pack a negacyclic polynomial product into integer multiplies on the unit,
apply the same three optimisations, and compare against the PQClean clean-C NTT on the same board. All numbers are from one ESP32-D0WD-V3 (240 MHz).

Short version: the three optimisations help again (2.8x to 3.9x per polynomial product), and for operands with small coefficients the packed product
beats a from-scratch NTT product. At scheme level there is no gain: ML-KEM-768 decapsulation is 5% slower, ML-DSA-44 signing is 3% faster, Falcon-512 verification is 46% to 52% slower
than the PQClean NTT code (shipped function / my copy of it). Where the operands are full-width (A x y in ML-DSA, h x s2 in Falcon) the packed product is about 2.8x slower than the NTT even after all three
optimisations. I did not find a configuration where this would be worth deploying.

## Results

### One polynomial product (microseconds, mean of 26 to 400 repetitions, relative standard deviation at most 1.2%; every output checked bit for bit)

NTT full = forward NTT of both operands, pointwise product, inverse NTT (PQClean, clean C). NTT pre = the same with the first operand already transformed (what the schemes do with
a fixed key or matrix). The four MPI columns are the cumulative stages described below, each at its best plan (number of chunks per operand, chosen by measurement over every feasible plan).

<!-- begin:polymul_latency -->
| case | NTT full (us) | NTT, a pre-transformed (us) | mbedtls API (us) | + limb I/O (us) | + direct driver (us) | + overlap (us) | overlap / NTT full | overlap / NTT pre | S0 -> S3 speedup | mismatches |
|---|---|---|---|---|---|---|---|---|---|---|
| mlkem_x_cbd | 871.85 | 664.08 | 1888.26 | 1088.79 | 750.64 | 567.37 | 0.65 | 0.85 | 3.33 | 0 |
| mlkem_full | 871.77 | 664.03 | 3638.82 | 2065.58 | 1463.08 | 1009.07 | 1.16 | 1.52 | 3.61 | 0 |
| mldsa_c_s | 738.26 | 525.53 | 864.20 | 541.46 | 393.01 | 312.43 | 0.42 | 0.59 | 2.77 | 0 |
| mldsa_c_t0 | 738.24 | 525.51 | 1985.90 | 1096.19 | 763.35 | 572.55 | 0.78 | 1.09 | 3.47 | 0 |
| mldsa_full | 738.31 | 525.53 | 7763.72 | 4220.15 | 2870.12 | 2096.02 | 2.84 | 3.99 | 3.70 | 0 |
| falcon512_h_s2 | 1304.71 | 995.35 | 14143.75 | 7835.80 | 5438.44 | 3591.93 | 2.75 | 3.61 | 3.94 | 0 |
<!-- end:polymul_latency -->

Operand ranges, slot widths and multiplies per product (from the host prototype, identical to what the firmware computes):

<!-- begin:packing_plan -->
| case | n | q | a range | b range | chunks per operand | coeffs per chunk | slot bits | padded operand bits | MPI multiplies |
|---|---|---|---|---|---|---|---|---|---|
| mlkem_x_cbd | 256 | 3329 | [0,3328] | [-2,2] | 3 | 86 | 21 | 2048 | 9 |
| mlkem_full | 256 | 3329 | [0,3328] | [0,3328] | 4 | 64 | 30 | 2048 | 16 |
| mldsa_c_s | 256 | 8380417 | [-1,1] | [-2,2] | 2 | 128 | 11 | 1536 | 4 |
| mldsa_c_t0 | 256 | 8380417 | [-1,1] | [-4095,4096] | 3 | 86 | 21 | 2048 | 9 |
| mldsa_full | 256 | 8380417 | [0,8380416] | [-131071,131072] | 6 | 43 | 47 | 2048 | 36 |
| falcon512_h_s2 | 512 | 12289 | [0,12288] | [-2047,2047] | 8 | 64 | 32 | 2048 | 64 |
<!-- end:packing_plan -->

![latency](results/figures/polymul_latency.png)

### Whole-scheme operations (milliseconds; the product is swapped, everything else is PQClean unchanged; outputs identical to PQClean in every run)

<!-- begin:e2e_compact -->
| operation | PQClean (ms) | NTT copy (ms) | mbedtls API (ms) | + limb I/O (ms) | + direct driver (ms) | + overlap (ms) | overlap / NTT copy | outputs identical |
|---|---|---|---|---|---|---|---|---|
| mlkem768 decapsulation | 12.71 | 12.72 | 17.54 | 14.81 | 13.88 | 13.36 | 1.05 | yes |
| mlkem768 decrypt step of decapsulation | 1.43 | 1.43 | 6.10 | 3.45 | 2.44 | 1.91 | 1.34 | yes |
| mldsa44 sign | 39.82 | 40.04 | 51.74 | 43.89 | 40.46 | 38.76 | 0.97 | yes |
| falcon512 verify | 4.94 | 4.74 | 17.68 | 11.37 | 8.93 | 7.19 | 1.52 | yes |
<!-- end:e2e_compact -->

"NTT copy" is my copy of the PQClean function with the NTT product still in it; it exists to show the wrapper costs nothing (it is within 1% of the shipped function for ML-KEM and ML-DSA,
4% faster for Falcon verification, which I did not chase down). The "overlap / NTT copy" column is the like-for-like ratio. Full table with spreads: `results/tables/e2e.md`.
ML-DSA signing time depends on how many times the rejection loop runs; the four test messages and RNG seeds are fixed, so every variant does the same number of passes (64 over 32 signatures, 2.0 per signature).

![whole scheme](results/figures/whole_scheme.png)

### Where the time goes

![breakdown](results/figures/breakdown.png)

Direct-driver stage, one product, no overlap (`results/tables/breakdown.md`). For the cases that use 9 or more multiplies the hardware multiply is 39% to 45% of the time,
loading the operands and reading the product another 13% to 15%, and the CPU work around it (packing, unpacking the slots, removing the shifts, reducing mod q) 39% to 47%. The multiplier
is therefore not the only problem: even a free multiply would leave the CPU part, which for ML-DSA A x y (1.12 ms) is already larger than the whole NTT product (0.74 ms).
Overlap hides part of the CPU work behind the multiplier, which is why the last stage is faster than the third.

### Multiplier alone (`results/tables/multiplier.md`)

<!-- begin:multiplier -->
| bits | mbedtls_us | direct_us | load_us | compute_us | read_us | mbedtls_exact | direct_exact |
|---|---|---|---|---|---|---|---|
| 512 | 25.27 | 6.09 | 2.23 | 2.92 | 0.94 | 1 | 1 |
| 1024 | 40.66 | 15.39 | 4.15 | 9.57 | 1.68 | 1 | 1 |
| 1536 | 60.08 | 29.56 | 6.22 | 20.83 | 2.50 | 1 | 1 |
| 2048 | 83.63 | 47.48 | 8.15 | 36.05 | 3.28 | 1 | 1 |
| 2560 | 485.65 | n/a | n/a | n/a | n/a | 1 | n/a |
| 3072 | 384.71 | n/a | n/a | n/a | n/a | 1 | n/a |
<!-- end:multiplier -->

A 2048 x 2048-bit multiply is 36 us of compute plus 8 us to load and 3 us to read: 47.5 us through the direct driver against 84 us through mbedtls. Above 2048 bits the unit cannot be used
(the Z block holds the 4096-bit product, `MAX_WORDS = 64` in `firmware/src/mpi_hw.h`); mbedtls then takes 486 us at 2560 bits and 385 us at 3072 bits, i.e. it no longer runs one hardware multiply
(I did not look at what it does instead). That is why a 256-coefficient operand with a 21-bit slot (5376 bits) has to be cut into chunks.

## What was done

**Packing.** Two polynomials a, b of n coefficients are shifted to be non-negative (a' = a + oa, b' = b + ob when a range has negative values), cut into c chunks of m coefficients, and each chunk is
written into an integer with one coefficient per w-bit slot. One integer multiply of two chunks gives, in slot k, the sum of a'_i b'_j over i + j = k. The slot width is the smallest w for
which m * max(a') * max(b') < 2^w, so no slot can carry into the next. The chunk products are added into a linear product of 2n - 1 slots, the shifts are removed
(P_k = P'_k - ob * sum of a in the window - oa * sum of b in the window - oa * ob * count; the window sums come from prefix sums), x^n = -1 is folded in and the result is reduced mod q.
`host/kron_proto.py` does this in Python and checks it against a schoolbook negacyclic product for every feasible plan, with random and extreme operands, and also checks that w - 1 bits breaks the worst case.

**Stages**, as in the RSA project (`firmware/src/polymul_hw.cpp`):

| stage | what changes |
|---|---|
| mbedtls API | each chunk is copied in with `mbedtls_mpi_read_binary_le`, multiplied with `mbedtls_mpi_mul_mpi`, copied out with `write_binary_le` |
| + limb I/O | chunks are packed straight into the limbs of the `mbedtls_mpi` structs and the product is unpacked straight from them; still `mbedtls_mpi_mul_mpi` |
| + direct driver | `mpi_hw.h`: operands stored into the unit's memory blocks, start, wait, product read back with plain loads (the unit is acquired once per benchmark run; acquire plus release measured at 6 us, not included in any product time) |
| + overlap | the CPU packs the chunks of the next multiply and unpacks the previous product while the unit runs; prefix sums are computed during the first multiply |

Packing is done once per operand (not once per chunk pair) in every stage.

**Scheme code** (`firmware/src/schemes_*.c`): the PQClean clean-C sources are used unchanged. The variants are copies of three functions with one product swapped:

* ML-KEM-768 decapsulation: the decryption product s^T u is done in coefficient form (u from decompression, s small). The secret key is stored in the NTT domain, so the coefficient form of s is
  derived once per key (`kem_sk_std`, not timed). The re-encryption inside decapsulation is unchanged, because it multiplies by the public matrix, which the standard defines in the NTT domain; swapping that
  would need an inverse NTT per matrix entry and gain nothing.
* ML-DSA-44 signing: c*s1, c*s2, c*t0 in coefficient form (c has coefficients in {-1, 0, 1}), which also removes the up-front NTTs of s1, s2, t0 and the NTT of c. The A*y product stays on the NTT.
* Falcon-512 verification: h*s2 in coefficient form (also removes the NTT of the public key).

Not swapped: key generation and encapsulation for ML-KEM, verification for ML-DSA (all multiply by matrices defined in the NTT domain), Falcon signing (floating-point FFT). Their PQClean times are in `results/tables/e2e.md`.

## Findings

1. **Small operands win a single product, wide operands lose.** With the NTT in clean C at 208 us forward and 329 us inverse (`results/tables/ntt_parts.md`), a full NTT product costs 872 us (ML-KEM) or 738 us (ML-DSA).
   The packed product of u (mod q) with a CBD vector takes 567 us, of c with s 312 us, of c with t0 573 us (9 multiplies of 2048 bits): faster than the NTT product from scratch,
   and for c x s and u x s also faster than the NTT product with one operand pre-transformed. With 30-bit or wider slots (16, 36, 64 multiplies) it is 1.2x to 2.8x slower than the NTT product from scratch.
2. **At scheme level the NTT keeps its structural advantages.** PQClean's decrypt step does 3 forward NTTs, 3 basemuls and a single inverse NTT for the sum of three products with a secret that is already transformed:
   3 x 208 + 3 x 89 + 329 = 1219 us of the 1427 us measured. Three packed products cost 1910 us. The unit gets the three products as three independent jobs and has no way to share the inverse transform.
   In ML-DSA signing the NTT path transforms s1, s2 and t0 at the start of every signature (12 NTTs, about 2.6 ms at the 213 us measured per NTT, out of 39.8 ms) and the packed path does not;
   that is the likely source of the 3% win, since a single packed c x t0 or u x s product costs more than the NTT product with its operand pre-transformed (table above). I did not isolate it.
   An implementation that kept the transformed secret between signatures (PQClean does not) would not have that saving; I did not measure that variant.
3. **The three optimisations transfer.** Per product, mbedtls API -> overlap is 2.8x to 3.9x (RSA project: 3.9x on neural-network layers). Step by step, over the six cases: limb I/O 1.6x to 1.8x,
   register driver 1.4x to 1.5x, overlap 1.3x to 1.5x. The total is smallest for ML-DSA c x s (2.8x), which has only 4 multiplies.
4. **The hardware multiplier is not the bottleneck by itself** (see "Where the time goes"). My first version of the pack/unpack/unshift code used 64-bit arithmetic everywhere; it made the CPU part larger than the multiply
   (`results/raw_first_pass/`, kept for that reason). The final code uses 32-bit accumulators when every intermediate is below 2^30 (the three narrow cases) and still uses 64-bit for the wide ones;
   I did not try to speed the wide path up, and the gain would be bounded by their CPU share (39% to 47%).
5. **Fewest multiplies is the best plan in every case and at every stage**, as the host prototype predicts (`results/tables/polymul_sweep.csv`); splitting into more, smaller chunks to use smaller (512-1536 bit) operands was always slower.

## Reproduce

```
# host: packing proof (Python) and the C code with a software multiplier standing in for the unit
python3 host/kron_proto.py
./tests/run_host_tests.sh

# board: copy the repo to the machine the ESP32 is plugged into, build, flash, measure (holds the board lock)
rsync -a --exclude .git --exclude .pio --exclude results -e "ssh -o BatchMode=yes -o HostKeyAlias=pi.local" ./ ankushachwani@10.0.0.201:~/pqc_opt/
ssh -o BatchMode=yes -o HostKeyAlias=pi.local ankushachwani@10.0.0.201 'cd ~/pqc_opt && ./run_all.sh'        # about 10 minutes; ./run_all.sh --quick for a 3 minute smoke run
rsync -a -e "ssh -o BatchMode=yes -o HostKeyAlias=pi.local" ankushachwani@10.0.0.201:~/pqc_opt/results/raw/ results/raw/

# tables and figures from the raw CSVs
python3 analysis/make_table.py && python3 analysis/plot.py && python3 analysis/embed_tables.py
```

Needs numpy, pandas, matplotlib (host), pyserial (on the Pi), PlatformIO (`~/.platformio/penv/bin/pio`, Arduino-ESP32 core with ESP-IDF 4.4.7 as in `firmware/platformio.ini`).
Serial commands are documented at the top of `firmware/src/main.cpp`; `host/cmd.py "INFO" "BENCH mlkem_x_cbd 3 3 100"` sends single commands.

## Layout

```
host/kron_proto.py     packing math in Python, exactness proof, slot widths and operand sizes -> results/tables/packing_plan.*
host/                  board.py (serial), run_bench.py (full measurement run), cmd.py
firmware/src/kron.*    packing, unpacking, shift removal (portable C)
firmware/src/mpi_hw.h  direct driver for the unit (from Cryptonite)
firmware/src/polymul_hw.cpp   the four stages
firmware/src/schemes_*.c      NTT baselines and the three whole-scheme variants on top of PQClean
firmware/src/main.cpp         serial benchmark protocol
tests/                 host tests (bit-exactness of kron.c, the NTT baselines and the three variants against PQClean)
analysis/              tables, figures, README table embedding
vendor/pqclean/        PQClean clean-C sources used (see vendor/README.md)
results/raw/           CSVs straight from the board       results/tables/  derived tables (.csv, .md)       results/figures/
results/raw_first_pass/  first complete run, before the CPU-side code was tuned
```

## Correctness checks

* `host/kron_proto.py`: all packings bit-exact against a schoolbook product; the plans (slot width, operand size, multiply count) it prints are the ones the firmware computes (76 of 76 compared in `analysis/make_table.py`).
* On the board (`results/raw/check.csv`, `check_cpu.csv`): 13,984 checks (46 random and extreme operand pairs for each of the 76 feasible case-and-chunking plans at each of the four stages), 0 mismatches against an O(n^2) reference computed on the board; the NTT baselines
  are checked the same way (912 checks, 0 mismatches).
* Whole schemes (`results/raw/e2e.csv`, column `ok`): every ML-KEM decapsulation (valid and corrupted ciphertexts, so implicit rejection is exercised), every ML-DSA signature (byte for byte) and every Falcon verification verdict (valid and tampered messages) is
  identical to PQClean's.
* `tests/run_host_tests.sh` repeats the whole-scheme comparisons on the host over more keys and messages (60 ciphertexts, 25 signatures, 8 Falcon signatures).
* The ML-KEM-768 and ML-DSA-44 PQClean timings here agree within 1% with the PQClean timings of the sibling repo `pqc_microcontroller` on the same board (decapsulation 12.71 vs 12.79 ms, ML-DSA verify 22.07 vs 22.19 ms; I read its latency CSV on the Pi while that run was still in progress).

## Limits of the numbers

* One board, one key and four messages for each scheme; ML-DSA signing times depend on the rejection loop (see above). Each product timing cycles through four random operand pairs; as far as I can tell times do not depend on the data, except in the wide cases through one 32-bit/64-bit branch in the final reduction.
* The CPU baseline is PQClean clean C compiled with `-O3`, no assembly. The LX6 has no SIMD and a 32-bit ALU; a hand-optimised NTT could be faster, which would only make the packed product look worse. The result should be read as "against clean C", not "against the best NTT on the ESP32".
* The packed product is also plain C around the multiplier (packing, unpacking, reduction); it is not hand-tuned either.
* The best plan per stage was picked from the same sweep that is reported; the plans differ by far more than the run-to-run spread, so I do not expect a selection effect.
* `kem_sk_std` (secret key to coefficient form) is done once per key and not timed. A device that stored its secret key in coefficient form would not pay it.
* The direct driver reads the product with plain loads, valid because only one core uses the unit (same as in Cryptonite); the other core idles. mbedtls and the driver are never used at the same time.
* The mbedtls column measures the Arduino-ESP32 2.0.x / ESP-IDF 4.4.7 library as shipped (`sdk=v4.4.7-dirty` in `results/raw/info.csv`).
* No side-channel or constant-time analysis was done.

## Not measured

Energy and power. The board has no current sensor, so there are no energy numbers and no claims about energy. Also not measured: code size and RAM of the variants, other parameter sets (ML-KEM-512/1024,
ML-DSA-65/87, Falcon-1024), other chips (the other ESP32 variants have a different RSA peripheral), and key generation or encapsulation with an accelerated product.

## Credits

* PQClean (https://github.com/PQClean/PQClean) for the clean-C ML-KEM, ML-DSA and Falcon implementations and `fips202`; licence files are kept in `vendor/pqclean`. Those implementations derive from the Kyber, Dilithium and Falcon reference code.
* mbedtls (as shipped with ESP-IDF 4.4.7 in the Arduino-ESP32 core) for the baseline multiply path.
* Cryptonite, the earlier project on neural-network inference on the same unit: `firmware/src/mpi_hw.h` is the register driver from there, and the three optimisations are the ones studied there.
