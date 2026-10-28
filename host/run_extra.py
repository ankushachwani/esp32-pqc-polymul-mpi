"""Follow-up to run_bench.py on the same firmware: ML-KEM decapsulation / decrypt step with the optimised path without the fused sum (switch set 19),
because the ablation showed the fused sum is slower than three separate products once the CPU code is fast. Writes raw/e2e_extra.csv."""
import os, sys
from board import Board, kv
sys.path.insert(0, os.path.dirname(__file__))
from run_bench import Out, log
out = Out(sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), '..', 'results', 'raw')); b = Board()
for op, reps in (('dec', 80), ('indcpa', 320)):
    cmd = 'E2E kem %s f19 %d 3' % (op, reps)
    for ln in b.cmd(cmd, timeout=600):
        log(ln)
        if ln.startswith('E '): r = kv(ln); r['cmd'] = cmd; out.add('e2e_extra', r)
out.flush()
