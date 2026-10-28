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
        out.add('check_cpu', kv(b.cmd('CHECKCPU %s %d' % (case, 10 if q else 60))[0]))
        for ncmd in ('BENCH %s 0 4 %%d' % case, 'BENCH %s 0 5 %%d' % case):
            reps = reps_for(b, ncmd, target)
            out.add('sweep', kv(b.cmd(ncmd % reps, timeout=300)[0]))
        out.add('comp', kv(b.cmd('COMP %s %d' % (case, 20 if q else 200))[0]))
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

    # whole-scheme operations; plan for each stage = the one that was fastest in the sweep
    def plan_for(case, st): return best[(case, st)][1]
    e_reps = dict(kem=12 if q else 80, dsa=8 if q else 32, falcon=16 if q else 128)
    def e2e(cmdline):
        for ln in b.cmd(cmdline, timeout=1800):
            log(ln)
            if ln.startswith('E '): r = kv(ln); r['cmd'] = cmdline; out.add('e2e', r)
    for op in ('keygen', 'enc'): e2e('E2E kem %s ref %d' % (op, e_reps['kem']))
    for op in ('dec', 'indcpa'):
        for var in ('ref', 'cpu'): e2e('E2E kem %s %s %d' % (op, var, e_reps['kem'] * (4 if op == 'indcpa' else 1)))
        for st in STAGES: e2e('E2E kem %s s%d %d %d' % (op, st, e_reps['kem'] * (4 if op == 'indcpa' else 1), plan_for('mlkem_x_cbd', st)))
    e2e('E2E dsa verify ref %d' % e_reps['dsa'])
    for var in ('ref', 'cpu'): e2e('E2E dsa sign %s %d' % (var, e_reps['dsa']))
    for st in STAGES: e2e('E2E dsa sign s%d %d %d %d' % (st, e_reps['dsa'], plan_for('mldsa_c_s', st), plan_for('mldsa_c_t0', st)))
    e2e('E2E falcon sign ref %d' % 8)
    for var in ('ref', 'cpu'): e2e('E2E falcon verify %s %d' % (var, e_reps['falcon']))
    for st in STAGES: e2e('E2E falcon verify s%d %d %d' % (st, e_reps['falcon'], plan_for('falcon512_s2_h', st)))
    out.flush(); log('done')
    bad = [r for r in out.rows['check'] + out.rows['check_cpu'] if r.get('bad')] + [r for r in out.rows['e2e'] if r.get('ok') != 1]
    if bad: log('MISMATCHES:', bad); return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
