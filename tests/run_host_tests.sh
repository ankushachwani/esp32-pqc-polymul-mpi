#!/usr/bin/env bash
# Builds the portable part of the firmware for the host and runs tests/host_test.c.
set -euo pipefail
cd "$(dirname "$0")/.."
V=vendor/pqclean; OUT=${OUT:-/tmp/pqc_host_test}; mkdir -p "$OUT"
SRC="firmware/src/kron.c firmware/src/cases.c firmware/src/ntt_tuned.c firmware/src/cpu_tuned.c firmware/src/rb.c firmware/src/schemes_kem.c firmware/src/schemes_dsa.c firmware/src/schemes_falcon.c tests/host_test.c $V/common/fips202.c"
for d in ml-kem-768 ml-dsa-44; do SRC="$SRC $(ls $V/crypto_*/$d/clean/*.c)"; done
SRC="$SRC $(ls $V/crypto_sign/falcon-512/clean/*.c | grep -v vrfy.c)"
INC="-Ifirmware/src -I$V/common -I$V/crypto_kem -I$V/crypto_sign"
c++ -O2 -std=c++11 -Wall $INC -c firmware/src/kron_fast.cpp -o "$OUT/kron_fast.o"
c++ -O2 -std=c++11 -Wall -Wno-unused-function -DPQC_HOST -Itests $INC -c firmware/src/polymul_fast.cpp -o "$OUT/polymul_fast.o"
c++ -O2 -std=c++11 -Wall -DPQC_HOST -Itests $INC -c firmware/src/fast_hooks.cpp -o "$OUT/fast_hooks.o"
cc -O2 -std=gnu99 -Wall -Wno-unused-function $INC $SRC "$OUT/kron_fast.o" "$OUT/polymul_fast.o" "$OUT/fast_hooks.o" -lstdc++ -o "$OUT/host_test"
"$OUT/host_test"

# the optimised engine (polymul_fast.cpp) with a software multiplier standing in for the unit: every switch combination against the reference
cc -O2 -std=gnu99 -Ifirmware/src -c firmware/src/kron.c -o "$OUT/kron.o"
cc -O2 -std=gnu99 -Ifirmware/src -c firmware/src/cases.c -o "$OUT/cases.o"
c++ -O2 -std=c++11 -Wall -Wno-unused-function -DPQC_HOST -Itests -Ifirmware/src tests/host_engine_test.cpp "$OUT/polymul_fast.o" "$OUT/kron_fast.o" "$OUT/kron.o" "$OUT/cases.o" -o "$OUT/engine_test"
"$OUT/engine_test"
