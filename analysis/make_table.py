"""Raw board CSVs (results/raw) -> tables (results/tables, .csv and .md). Nothing is computed here except ratios, minima, and the Amdahl arithmetic on measured times."""
import os, sys
import pandas as pd

HERE = os.path.dirname(__file__)
RESULTS = os.environ.get('PQC_RESULTS', os.path.join(HERE, '..', 'results'))      # override to run on a scratch copy of the raw data
RAW = os.path.join(RESULTS, 'raw'); OUT = os.path.join(RESULTS, 'tables')
STAGE_LABEL = {'mbedtls_api': 'mbedtls API', 'limb_io': '+ limb I/O', 'direct_driver': '+ direct driver', 'overlap': '+ overlap'}
STAGES = list(STAGE_LABEL)
CASE_NOTE = {
    'mlkem_x_cbd': 'ML-KEM-768, u (mod q) x s (CBD2)', 'mlkem_full': 'ML-KEM ring, two arbitrary elements', 'mldsa_c_s': 'ML-DSA-44, c x s1/s2',
    'mldsa_c_t0': 'ML-DSA-44, c x t0', 'mldsa_full': 'ML-DSA-44, y x A (full width)', 'falcon512_s2_h': 'Falcon-512 verify, s2 x h'}
SWITCH = {1: 'prepack', 2: 'X resident', 4: 'fuse sum', 8: 'share operand', 16: 'fast CPU code', 32: 'no Z clear'}
VARIANT = {'ref': 'PQClean as shipped', 'cpu': 'copy of it, PQClean NTT', 't': 'tuned NTT', 'tc': 'tuned NTT, key transform cached',
           's0': 'MPI: mbedtls API', 's1': 'MPI: + limb I/O', 's2': 'MPI: + direct driver', 's3': 'MPI: + overlap', 'f0': 'MPI engine, no switch', 'f31': 'MPI optimised (all switches)'}


def rd(name): return pd.read_csv(os.path.join(RAW, name + '.csv'))


def md(df, path):
    cols = list(df.columns)
    def cell(col, v):
        if isinstance(v, float):
            if v != v: return ''
            return '%.2f' % v if any(t in col for t in ('us', 'ms', '/', 'vs', 'speedup', 'x ', ' x', 'share', 'bound')) else '%d' % v
        return str(v)
    with open(path, 'w') as f:
        f.write('| ' + ' | '.join(cols) + ' |\n|' + '---|' * len(cols) + '\n')
        for _, r in df.iterrows():
            f.write('| ' + ' | '.join(cell(c, v) for c, v in zip(cols, r.values)) + ' |\n')


def switches(opt):
    names = [n for b, n in SWITCH.items() if opt & b]
    return ' + '.join(names) if names else 'none'


def main():
    os.makedirs(OUT, exist_ok=True)
    sweep = rd('sweep'); chk = rd('check'); chkcpu = rd('check_cpu'); fsweep = rd('fast_sweep'); fabl = rd('fast_ablation')
    sweep.to_csv(os.path.join(OUT, 'polymul_sweep.csv'), index=False); fsweep.to_csv(os.path.join(OUT, 'fast_sweep.csv'), index=False)

    # --- one polynomial product: baselines, the four stages, the optimised path
    rows = []
    for case in CASE_NOTE:
        s = sweep[sweep.case == case]
        def get(st): return s[s.stage == st].iloc[0].mean_us
        fs = fsweep[fsweep.case == case]; fb = fs.loc[fs.mean_us.idxmin()]
        row = {'case': case, 'what': CASE_NOTE[case], 'PQClean NTT full (us)': get('ntt_full'), 'PQClean NTT, fixed operand transformed (us)': get('ntt_pre'),
               'tuned NTT full (us)': get('tuned_full'), 'tuned NTT, fixed operand transformed (us)': get('tuned_pre')}
        for st in STAGES:
            t = s[s.stage == st]; r = t.loc[t.mean_us.idxmin()]; row['%s (us)' % STAGE_LABEL[st]] = r.mean_us
        row['optimised (us)'] = fb.mean_us; row['optimised plan'] = '%d chunks' % fb.c
        row['mbedtls API / optimised'] = row['mbedtls API (us)'] / fb.mean_us
        row['optimised / PQClean full'] = fb.mean_us / row['PQClean NTT full (us)']
        row['optimised / PQClean pre'] = fb.mean_us / row['PQClean NTT, fixed operand transformed (us)']
        row['optimised / tuned full'] = fb.mean_us / row['tuned NTT full (us)']
        row['optimised / tuned pre'] = fb.mean_us / row['tuned NTT, fixed operand transformed (us)']
        c = chk[chk.case == case]
        row['checks (stages)'] = int(c.vectors.sum()); row['checks (optimised sweep)'] = int(fs.checks.sum()); row['mismatches'] = int(c.bad.sum()) + int(fs.bad.sum()) + int(chkcpu[chkcpu.case == case].bad.sum())
        rows.append(row)
    pm = pd.DataFrame(rows); pm.to_csv(os.path.join(OUT, 'polymul_latency.csv'), index=False)
    md(pm[['case', 'PQClean NTT full (us)', 'PQClean NTT, fixed operand transformed (us)', 'tuned NTT full (us)', 'tuned NTT, fixed operand transformed (us)', 'mbedtls API (us)', '+ overlap (us)',
           'optimised (us)', 'mbedtls API / optimised', 'optimised / PQClean full', 'optimised / PQClean pre', 'optimised / tuned full', 'optimised / tuned pre', 'mismatches']].rename(
        columns={'PQClean NTT, fixed operand transformed (us)': 'PQClean NTT, key side transformed (us)', 'tuned NTT, fixed operand transformed (us)': 'tuned NTT, key side transformed (us)'}),
       os.path.join(OUT, 'polymul_latency.md'))
    md(pm[['case', 'mbedtls API (us)', '+ limb I/O (us)', '+ direct driver (us)', '+ overlap (us)', 'optimised (us)', 'optimised plan']], os.path.join(OUT, 'polymul_stages.md'))

    # --- ablation of the switches
    ab = fabl.copy(); ab['switches'] = ab.opt.apply(switches)
    old = rd('old_overlap').set_index('case')
    out_rows = []
    for (kind, case), g in ab.groupby(['kind', 'case'], sort=False):
        base = g[g.opt == 0].iloc[0].mean_us
        for _, r in g.iterrows():
            out_rows.append({'kind': {'single': 'one product', 'sum': 'sum of 3 products (ML-KEM decrypt shape)', 'shared': '4 products, common operand'}[kind], 'case': case, 'chunks': int(r.c),
                             'switches': r.switches, 'opt': int(r.opt), 'mean (us)': r.mean_us, 'x vs no switch': base / r.mean_us, 'checks': int(r.checks), 'mismatches': int(r.bad)})
        if kind == 'single' and case in old.index:
            out_rows.append({'kind': 'one product', 'case': case, 'chunks': int(old.loc[case].c), 'switches': 'for reference: old overlap stage (no engine)', 'opt': -1,
                             'mean (us)': old.loc[case].mean_us, 'x vs no switch': base / old.loc[case].mean_us, 'checks': '', 'mismatches': ''})
    at = pd.DataFrame(out_rows); at.to_csv(os.path.join(OUT, 'ablation.csv'), index=False); md(at.drop(columns=['opt']), os.path.join(OUT, 'ablation.md'))

    # --- the unit's own floor for the plan, against the optimised path
    hw = rd('hwonly'); rows = []
    for case in CASE_NOTE:
        h = hw[hw.case == case]; o = fsweep[fsweep.case == case]; o = o.loc[o.mean_us.idxmin()]
        h0 = h[h.xres == 0].iloc[0]; h1 = h[h.xres == 1].iloc[0]
        rows.append({'case': case, 'chunks': int(o.c), 'multiplies': int(h0.multiplies), 'unit only, X reloaded (us)': h0.mean_us, 'unit only, X resident (us)': h1.mean_us,
                     'optimised (us)': o.mean_us, 'optimised / unit only (X resident)': o.mean_us / h1.mean_us})
    hwt = pd.DataFrame(rows); hwt.to_csv(os.path.join(OUT, 'unit_floor.csv'), index=False); md(hwt, os.path.join(OUT, 'unit_floor.md'))

    # --- where the time goes at the direct-driver stage (before the optimised path)
    pr = rd('prof'); comp = rd('comp')
    pr = pr.merge(pm[['case', 'PQClean NTT full (us)']], on='case'); pr.to_csv(os.path.join(OUT, 'breakdown.csv'), index=False)
    md(pr[['case', 'c', 'pack_us', 'prefix_us', 'load_us', 'compute_us', 'read_us', 'accum_us', 'finish_us', 'total_us', 'PQClean NTT full (us)']], os.path.join(OUT, 'breakdown.md'))
    comp = comp.assign(impl=comp.tag.map({'C': 'PQClean', 'CT': 'tuned'}))
    comp.to_csv(os.path.join(OUT, 'ntt_parts.csv'), index=False); md(comp[['case', 'impl', 'ntt_us', 'pointwise_us', 'intt_us', 'full_us', 'pre_us']], os.path.join(OUT, 'ntt_parts.md'))

    # --- multiplier alone
    hwl = rd('hwlat'); hwl = hwl[['bits', 'mbedtls_us', 'direct_us', 'load_us', 'compute_us', 'read_us', 'mbedtls_exact', 'direct_exact']]
    hwl.to_csv(os.path.join(OUT, 'multiplier.csv'), index=False); md(hwl.astype(object).where(hwl != -1, 'n/a'), os.path.join(OUT, 'multiplier.md'))

    # --- whole schemes
    e = rd('e2e'); rows = []
    for (scheme, op), g in e.groupby(['scheme', 'op'], sort=False):
        ref = g[g.variant == 'ref'].iloc[0].mean_us
        base = g[g.variant == 'cpu'].iloc[0].mean_us if (g.variant == 'cpu').any() else ref
        att = None
        if 'attempts' in g.columns:
            a = pd.to_numeric(g.attempts, errors='coerce').dropna(); att = a.iloc[0] if len(a) else None
        for _, r in g.iterrows():
            rows.append({'scheme': scheme, 'op': op, 'variant': VARIANT[r.variant], 'mean_us': r.mean_us, 'min_us': r.min_us, 'std_us': r.std_us, 'vs PQClean': r.mean_us / ref,
                         'vs NTT copy': r.mean_us / base, 'us per rejection pass': (r.mean_us * r.reps / att) if att else '', 'product steps (us)': r.prod_us if r.prod_us >= 0 else float('nan'),
                         'prepacked key (bytes)': int(r.mem_bytes), 'output identical': 'yes' if r.ok == 1 else 'NO', 'reps': r.reps, 'cmd': r.cmd, 'variant_id': r.variant})
    et = pd.DataFrame(rows); et.to_csv(os.path.join(OUT, 'e2e.csv'), index=False)
    md(et[['scheme', 'op', 'variant', 'mean_us', 'std_us', 'vs PQClean', 'vs NTT copy', 'us per rejection pass', 'product steps (us)', 'prepacked key (bytes)', 'output identical', 'reps']], os.path.join(OUT, 'e2e.md'))

    comp_rows = []
    for (scheme, op), g in et.groupby(['scheme', 'op'], sort=False):
        if len(g) == 1: continue
        t = {r['variant_id']: r['mean_us'] / 1000.0 for _, r in g.iterrows()}
        nm = {'mlkem768 dec': 'ML-KEM-768 decapsulation', 'mlkem768 indcpa': 'ML-KEM-768 decrypt step', 'mldsa44 sign': 'ML-DSA-44 signing', 'falcon512 verify': 'Falcon-512 verification'}['%s %s' % (scheme, op)]
        opt = t['f31']
        comp_rows.append({'operation': nm, 'PQClean (ms)': t['ref'], 'PQClean NTT copy (ms)': t['cpu'], 'tuned NTT (ms)': t['t'], 'tuned NTT, key cached (ms)': t.get('tc', float('nan')),
                          'MPI mbedtls API (ms)': t['s0'], 'MPI overlap stage (ms)': t['s3'], 'MPI optimised (ms)': opt, 'mbedtls API / optimised': t['s0'] / opt,
                          'optimised / NTT copy': opt / t['cpu'], 'optimised / tuned NTT': opt / t['t'], 'optimised / tuned, key cached': opt / t['tc'] if 'tc' in t else float('nan'),
                          'outputs identical': 'yes' if (g['output identical'] == 'yes').all() else 'NO'})
    ec = pd.DataFrame(comp_rows); ec.to_csv(os.path.join(OUT, 'e2e_compact.csv'), index=False); md(ec, os.path.join(OUT, 'e2e_compact.md'))

    # --- Amdahl: measured share of the replaceable product steps in the baseline, measured speedup of those steps, the bounds that follow, and the measured whole-scheme ratio
    rows = []
    for (scheme, op), g in et.groupby(['scheme', 'op'], sort=False):
        if len(g) == 1: continue
        d = {r['variant_id']: r for _, r in g.iterrows()}
        for base_id in ('cpu', 't', 'tc'):
            if base_id not in d: continue
            b = d[base_id]; o = d['f31']
            share = b['product steps (us)'] / b['mean_us']; S = b['product steps (us)'] / o['product steps (us)']
            rows.append({'operation': '%s %s' % (scheme, op), 'baseline': VARIANT[base_id], 'baseline total (ms)': b['mean_us'] / 1000.0, 'share of time in the product steps': share,
                         'product steps: optimised MPI speedup (x)': S, 'bound if the products cost nothing (x)': 1 / (1 - share), 'bound at the measured product speedup (x)': 1 / ((1 - share) + share / S),
                         'measured whole-scheme speedup (x)': b['mean_us'] / o['mean_us']})
    amd = pd.DataFrame(rows); amd.to_csv(os.path.join(OUT, 'amdahl.csv'), index=False); md(amd, os.path.join(OUT, 'amdahl.md'))

    # --- the host prototype's plans must be the ones the firmware computed
    pp = pd.read_csv(os.path.join(OUT, 'packing_plan.csv')); m = rd('plans').merge(pp, left_on=['case', 'c'], right_on=['case', 'chunks'])
    assert len(m) == len(rd('plans')) and (m.slot_bits_x == m.slot_bits_y).all() and (m.hw_words * 32 == m.padded_operand_bits).all() and (m.multiplies == m.mpi_multiplies).all(), 'prototype and firmware plans differ'
    print('host prototype and firmware agree on all %d plans' % len(m))
    print('tables written to', OUT)


if __name__ == '__main__':
    sys.exit(main())
