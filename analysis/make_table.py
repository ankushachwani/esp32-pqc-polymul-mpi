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
    'mldsa_c_t0': 'ML-DSA-44, c x t0', 'mldsa_full': 'ML-DSA-44, A x y (full width)', 'falcon512_h_s2': 'Falcon-512 verify, h x s2'}


def rd(name): return pd.read_csv(os.path.join(RAW, name + '.csv'))


def md(df, path, floatfmt='%.2f'):
    cols = list(df.columns)
    with open(path, 'w') as f:
        f.write('| ' + ' | '.join(cols) + ' |\n|' + '---|' * len(cols) + '\n')
        for _, r in df.iterrows():
            f.write('| ' + ' | '.join((floatfmt % v) if isinstance(v, float) else str(v) for v in r.values) + ' |\n')


def main():
    os.makedirs(OUT, exist_ok=True)
    sweep = rd('sweep'); chk = rd('check'); chkcpu = rd('check_cpu')
    sweep.to_csv(os.path.join(OUT, 'polymul_sweep.csv'), index=False)

    # --- polynomial product, best plan per stage
    rows = []
    for case in CASE_NOTE:
        s = sweep[sweep.case == case]
        ntt_full = s[s.stage == 'ntt_full'].iloc[0]; ntt_pre = s[s.stage == 'ntt_pre'].iloc[0]
        row = {'case': case, 'what': CASE_NOTE[case], 'NTT full (us)': ntt_full.mean_us, 'NTT, a pre-transformed (us)': ntt_pre.mean_us}
        for st in STAGES:
            t = s[s.stage == st]; r = t.loc[t.mean_us.idxmin()]
            row['%s (us)' % STAGE_LABEL[st]] = r.mean_us; row['%s plan' % STAGE_LABEL[st]] = '%d chunks, %d mults' % (r.c, r.multiplies)
        last = STAGE_LABEL['overlap']
        row['best MPI / NTT full'] = row['%s (us)' % last] / ntt_full.mean_us
        row['best MPI / NTT pre'] = row['%s (us)' % last] / ntt_pre.mean_us
        row['S0 -> S3 speedup'] = row['%s (us)' % STAGE_LABEL['mbedtls_api']] / row['%s (us)' % last]
        c = chk[chk.case == case]; row['vectors checked (all plans, stages)'] = int(c.vectors.sum()); row['mismatches'] = int(c.bad.sum()) + int(chkcpu[chkcpu.case == case].bad.sum())
        rows.append(row)
    pm = pd.DataFrame(rows); pm.to_csv(os.path.join(OUT, 'polymul_latency.csv'), index=False)
    short = pm[['case', 'NTT full (us)', 'NTT, a pre-transformed (us)'] + ['%s (us)' % STAGE_LABEL[s] for s in STAGES] + ['best MPI / NTT full', 'best MPI / NTT pre', 'S0 -> S3 speedup', 'mismatches']]
    md(short, os.path.join(OUT, 'polymul_latency.md'))

    # --- where the time goes at the direct-driver stage
    pr = rd('prof'); comp = rd('comp')
    pr = pr.merge(pm[['case', 'NTT full (us)']], on='case')
    pr.to_csv(os.path.join(OUT, 'breakdown.csv'), index=False)
    md(pr[['case', 'c', 'pack_us', 'prefix_us', 'load_us', 'compute_us', 'read_us', 'accum_us', 'finish_us', 'total_us', 'NTT full (us)']], os.path.join(OUT, 'breakdown.md'))
    comp.to_csv(os.path.join(OUT, 'ntt_parts.csv'), index=False); md(comp[['case', 'ntt_us', 'pointwise_us', 'intt_us', 'full_us', 'pre_us']], os.path.join(OUT, 'ntt_parts.md'))

    # --- multiplier alone
    hw = rd('hwlat'); hw = hw[['bits', 'mbedtls_us', 'direct_us', 'load_us', 'compute_us', 'read_us', 'mbedtls_exact', 'direct_exact']]
    hw.to_csv(os.path.join(OUT, 'multiplier.csv'), index=False); md(hw, os.path.join(OUT, 'multiplier.md'))

    # --- whole schemes
    e = rd('e2e'); rows = []
    for (scheme, op), g in e.groupby(['scheme', 'op'], sort=False):
        ref = g[g.variant == 'ref'].iloc[0].mean_us
        for _, r in g.iterrows():
            lab = {'ref': 'PQClean as shipped', 'cpu': 'copy of it, NTT'}[r.variant] if r.variant in ('ref', 'cpu') else 'product on MPI, ' + STAGE_LABEL[STAGES[int(r.variant[1])]]
            rows.append({'scheme': scheme, 'op': op, 'variant': lab, 'mean_us': r.mean_us, 'min_us': r.min_us, 'std_us': r.std_us, 'vs PQClean': r.mean_us / ref, 'output identical': 'yes' if r.ok == 1 else 'NO',
                         'reps': r.reps, 'cmd': r.cmd})
    et = pd.DataFrame(rows); et.to_csv(os.path.join(OUT, 'e2e.csv'), index=False)
    md(et[['scheme', 'op', 'variant', 'mean_us', 'std_us', 'vs PQClean', 'output identical', 'reps']], os.path.join(OUT, 'e2e.md'))
    print('tables written to', OUT)


if __name__ == '__main__':
    sys.exit(main())
