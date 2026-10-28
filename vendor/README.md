# Vendored PQClean

Unmodified copies of the clean-C implementations from PQClean (https://github.com/PQClean/PQClean), taken from commit 0586a824
(the checkout at pqc_microcontroller): `crypto_kem/ml-kem-768/clean`, `crypto_sign/ml-dsa-44/clean`, `crypto_sign/falcon-512/clean`
and the parts of `common/` they need (fips202, compat, randombytes header). The licence files of each directory are kept.
Licences: ML-KEM and ML-DSA directories are CC0 / public domain, Falcon is MIT, `common/fips202` is public domain (see the file headers).
`common/` also holds aes/sha2/nistseedexpander/sp800-185 sources that nothing here compiles; they are left as PQClean ships them.
PQClean's own `randombytes.c` is not included: `firmware/src/rb.c` provides a seedable generator instead.
