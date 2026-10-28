"""Host prototype of the Kronecker-packed negacyclic polynomial product, before any of it goes on the board.

Question answered here: can a(x)*b(x) mod (x^n + 1, q) be computed with big-integer multiplies of at most 2048 bits
(the ESP32 MPI limit, see firmware/src/mpi_hw.h), and how many such multiplies does it take?

Method (the same arithmetic the firmware does, kept in plain python ints so it is easy to audit):
  1. shift both operands to be non-negative: a' = a + oa, b' = b + ob (oa, ob >= 0 only if the input range has negatives)
  2. split each operand into c chunks of m coefficients and pack every chunk into one integer, one coefficient per w-bit slot
  3. one integer multiply per chunk pair; slot k of the product holds sum_{i+j=k} a'_i b'_j for that pair
  4. add the chunk-pair products into a linear product P' of 2n-1 slots
  5. undo the shift:  P_k = P'_k - ob*SA(k) - oa*SB(k) - oa*ob*cnt(k)   (SA, SB = window sums of a and b, cnt = number of terms)
  6. fold the wrap-around:  c_k = P_k - P_{k+n}  mod q
w is the smallest width for which no slot can overflow: bitlen(m * max(a') * max(b')).

Run:  python3 host/kron_proto.py          (writes results/tables/packing_plan.csv and .md, exits non-zero on any mismatch)
"""
import csv, os, sys
import numpy as np

MPI_MAX_OPERAND_BITS = 2048     # mpi_hw.h: MAX_WORDS = 64 words per operand, 4096-bit product
MPI_BLOCK_BITS = 512            # mpi_hw.h: hw_words() pads operands to a multiple of 16 words

# n, q, (amin, amax), (bmin, bmax), where it shows up
CASES = {
    'mlkem_x_cbd':    dict(n=256, q=3329,    a=(0, 3328),      b=(-2, 2),          note='ML-KEM-768: uniform mod q times a CBD(eta=2) vector (s*u in Decrypt, r*A in Encrypt)'),
    'mlkem_full':     dict(n=256, q=3329,    a=(0, 3328),      b=(0, 3328),        note='ML-KEM ring, two arbitrary elements (not a step the scheme does in coefficient form)'),
    'mldsa_c_s':      dict(n=256, q=8380417, a=(-1, 1),        b=(-2, 2),          note='ML-DSA-44: challenge c (+-1) times s1 or s2 (eta=2)'),
    'mldsa_c_t0':     dict(n=256, q=8380417, a=(-1, 1),        b=(-4095, 4096),    note='ML-DSA-44: challenge c times t0 (13-bit signed)'),
    'mldsa_full':     dict(n=256, q=8380417, a=(-131071, 131072), b=(0, 8380416),  note='ML-DSA-44: y or z (gamma1 = 2^17) times A (uniform mod q, fixed)'),
    'falcon512_s2_h': dict(n=512, q=12289,   a=(-2047, 2047),   b=(0, 12288),       note='Falcon-512 verify: signature s2 times public key h (fixed)'),
}


def ref_negacyclic(a, b, n, q):
    """Schoolbook reference: full linear product with numpy (exact in int64 for every case here), then fold x^n = -1."""
    p = np.convolve(np.asarray(a, dtype=np.int64), np.asarray(b, dtype=np.int64))
    p = np.concatenate([p, np.zeros(2 * n - len(p), dtype=np.int64)])
    return (p[:n] - p[n:2 * n]) % q


def plan(case, c):
    n = case['n']; m = -(-n // c)
    oa = max(0, -case['a'][0]); ob = max(0, -case['b'][0])
    da = case['a'][1] + oa; db = case['b'][1] + ob
    w = (m * da * db).bit_length()
    op_bits = m * w
    padded = -(-op_bits // MPI_BLOCK_BITS) * MPI_BLOCK_BITS
    return dict(c=c, m=m, w=w, oa=oa, ob=ob, da=da, db=db, operand_bits=op_bits, padded_bits=padded,
                feasible=padded <= MPI_MAX_OPERAND_BITS, mults=c * c)


def feasible_plans(case):
    seen, out = set(), []
    for c in range(1, case['n'] + 1):
        p = plan(case, c)
        if p['m'] in seen: continue            # same chunk size as a smaller c (only the last chunk differs): skip
        seen.add(p['m'])
        if p['feasible']: out.append(p)
    return out


def pack(vals, w):
    x = 0
    for i, v in enumerate(vals):
        assert v >= 0
        x |= int(v) << (w * i)
    return x


def unpack(x, w, count):
    mask = (1 << w) - 1
    return [(x >> (w * k)) & mask for k in range(count)]


def kron_mul(a, b, case, p, hw_mul=lambda x, y: x * y, w_override=None):
    n, q = case['n'], case['q']; c, m, oa, ob = p['c'], p['m'], p['oa'], p['ob']
    w = w_override or p['w']
    a2 = [int(v) + oa for v in a]; b2 = [int(v) + ob for v in b]
    A = [pack(a2[i * m:(i + 1) * m], w) for i in range(c)]
    B = [pack(b2[i * m:(i + 1) * m], w) for i in range(c)]
    acc = [0] * (2 * c * m + 2)
    for i in range(c):
        for j in range(c):
            assert A[i].bit_length() <= MPI_MAX_OPERAND_BITS and B[j].bit_length() <= MPI_MAX_OPERAND_BITS
            z = hw_mul(A[i], B[j])
            for k, s in enumerate(unpack(z, w, 2 * m)):
                acc[(i + j) * m + k] += s
    # undo the shift. window sums over the original (unshifted) coefficients
    pa = np.concatenate([[0], np.cumsum(np.asarray(a, dtype=np.int64))]); pb = np.concatenate([[0], np.cumsum(np.asarray(b, dtype=np.int64))])
    P = [0] * (2 * n)
    for k in range(2 * n - 1):
        lo = max(0, k - n + 1); hi = min(k, n - 1)           # i ranges over lo..hi, j = k - i
        cnt = hi - lo + 1
        sa = int(pa[hi + 1] - pa[lo])
        sb = int(pb[k - lo + 1] - pb[k - hi])
        P[k] = acc[k] - ob * sa - oa * sb - oa * ob * cnt
    return np.array([(P[k] - P[k + n]) % q for k in range(n)], dtype=np.int64)


def rand_in(rng, rngspec, n):
    return rng.integers(rngspec[0], rngspec[1] + 1, size=n)


def edge_vectors(rngspec, n):
    lo, hi = rngspec
    return [np.full(n, lo), np.full(n, hi), np.array([lo, hi] * (n // 2)), np.array([hi, lo] * (n // 2)), np.zeros(n, dtype=np.int64)]


def check_case(name, case, rng, trials=20):
    n = case['n']; plans = feasible_plans(case); bad = 0; tot = 0
    for p in plans:
        tests = [(rand_in(rng, case['a'], n), rand_in(rng, case['b'], n)) for _ in range(trials)]
        for ea in edge_vectors(case['a'], n):
            for eb in edge_vectors(case['b'], n): tests.append((ea, eb))
        for a, b in tests:
            tot += 1
            if not np.array_equal(kron_mul(a, b, case, p), ref_negacyclic(a, b, n, case['q'])): bad += 1
    return plans, tot, bad


def width_is_tight(case, p):
    """With one bit less per slot the worst-case input must break (otherwise w was not needed)."""
    n = case['n']; lo_a, hi_a = case['a']; lo_b, hi_b = case['b']
    # the worst case for the shifted operands is a = max, b = max (largest shifted values)
    a = np.full(n, hi_a); b = np.full(n, hi_b)
    got = kron_mul(a, b, case, p, w_override=p['w'] - 1) if p['w'] > 1 else None
    return got is not None and not np.array_equal(got, ref_negacyclic(a, b, n, case['q']))


def main():
    rng = np.random.default_rng(1)
    rows = []; fail = 0
    for name, case in CASES.items():
        plans, tot, bad = check_case(name, case, rng)
        fail += bad
        best = min(plans, key=lambda p: (p['mults'], p['padded_bits']))
        print('%-16s n=%3d q=%8d feasible plans=%2d  best c=%d m=%d w=%2d bits -> %d multiplies of %d bits   tests %d, mismatches %d, w-1 breaks: %s'
              % (name, case['n'], case['q'], len(plans), best['c'], best['m'], best['w'], best['mults'], best['padded_bits'], tot, bad, width_is_tight(case, best)))
        for p in plans:
            rows.append(dict(case=name, n=case['n'], q=case['q'], a_range='[%d,%d]' % case['a'], b_range='[%d,%d]' % case['b'], chunks=p['c'], coeffs_per_chunk=p['m'],
                             slot_bits=p['w'], operand_bits=p['operand_bits'], padded_operand_bits=p['padded_bits'], mpi_multiplies=p['mults'],
                             best='yes' if p is best else ''))
    out = os.path.join(os.path.dirname(__file__), '..', 'results', 'tables')
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, 'packing_plan.csv'), 'w', newline='') as f:
        wr = csv.DictWriter(f, fieldnames=list(rows[0].keys())); wr.writeheader(); wr.writerows(rows)
    with open(os.path.join(out, 'packing_plan.md'), 'w') as f:
        f.write('| case | n | q | a range | b range | chunks per operand | coeffs per chunk | slot bits | padded operand bits | MPI multiplies |\n|---|---|---|---|---|---|---|---|---|---|\n')
        for r in rows:
            if r['best']:
                f.write('| %s | %d | %d | %s | %s | %d | %d | %d | %d | %d |\n' % (r['case'], r['n'], r['q'], r['a_range'], r['b_range'], r['chunks'], r['coeffs_per_chunk'],
                                                                               r['slot_bits'], r['padded_operand_bits'], r['mpi_multiplies']))
    print('FAIL' if fail else 'all packings bit-exact against the schoolbook negacyclic product')
    return 1 if fail else 0


if __name__ == '__main__':
    sys.exit(main())
