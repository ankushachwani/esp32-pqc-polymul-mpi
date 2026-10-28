#!/usr/bin/env bash
# Builds the portable part of the firmware for the host and runs tests/host_test.c.
set -euo pipefail
cd "$(dirname "$0")/.."
V=vendor/pqclean; OUT=${OUT:-/tmp/pqc_host_test}; mkdir -p "$OUT"
SRC="firmware/src/kron.c firmware/src/cases.c firmware/src/ntt_tuned.c firmware/src/cpu_tuned.c firmware/src/rb.c firmware/src/schemes_kem.c firmware/src/schemes_dsa.c firmware/src/schemes_falcon.c tests/host_test.c $V/common/fips202.c"
for d in ml-kem-768 ml-dsa-44; do SRC="$SRC $(ls $V/crypto_*/$d/clean/*.c)"; done
SRC="$SRC $(ls $V/crypto_sign/falcon-512/clean/*.c | grep -v vrfy.c)"
cc -O2 -std=gnu99 -Wall -Wno-unused-function -Ifirmware/src -I$V/common -I$V/crypto_kem -I$V/crypto_sign $SRC -o "$OUT/host_test"
"$OUT/host_test"
