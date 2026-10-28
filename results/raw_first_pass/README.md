Raw output of the first complete run on the board (`run_bench.py --quick`, short repetition counts), taken before the CPU-side packing code was tuned.
Kept because it motivated that tuning: at this point packing, unpacking and unshifting cost more than the hardware multiply (see `prof.csv`).
Not used for any number in the README; `results/raw/` is the final run.
