"""Plan variants that were considered for cutting the number of multiplies, computed with the same slot-width rule as kron_proto.py (nothing here is timed):
  (a) centred operands with signed slots (one extra sign bit, |value| bound instead of range)
  (b) one Karatsuba level over the chunk grid: three half-size grids, the (A0+A1)(B0+B1) grid has operands up to twice as large, so its slots need 2 more bits
Run:  python3 host/plan_variants.py     (writes results/tables/plan_variants.csv and .md)
"""
import csv, os, sys
sys.path.insert(0, os.path.dirname(__file__))
from kron_proto import CASES, feasible_plans, MPI_BLOCK_BITS, MPI_MAX_OPERAND_BITS


def padded(bits): return -(-bits // MPI_BLOCK_BITS) * MPI_BLOCK_BITS


def signed_plan(case):
    n, q = case['n'], case['q']
    A = max(abs(case['a'][0]), abs(case['a'][1])); B = max(abs(case['b'][0]), abs(case['b'][1]))
    if case['a'][0] == 0 and case['a'][1] >= q - 1: A = q // 2          # uniform mod q operands can be centred
    if case['b'][0] == 0 and case['b'][1] >= q - 1: B = q // 2
    best = None
    for c in range(1, n + 1):
        m = -(-n // c); w = (m * A * B).bit_length() + 1
        if padded(m * w) <= MPI_MAX_OPERAND_BITS and (best is None or c * c < best[0]): best = (c * c, c, m, w)
    return best


def karatsuba_plan(case):
    n = case['n']; oa = max(0, -case['a'][0]); ob = max(0, -case['b'][0])
    da = case['a'][1] + oa; db = case['b'][1] + ob
    best = None
    for c in range(2, 33, 2):
        m = -(-n // c); w = (4 * m * da * db).bit_length()                  # widest grid: (A0 + A1) x (B0 + B1)
        if padded(m * w) <= MPI_MAX_OPERAND_BITS:
            mult = 3 * (c // 2) ** 2
            if best is None or mult < best[0]: best = (mult, c, m, w)
    return best


def main():
    rows = []
    for name, case in CASES.items():
        cur = min(feasible_plans(case), key=lambda p: (p['mults'], p['padded_bits']))
        s = signed_plan(case); k = karatsuba_plan(case)
        rows.append({'case': name, 'now: slot bits': cur['w'], 'now: multiplies': cur['mults'], 'signed slots: slot bits': s[3], 'signed slots: multiplies': s[0],
                     'Karatsuba: slot bits': k[3] if k else '', 'Karatsuba: multiplies': k[0] if k else 'none fits'})
    out = os.path.join(os.path.dirname(__file__), '..', 'results', 'tables')
    with open(os.path.join(out, 'plan_variants.csv'), 'w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=list(rows[0].keys())); w.writeheader(); w.writerows(rows)
    cols = list(rows[0].keys())
    with open(os.path.join(out, 'plan_variants.md'), 'w') as f:
        f.write('| ' + ' | '.join(cols) + ' |\n|' + '---|' * len(cols) + '\n')
        for r in rows: f.write('| ' + ' | '.join(str(r[c]) for c in cols) + ' |\n')
    for r in rows: print(r)


if __name__ == '__main__':
    main()
