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
    labels = ['NTT full', 'NTT, a pre-transformed', 'mbedtls API', '+ limb I/O', '+ direct driver', '+ overlap']
    cols = ['NTT full (us)', 'NTT, a pre-transformed (us)', 'mbedtls API (us)', '+ limb I/O (us)', '+ direct driver (us)', '+ overlap (us)']
    colors = [C_NTT, C_NTT2] + C_STAGE
    fig, ax = plt.subplots(figsize=(8.2, 3.6)); x = np.arange(len(pm)); w = 0.13
    for i, (c, lab, col) in enumerate(zip(cols, labels, colors)):
        ax.bar(x + (i - 2.5) * w, pm[c], w, label=lab, color=col)
    ax.set_yscale('log'); ax.set_xticks(x); ax.set_xticklabels([c.replace('_', '\n', 1) for c in pm.case]); ax.set_ylabel('one polynomial product (us, log)')
    ax.legend(ncol=3, fontsize=7.5, frameon=False, loc='upper left'); ax.set_title('Negacyclic product on the ESP32: PQClean NTT vs Kronecker product on the MPI unit', fontsize=9, loc='left')
    fig.savefig(os.path.join(F, 'polymul_latency.png')); plt.close(fig)


def fig_breakdown():
    b = pd.read_csv(os.path.join(T, 'breakdown.csv'))
    parts = [('pack_us', 'pack operands'), ('prefix_us', 'prefix sums'), ('load_us', 'load into unit'), ('compute_us', 'multiply (hardware)'), ('read_us', 'read product'), ('accum_us', 'unpack slots'), ('finish_us', 'unshift, fold, reduce')]
    cols = ['#c4a5d6', '#e6d3ef', '#8d6bb8', '#e69f00', '#5a3a96', '#56b4e9', '#009e73']
    fig, ax = plt.subplots(figsize=(8.2, 3.4)); x = np.arange(len(b)); bottom = np.zeros(len(b))
    for (c, lab), col in zip(parts, cols):
        ax.bar(x, b[c], 0.55, bottom=bottom, label=lab, color=col); bottom += b[c].values
    ax.scatter(x, b['NTT full (us)'], marker='_', s=900, color=INK, zorder=5, label='NTT full')
    ax.set_xticks(x); ax.set_xticklabels([c.replace('_', '\n', 1) for c in b.case]); ax.set_ylabel('us per product (direct driver, no overlap)'); ax.set_yscale('log')
    ax.set_ylim(top=ax.get_ylim()[1] * 1.0); ax.legend(ncol=4, fontsize=7, frameon=False, loc='upper left'); ax.set_title('Where the time goes at the direct-driver stage (log scale)', fontsize=9, loc='left')
    fig.savefig(os.path.join(F, 'breakdown.png')); plt.close(fig)


def fig_e2e():
    e = pd.read_csv(os.path.join(T, 'e2e.csv')); keep = [('mlkem768', 'dec'), ('mldsa44', 'sign'), ('falcon512', 'verify')]
    fig, axes = plt.subplots(1, 3, figsize=(8.2, 3.0), sharey=False)
    labs = ['PQClean', 'NTT copy', 'mbedtls API', '+ limb I/O', '+ direct driver', '+ overlap']; cols = [C_NTT, C_NTT2] + C_STAGE
    for ax, (sch, op) in zip(axes, keep):
        g = e[(e.scheme == sch) & (e.op == op)].reset_index(drop=True)
        ax.bar(np.arange(len(g)), g['mean_us'] / 1000.0, color=cols[:len(g)])
        ax.set_xticks(np.arange(len(g))); ax.set_xticklabels(labs[:len(g)], rotation=40, ha='right', fontsize=7.5); ax.set_title('%s %s' % (sch, op), fontsize=9, loc='left'); ax.set_ylabel('ms')
    fig.suptitle('Whole-scheme operations with the product swapped (outputs identical to PQClean)', fontsize=9, x=0.01, ha='left')
    fig.tight_layout(); fig.savefig(os.path.join(F, 'whole_scheme.png')); plt.close(fig)


if __name__ == '__main__':
    os.makedirs(F, exist_ok=True); fig_latency(); fig_breakdown(); fig_e2e(); print('figures written to', F)
