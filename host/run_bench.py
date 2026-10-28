"""Full measurement run (on the Pi, firmware already flashed): writes raw CSVs straight from the board into --out (default results/raw).

  python3 host/run_bench.py            full run
  python3 host/run_bench.py --quick    few reps, for a smoke test

Steps: board info and multiplier latency; per case, bit-exactness of the NTT baseline and of every (plan, stage) pair, then latency of every
feasible plan at every stage; stage-2 time breakdown; NTT parts; whole-scheme operations with the best plan per stage (best = lowest measured mean).
"""
import argparse, csv, os, sys, time
from board import Board, kv

CASES = ['mlkem_x_cbd', 'mlkem_full', 'mldsa_c_s', 'mldsa_c_t0', 'mldsa_full', 'falcon512_s2_h']
STAGES = [0, 1, 2, 3]


class Out:
    def __init__(self, d): self.d = d; self.rows = {}; os.makedirs(d, exist_ok=True)
    def add(self, name, row): self.rows.setdefault(name, []).append(row)
    def flush(self):
        for name, rows in self.rows.items():
            keys = []
            for r in rows:
                for k in r:
                    if k not in keys: keys.append(k)
            with open(os.path.join(self.d, name + '.csv'), 'w', newline='') as f:
                w = csv.DictWriter(f, fieldnames=keys); w.writeheader(); w.writerows(rows)


def log(*a): print(time.strftime('%H:%M:%S'), *a, flush=True)


def reps_for(b, cmd_fmt, target_s):
    """Pick a repetition count so one measurement takes about target_s seconds, from a short pilot."""
    r = kv(b.cmd(cmd_fmt % 4, timeout=300)[0]); per = max(r['mean_us'], 1.0) / 1e6
    return int(min(400, max(8, target_s / per)))


def main():
    ap = argparse.ArgumentParser(); ap.add_argument('--quick', action='store_true'); ap.add_argument('--out', default=os.path.join(os.path.dirname(__file__), '..', 'results', 'raw'))
    args = ap.parse_args(); q = args.quick; out = Out(args.out); b = Board()
    target = 0.15 if q else 1.0

    for ln in b.cmd('INFO'): log(ln); out.add('info', kv(ln))
    out.add('acq', kv(b.cmd('ACQ 200')[0]))
    for ln in b.cmd('HWLAT', timeout=300): out.add('hwlat', kv(ln)); log(ln)

    best = {}                                     # (case, stage) -> (mean_us, c)
    for case in CASES:
        log('case', case)
        for ln in b.cmd('CHECKCPU %s %d' % (case, 10 if q else 60)): out.add('check_cpu', kv(ln))
        for ncmd in ('BENCH %s 0 4 %%d' % case, 'BENCH %s 0 5 %%d' % case, 'BENCH %s 0 6 %%d' % case, 'BENCH %s 0 7 %%d' % case):
            reps = reps_for(b, ncmd, target)
            out.add('sweep', kv(b.cmd(ncmd % reps, timeout=300)[0]))
        for ln in b.cmd('COMP %s %d' % (case, 20 if q else 200)): out.add('comp', kv(ln))
        for c in range(1, 17):
            try: plan = kv(b.cmd('PLAN %s %d' % (case, c))[0])
            except RuntimeError: continue
            out.add('plans', plan)
            for st in STAGES:
                out.add('check', kv(b.cmd('CHECK %s %d %d %d' % (case, c, st, 6 if q else 30), timeout=300)[0]))
                reps = reps_for(b, 'BENCH %s %d %d %%d' % (case, c, st), target)
                r = kv(b.cmd('BENCH %s %d %d %d' % (case, c, st, reps), timeout=300)[0]); r['reps_target_s'] = target
                out.add('sweep', r)
                k = (case, st)
                if k not in best or r['mean_us'] < best[k][0]: best[k] = (r['mean_us'], c)
            log('  c=%d done' % c)
        for st in (2,):
            c = best[(case, st)][1]
            out.add('prof', kv(b.cmd('PROF %s %d %d' % (case, c, 20 if q else 200))[0]))
        out.flush()

    # ---- optimised path: plan sweep with every switch on, then each switch alone and together at the best plan
    fbest = {}
    for case in CASES:
        log('fast sweep', case)
        for c in range(1, 17):
            try: b.cmd('PLAN %s %d' % (case, c))
            except RuntimeError: continue
            reps = reps_for(b, 'FAST single %s %d 31 %%d' % (case, c), target)
            r = kv(b.cmd('FAST single %s %d 31 %d' % (case, c, reps), timeout=300)[0]); out.add('fast_sweep', r)
            if case not in fbest or r['mean_us'] < fbest[case][0]: fbest[case] = (r['mean_us'], c)
        out.flush()
    NARROW = ['mlkem_x_cbd', 'mldsa_c_s', 'mldsa_c_t0']
    def fast(kind, case, c, opt):
        reps = reps_for(b, 'FAST %s %s %d %d %%d' % (kind, case, c, opt), target)
        r = kv(b.cmd('FAST %s %s %d %d %d' % (kind, case, c, opt, reps), timeout=300)[0]); r['reps_target_s'] = target; out.add('fast_ablation', r); return r
    for case in CASES:                                    # one product: switches alone and together (SHARE / FUSE do not apply to a single product)
        c = fbest[case][1]
        for opt in (0, 1, 2, 16, 32, 3, 17, 18, 19, 51): fast("single", case, c, opt)
        out.add('hwonly', kv(b.cmd('HWONLY %s %d 0 %d' % (case, c, 50 if q else 300))[0]))
        out.add('hwonly', kv(b.cmd('HWONLY %s %d 1 %d' % (case, c, 50 if q else 300))[0]))
        # old overlap stage at the same plan, for the comparison with the engine at opt 0
        reps = reps_for(b, 'BENCH %s %d 3 %%d' % (case, c), target)
        r = kv(b.cmd('BENCH %s %d 3 %d' % (case, c, reps), timeout=300)[0]); out.add('old_overlap', r)
    log('ablation: sum of 3 (ML-KEM decrypt shape)')
    for opt in (0, 1, 2, 4, 16, 5, 7, 19, 23, 31): fast('sum', 'mlkem_x_cbd', fbest['mlkem_x_cbd'][1], opt)
    log('ablation: four products with a common operand (ML-DSA c x s1 / s2 / t0)')
    for case in ('mldsa_c_s', 'mldsa_c_t0'):
        for opt in (0, 1, 2, 8, 16, 11, 9, 25, 27, 31): fast('shared', case, fbest[case][1], opt)
    out.flush()

    # whole-scheme operations; plan for each stage = the one that was fastest in the sweep
    def plan_for(case, st): return best[(case, st)][1]
    e_reps = dict(kem=12 if q else 80, dsa=8 if q else 32, falcon=16 if q else 128)
    def e2e(cmdline):
        for ln in b.cmd(cmdline, timeout=1800):
            log(ln)
            if ln.startswith('E '): r = kv(ln); r['cmd'] = cmdline; out.add('e2e', r)
    ck = fbest['mlkem_x_cbd'][1]; cs_, ct_ = fbest['mldsa_c_s'][1], fbest['mldsa_c_t0'][1]; cf = fbest['falcon512_s2_h'][1]
    for op in ('keygen', 'enc'): e2e('E2E kem %s ref %d' % (op, e_reps['kem']))
    for op in ('dec', 'indcpa'):
        r_ = e_reps['kem'] * (4 if op == 'indcpa' else 1)
        for var in ('ref', 'cpu', 't'): e2e('E2E kem %s %s %d' % (op, var, r_))
        for st in STAGES: e2e('E2E kem %s s%d %d %d' % (op, st, r_, plan_for('mlkem_x_cbd', st)))
        for opt in (0, 31): e2e('E2E kem %s f%d %d %d' % (op, opt, r_, ck))
    e2e('E2E dsa verify ref %d' % e_reps['dsa'])
    for var in ('ref', 'cpu', 't', 'tc'): e2e('E2E dsa sign %s %d' % (var, e_reps['dsa']))
    for st in STAGES: e2e('E2E dsa sign s%d %d %d %d' % (st, e_reps['dsa'], plan_for('mldsa_c_s', st), plan_for('mldsa_c_t0', st)))
    for opt in (0, 31): e2e('E2E dsa sign f%d %d %d %d' % (opt, e_reps['dsa'], cs_, ct_))
    e2e('E2E falcon sign ref %d' % 8)
    for var in ('ref', 'cpu', 't', 'tc'): e2e('E2E falcon verify %s %d' % (var, e_reps['falcon']))
    for st in STAGES: e2e('E2E falcon verify s%d %d %d' % (st, e_reps['falcon'], plan_for('falcon512_s2_h', st)))
    for opt in (0, 31): e2e('E2E falcon verify f%d %d %d' % (opt, e_reps['falcon'], cf))
    out.flush(); log('done')
    bad = [r for r in out.rows['check'] + out.rows['check_cpu'] + out.rows['fast_ablation'] + out.rows['fast_sweep'] if r.get('bad')] + [r for r in out.rows['e2e'] if r.get('ok') != 1]
    if bad: log('MISMATCHES:', bad); return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
