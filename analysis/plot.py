"""Figures from results/tables: latency of one polynomial product per case, where the time goes, and whole-scheme operations."""
import os, sys
import numpy as np, pandas as pd
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt

HERE = os.path.dirname(__file__)
RESULTS = os.environ.get('PQC_RESULTS', os.path.join(HERE, '..', 'results'))
T = os.path.join(RESULTS, 'tables'); F = os.path.join(RESULTS, 'figures')
INK = '#222222'; GRID = '#dddddd'
C_NTT = '#555555'; C_NTT2 = '#aaaaaa'
C_STAGE = ['#c4a5d6', '#8d6bb8', '#5a3a96', '#e69f00']        # mbedtls, limb, driver, overlap
plt.rcParams.update({'font.size': 9, 'axes.edgecolor': INK, 'axes.labelcolor': INK, 'xtick.color': INK, 'ytick.color': INK, 'axes.spines.top': False, 'axes.spines.right': False,
                     'axes.grid': True, 'grid.color': GRID, 'grid.linewidth': 0.6, 'axes.axisbelow': True, 'figure.dpi': 150, 'savefig.bbox': 'tight'})


def fig_latency():
    pm = pd.read_csv(os.path.join(T, 'polymul_latency.csv'))
    labels = ['PQClean NTT full', 'PQClean NTT, key side transformed', 'tuned NTT full', 'tuned NTT, key side transformed', 'MPI: mbedtls API', 'MPI: overlap stage', 'MPI: optimised']
    cols = ['PQClean NTT full (us)', 'PQClean NTT, fixed operand transformed (us)', 'tuned NTT full (us)', 'tuned NTT, fixed operand transformed (us)', 'mbedtls API (us)', '+ overlap (us)', 'optimised (us)']
    colors = ['#555555', '#999999', '#1b6ca8', '#7fb3d9', C_STAGE[0], C_STAGE[2], C_STAGE[3]]
    fig, ax = plt.subplots(figsize=(9.0, 3.8)); x = np.arange(len(pm)); w = 0.115
    for i, (c, lab, col) in enumerate(zip(cols, labels, colors)):
        ax.bar(x + (i - 3) * w, pm[c], w, label=lab, color=col)
    ax.set_yscale('log'); ax.set_xticks(x); ax.set_xticklabels([c.replace('_', '\n', 1) for c in pm.case]); ax.set_ylabel('one polynomial product (us, log)')
    ax.legend(ncol=3, fontsize=7, frameon=False, loc='upper left'); ax.set_title('Negacyclic product on the ESP32: NTT (PQClean and tuned) vs Kronecker product on the MPI unit', fontsize=9, loc='left')
    fig.savefig(os.path.join(F, 'polymul_latency.png')); plt.close(fig)


def fig_ablation():
    a = pd.read_csv(os.path.join(T, 'ablation.csv')); a = a[(a.opt >= 0) & ~a.switches.str.contains('no Z clear')]
    ab = {'prepack': 'P', 'X resident': 'X', 'fuse sum': 'F', 'share operand': 'S', 'fast CPU code': 'C'}
    def short(sw):
        if sw == 'none': return 'none'
        for k, v in ab.items(): sw = sw.replace(k, v)
        return sw.replace(' + ', '+')
    groups = [('one product', 'mlkem_x_cbd'), ('one product', 'mldsa_c_s'), ('one product', 'mldsa_c_t0'), ('sum of 3 products (ML-KEM decrypt shape)', 'mlkem_x_cbd'), ('4 products, common operand', 'mldsa_c_s')]
    ttl = {'one product': 'one product', 'sum of 3 products (ML-KEM decrypt shape)': 'sum of 3', '4 products, common operand': '4 products, shared c'}
    fig, axes = plt.subplots(1, len(groups), figsize=(12.5, 3.4))
    for ax, (kind, case) in zip(axes, groups):
        g = a[(a.kind == kind) & (a.case == case)].sort_values('opt')
        ax.barh(np.arange(len(g)), g['mean (us)'], color=['#999999'] + [C_STAGE[3]] * (len(g) - 1))
        ax.set_yticks(np.arange(len(g))); ax.set_yticklabels([short(x) for x in g.switches], fontsize=7); ax.invert_yaxis()
        ax.set_title('%s\n%s' % (case, ttl[kind]), fontsize=8, loc='left'); ax.tick_params(labelsize=7); ax.set_xlabel('us', fontsize=7)
    fig.suptitle('Switches of the optimised path (P prepack, X X resident, F fuse sum, S share operand, C fast CPU code); grey = none', fontsize=9, x=0.01, ha='left')
    fig.tight_layout(rect=(0, 0, 1, 0.93)); fig.savefig(os.path.join(F, 'ablation.png')); plt.close(fig)


def fig_breakdown():
    b = pd.read_csv(os.path.join(T, 'breakdown.csv'))
    parts = [('pack_us', 'pack operands'), ('prefix_us', 'prefix sums'), ('load_us', 'load into unit'), ('compute_us', 'multiply (hardware)'), ('read_us', 'read product'), ('accum_us', 'unpack slots'), ('finish_us', 'unshift, fold, reduce')]
    cols = ['#c4a5d6', '#e6d3ef', '#8d6bb8', '#e69f00', '#5a3a96', '#56b4e9', '#009e73']
    fig, ax = plt.subplots(figsize=(8.2, 3.4)); y = np.arange(len(b))[::-1]; left = np.zeros(len(b)); ntt = b['PQClean NTT full (us)'].values
    for (c, lab), col in zip(parts, cols):
        v = b[c].values / ntt; ax.barh(y, v, 0.6, left=left, label=lab, color=col); left += v
    ax.axvline(1.0, color=INK, lw=1.2); ax.text(1.03, -0.55, 'NTT product = 1', fontsize=7.5, va='center', color=INK); ax.set_ylim(-0.8, len(b) - 0.4)
    ax.set_yticks(y); ax.set_yticklabels(['%s (c=%d)' % (c, k) for c, k in zip(b.case, b.c)]); ax.set_xlabel('time of one product / time of the NTT product (direct driver, no overlap)')
    ax.legend(ncol=4, fontsize=7, frameon=False, loc='upper center', bbox_to_anchor=(0.5, -0.2)); ax.set_title('Where the time goes at the direct-driver stage', fontsize=9, loc='left')
    fig.savefig(os.path.join(F, 'breakdown.png')); plt.close(fig)


def fig_e2e():
    e = pd.read_csv(os.path.join(T, 'e2e.csv')); keep = [('mlkem768', 'indcpa'), ('mlkem768', 'dec'), ('mldsa44', 'sign'), ('falcon512', 'verify')]
    ids = [('ref', 'PQClean', C_NTT), ('t', 'tuned NTT', '#1b6ca8'), ('tc', 'tuned NTT,\nkey cached', '#7fb3d9'), ('s0', 'MPI mbedtls\nAPI', C_STAGE[0]), ('s3', 'MPI overlap\nstage', C_STAGE[2]), ('opt', 'MPI\noptimised', C_STAGE[3])]
    fig, axes = plt.subplots(1, 4, figsize=(11.0, 3.2))
    for ax, (sch, op) in zip(axes, keep):
        g = e[(e.scheme == sch) & (e.op == op)].set_index('variant_id'); g = g.rename(index={'f19': 'opt'}) if 'f19' in g.index else g.rename(index={'f31': 'opt'}); sel = [(k, l, c) for k, l, c in ids if k in g.index]
        ax.bar(np.arange(len(sel)), [g.loc[k, 'mean_us'] / 1000.0 for k, _, _ in sel], color=[c for _, _, c in sel])
        ax.set_xticks(np.arange(len(sel))); ax.set_xticklabels([l for _, l, _ in sel], rotation=40, ha='right', fontsize=6.5); ax.set_title('%s %s' % (sch, 'decrypt step' if op == 'indcpa' else op), fontsize=8.5, loc='left'); ax.set_ylabel('ms')
    fig.suptitle('Whole-scheme operations with the product swapped (outputs identical to PQClean)', fontsize=9, x=0.01, ha='left')
    fig.tight_layout(); fig.savefig(os.path.join(F, 'whole_scheme.png')); plt.close(fig)


def fig_amdahl():
    a = pd.read_csv(os.path.join(T, 'amdahl.csv')); a = a[a.baseline == 'tuned NTT'] if (a.baseline == 'tuned NTT').any() else a
    fig, ax = plt.subplots(figsize=(7.5, 3.0)); y = np.arange(len(a))[::-1]
    ax.barh(y + 0.2, a['bound if the products cost nothing (x)'], 0.35, color='#cccccc', label='bound if the products cost nothing')
    ax.barh(y - 0.2, a['measured whole-scheme speedup (x)'], 0.35, color=C_STAGE[3], label='measured')
    ax.axvline(1.0, color=INK, lw=1); ax.set_yticks(y); ax.set_yticklabels(a.operation); ax.set_xlabel('speedup over the tuned NTT code (x)'); ax.legend(fontsize=7, frameon=False)
    ax.set_title('Amdahl bound and measured whole-scheme speedup (baseline: tuned NTT)', fontsize=9, loc='left'); fig.savefig(os.path.join(F, 'amdahl.png')); plt.close(fig)


if __name__ == '__main__':
    os.makedirs(F, exist_ok=True); fig_latency(); fig_ablation(); fig_breakdown(); fig_e2e(); fig_amdahl(); print('figures written to', F)
