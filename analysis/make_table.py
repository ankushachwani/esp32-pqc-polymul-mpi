"""Raw board CSVs (results/raw) -> tables (results/tables, .csv and .md). No number is computed that is not a ratio or a minimum of measured values."""
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


def rd(name): return pd.read_csv(os.path.join(RAW, name + '.csv'))


def md(df, path):
    cols = list(df.columns)
    def cell(col, v):
        if isinstance(v, float):
            return '%.2f' % v if any(t in col for t in ('us', 'ms', '/', 'vs', 'speedup')) else '%d' % v
        return str(v)
    with open(path, 'w') as f:
        f.write('| ' + ' | '.join(cols) + ' |\n|' + '---|' * len(cols) + '\n')
        for _, r in df.iterrows():
            f.write('| ' + ' | '.join(cell(c, v) for c, v in zip(cols, r.values)) + ' |\n')


def main():
    os.makedirs(OUT, exist_ok=True)
    sweep = rd('sweep'); chk = rd('check'); chkcpu = rd('check_cpu')
    sweep.to_csv(os.path.join(OUT, 'polymul_sweep.csv'), index=False)

    # --- polynomial product, best plan per stage
    rows = []
    for case in CASE_NOTE:
        s = sweep[sweep.case == case]
        ntt_full = s[s.stage == 'ntt_full'].iloc[0]; ntt_pre = s[s.stage == 'ntt_pre'].iloc[0]
        row = {'case': case, 'what': CASE_NOTE[case], 'NTT full (us)': ntt_full.mean_us, 'NTT, fixed operand pre-transformed (us)': ntt_pre.mean_us}
        for st in STAGES:
            t = s[s.stage == st]; r = t.loc[t.mean_us.idxmin()]
            row['%s (us)' % STAGE_LABEL[st]] = r.mean_us; row['%s plan' % STAGE_LABEL[st]] = '%d chunks, %d mults' % (r.c, r.multiplies)
        last = STAGE_LABEL['overlap']
        row['overlap / NTT full'] = row['%s (us)' % last] / ntt_full.mean_us
        row['overlap / NTT pre'] = row['%s (us)' % last] / ntt_pre.mean_us
        row['S0 -> S3 speedup'] = row['%s (us)' % STAGE_LABEL['mbedtls_api']] / row['%s (us)' % last]
        c = chk[chk.case == case]; row['vectors checked (all plans, stages)'] = int(c.vectors.sum()); row['mismatches'] = int(c.bad.sum()) + int(chkcpu[chkcpu.case == case].bad.sum())
        rows.append(row)
    pm = pd.DataFrame(rows); pm.to_csv(os.path.join(OUT, 'polymul_latency.csv'), index=False)
    short = pm[['case', 'NTT full (us)', 'NTT, fixed operand pre-transformed (us)'] + ['%s (us)' % STAGE_LABEL[s] for s in STAGES] + ['overlap / NTT full', 'overlap / NTT pre', 'S0 -> S3 speedup', 'mismatches']]
    md(short, os.path.join(OUT, 'polymul_latency.md'))

    # --- where the time goes at the direct-driver stage
    pr = rd('prof'); comp = rd('comp')
    pr = pr.merge(pm[['case', 'NTT full (us)']], on='case')
    pr.to_csv(os.path.join(OUT, 'breakdown.csv'), index=False)
    md(pr[['case', 'c', 'pack_us', 'prefix_us', 'load_us', 'compute_us', 'read_us', 'accum_us', 'finish_us', 'total_us', 'NTT full (us)']], os.path.join(OUT, 'breakdown.md'))
    comp.to_csv(os.path.join(OUT, 'ntt_parts.csv'), index=False); md(comp[['case', 'ntt_us', 'pointwise_us', 'intt_us', 'full_us', 'pre_us']], os.path.join(OUT, 'ntt_parts.md'))

    # --- multiplier alone
    hw = rd('hwlat'); hw = hw[['bits', 'mbedtls_us', 'direct_us', 'load_us', 'compute_us', 'read_us', 'mbedtls_exact', 'direct_exact']]
    hw.to_csv(os.path.join(OUT, 'multiplier.csv'), index=False); md(hw.astype(object).where(hw != -1, 'n/a'), os.path.join(OUT, 'multiplier.md'))

    # --- whole schemes
    e = rd('e2e'); rows = []
    for (scheme, op), g in e.groupby(['scheme', 'op'], sort=False):
        ref = g[g.variant == 'ref'].iloc[0].mean_us
        base = g[g.variant == 'cpu'].iloc[0].mean_us if (g.variant == 'cpu').any() else ref
        att = None
        if 'attempts' in g.columns:
            a = pd.to_numeric(g.attempts, errors='coerce').dropna()
            att = a.iloc[0] if len(a) else None
        for _, r in g.iterrows():
            lab = {'ref': 'PQClean as shipped', 'cpu': 'copy of it, NTT'}[r.variant] if r.variant in ('ref', 'cpu') else 'product on MPI, ' + STAGE_LABEL[STAGES[int(r.variant[1])]]
            rows.append({'scheme': scheme, 'op': op, 'variant': lab, 'mean_us': r.mean_us, 'min_us': r.min_us, 'std_us': r.std_us, 'vs PQClean': r.mean_us / ref, 'vs NTT copy': r.mean_us / base,
                         'us per rejection pass': (r.mean_us * r.reps / att) if att else '', 'output identical': 'yes' if r.ok == 1 else 'NO',
                         'reps': r.reps, 'cmd': r.cmd})
    et = pd.DataFrame(rows); et.to_csv(os.path.join(OUT, 'e2e.csv'), index=False)
    md(et[['scheme', 'op', 'variant', 'mean_us', 'std_us', 'vs PQClean', 'vs NTT copy', 'us per rejection pass', 'output identical', 'reps']], os.path.join(OUT, 'e2e.md'))
    # the host prototype's plans (slot width, operand size, multiplies) must be the ones the firmware computed
    pp = pd.read_csv(os.path.join(OUT, 'packing_plan.csv')); m = rd('plans').merge(pp, left_on=['case', 'c'], right_on=['case', 'chunks'])
    assert len(m) == len(rd('plans')) and (m.slot_bits_x == m.slot_bits_y).all() and (m.hw_words * 32 == m.padded_operand_bits).all() and (m.multiplies == m.mpi_multiplies).all(), 'prototype and firmware plans differ'
    print('host prototype and firmware agree on all %d plans' % len(m))
    # compact view for the README: one row per operation, times in ms
    rows = []
    for (scheme, op), g in et.groupby(['scheme', 'op'], sort=False):
        if len(g) == 1: continue
        t = {r.variant: r.mean_us / 1000.0 for r in g.itertuples()}
        mpi = [v for k, v in t.items() if k.startswith('product on MPI')]
        base = t.get('copy of it, NTT', t['PQClean as shipped'])
        rows.append({'operation': '%s %s' % (scheme, {'dec': 'decapsulation', 'indcpa': 'decrypt step of decapsulation'}.get(op, op)), 'PQClean (ms)': t['PQClean as shipped'], 'NTT copy (ms)': t.get('copy of it, NTT', float('nan')),
                     'mbedtls API (ms)': mpi[0], '+ limb I/O (ms)': mpi[1], '+ direct driver (ms)': mpi[2], '+ overlap (ms)': mpi[3], 'overlap / NTT copy': mpi[3] / base,
                     'outputs identical': 'yes' if (g['output identical'] == 'yes').all() else 'NO'})
    ec = pd.DataFrame(rows); ec.to_csv(os.path.join(OUT, 'e2e_compact.csv'), index=False)
    md(ec, os.path.join(OUT, 'e2e_compact.md'))
    print('tables written to', OUT)


if __name__ == '__main__':
    sys.exit(main())
