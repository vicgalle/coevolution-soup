#!/usr/bin/env python3
"""Unified plotting for the primordial-soup replication.

Three figure modes (first argument selects the mode):

  dynamics  Generic multi-run dynamics (replication / task / entropy / steps).
            python3 plot.py dynamics OUT.png label1=run1.csv [label2=run2.csv ...]

  final     Z80 reduced-scale figure (de novo, competence, entropy, metabolic).
            python3 plot.py final OUT.png denovo_s1.csv denovo_s2.csv denovo_s3.csv compete.csv

  6502      MOS 6502 vs Z80 companion figure (reads CSVs from results/).
            python3 plot.py 6502 [OUT.png]
"""
import sys, csv, os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np

R = os.path.join(os.path.dirname(os.path.abspath(__file__)), "results")

# ------------------------------- shared helpers -----------------------------
def load(path):
    d = {}
    with open(path) as f:
        r = csv.DictReader(f)
        for k in r.fieldnames: d[k] = []
        for row in r:
            for k in r.fieldnames:
                try: d[k].append(float(row[k]))
                except: d[k].append(0.0)
    return d
def ep(d):  return [max(x, 1) for x in d["epoch"]]
def has(p): return os.path.exists(os.path.join(R, p))
def ld(p):  return load(os.path.join(R, p))


# ------------------------------- mode: dynamics -----------------------------
def mode_dynamics(argv):
    """Generic dynamics for one or more labelled run CSVs."""
    out = argv[0]
    runs = [(a.split("=", 1)[0], load(a.split("=", 1)[1])) for a in argv[1:]]
    colors = plt.cm.viridis([0.15, 0.75, 0.45, 0.9])
    mk = dict(marker="o", ms=2.5, lw=1.8)
    fig, ax = plt.subplots(2, 2, figsize=(13, 9))

    a = ax[0][0]
    for i, (label, d) in enumerate(runs):
        a.plot(ep(d), d["true_repl_frac"], color=colors[i % 4], label=f"{label}: true replicators", **mk)
        a.plot(ep(d), d["repl_sig_frac"], color=colors[i % 4], ls="--", alpha=.6, lw=1, label=f"{label}: signature")
    a.set_xscale("log"); a.set_xlabel("epoch"); a.set_ylabel("fraction of population")
    a.set_title("Emergence of self-replication"); a.set_ylim(-.02, 1.02)
    a.legend(fontsize=7); a.grid(alpha=.3)

    a = ax[0][1]
    for i, (label, d) in enumerate(runs):
        a.plot(ep(d), d["solve_frac"], color=colors[i % 4], label=f"{label}", **mk)
    a.set_xscale("log"); a.set_xlabel("epoch"); a.set_ylabel("fraction solving all 16 inputs")
    a.set_title("Emergence of task solving (2n+1)"); a.set_ylim(-.02, 1.02)
    a.legend(fontsize=7); a.grid(alpha=.3)

    a = ax[1][0]
    for i, (label, d) in enumerate(runs):
        a.plot(ep(d), d["zero_frac"], color=colors[i % 4], label=f"{label}", **mk)
    a.set_xscale("log"); a.set_xlabel("epoch"); a.set_ylabel("mean fraction of zero bytes")
    a.set_title("Tape entropy (zero-byte fraction)"); a.legend(fontsize=7); a.grid(alpha=.3)
    a.axhline(1/256, color="gray", ls=":", lw=1)

    a = ax[1][1]
    for i, (label, d) in enumerate(runs):
        a.plot(ep(d), d["avg_steps"], color=colors[i % 4], label=f"{label}", **mk)
    a.set_xscale("log"); a.set_xlabel("epoch"); a.set_ylabel("avg execution steps (of 512)")
    a.set_title("Mean execution length (metabolic)"); a.legend(fontsize=7); a.grid(alpha=.3)

    fig.tight_layout(); fig.savefig(out, dpi=130); print("wrote", out)


# ------------------------------- mode: final --------------------------------
def mode_final(argv):
    """Z80 reduced-scale figure: python3 plot.py final OUT s1 s2 s3 compete."""
    out = argv[0]
    denovo = [load(p) for p in argv[1:-1]]
    comp = load(argv[-1])
    greens = plt.cm.Greens([0.5, 0.7, 0.9]); purple = "#453781"; teal = "#1f9e89"
    fig, ax = plt.subplots(2, 2, figsize=(13, 9))

    a = ax[0][0]
    for i, d in enumerate(denovo):
        a.plot(ep(d), d["true_repl_frac"], color=greens[i], lw=1.8, marker="o", ms=2,
               label=f"seed {i+1}: true replicators")
    a.plot(ep(denovo[0]), denovo[0]["repl_sig_frac"], color="gray", ls="--", lw=1, label="seed 1: replicator signature")
    a.set_xscale("log"); a.set_xlabel("epoch"); a.set_ylabel("fraction of population")
    a.set_title("(A) Spontaneous self-replication emerges de novo\n(one 128×128 niche, task 2n+1)")
    a.set_ylim(-.02, 1.02); a.legend(fontsize=7); a.grid(alpha=.3)

    a = ax[0][1]
    a.plot(ep(comp), comp["solve_frac"], color=teal, lw=2, marker="o", ms=3, label="solve fraction")
    a.plot(ep(comp), comp["repl_sig_frac"], color="gray", ls="--", lw=1, label="replicator signature")
    a.set_xscale("log"); a.set_xlabel("epoch"); a.set_ylabel("fraction")
    a.set_title("(B) Competence-gating selects task-solvers\n(seeded solver+replicators vs matched non-solvers)")
    a.set_ylim(-.02, 1.02); a.legend(fontsize=7); a.grid(alpha=.3)
    a.annotate("solvers fix\n(0.3% → 98%)", xy=(120, 0.95), xytext=(300, 0.55),
               fontsize=8, arrowprops=dict(arrowstyle="->", color="black"))

    a = ax[1][0]
    for i, d in enumerate(denovo):
        a.plot(ep(d), d["zero_frac"], color=greens[i], lw=1.6, label=f"seed {i+1}")
    a.axhline(1/256, color="gray", ls=":", lw=1, label="random-init baseline")
    a.set_xscale("log"); a.set_xlabel("epoch"); a.set_ylabel("mean fraction of zero bytes")
    a.set_title("(C) Tape entropy: heat-death pressure, then\ncollapse as replicators fill memory")
    a.legend(fontsize=7); a.grid(alpha=.3)

    a = ax[1][1]
    a.plot(ep(denovo[0]), denovo[0]["avg_steps"], color=purple, lw=2, label="de novo replicators")
    a.plot(ep(comp), comp["avg_steps"], color=teal, lw=2, label="competence-gated solvers")
    a.set_xscale("log"); a.set_xlabel("epoch"); a.set_ylabel("avg execution steps (of 512)")
    a.set_title("(D) Metabolic dynamics: early-halting solvers\nevolve short execution")
    a.legend(fontsize=7); a.grid(alpha=.3)

    fig.suptitle("Co-evolution of self-replication and function in a Z80 primordial soup — replication of Cicala et al. (2607.09211)",
                 fontsize=11, y=1.005)
    fig.tight_layout(); fig.savefig(out, dpi=130, bbox_inches="tight"); print("wrote", out)


# ------------------------------- mode: 6502 ---------------------------------
def mode_6502(argv):
    """MOS 6502 vs Z80 companion figure (reads results/*.csv)."""
    out = argv[0] if argv else os.path.join(R, "fig_6502.png")
    Z80C = "#2c7fb8"; M65C = "#d95f0e"; GRIDC = "#b2182b"; teal = "#1f9e89"; purple = "#453781"
    fig, ax = plt.subplots(2, 3, figsize=(18, 10))

    # A: spontaneous-replicator density (build/density 500000000)
    a = ax[0][0]
    cats = ["functional\nself-copy", "true replicator\n(recursive)"]
    z80 = [9.14e-7, 3.72e-7]; m65 = [8.00e-9, 6.00e-9]
    x = np.arange(len(cats)); w = 0.36
    a.bar(x - w/2, z80, w, color=Z80C, label="Z80"); a.bar(x + w/2, m65, w, color=M65C, label="6502")
    a.set_yscale("log"); a.set_xticks(x); a.set_xticklabels(cats)
    a.set_ylabel("density per random 32-byte tape")
    a.set_title("(A) Spontaneous replication is ~100× rarer on the 6502\n(500M random tapes per CPU)")
    for xi, (zv, mv) in enumerate(zip(z80, m65)):
        a.text(xi - w/2, zv*1.3, f"{zv:.0e}", ha="center", fontsize=7)
        a.text(xi + w/2, mv*1.3, f"{mv:.0e}", ha="center", fontsize=7)
        a.text(xi, max(zv, mv)*4.0, f"{zv/mv:.0f}×", ha="center", fontsize=10, fontweight="bold")
    a.set_ylim(2e-9, 5e-6); a.legend(fontsize=8); a.grid(alpha=.3, axis="y")

    # B: de novo emergence is SCALE-dependent (log epoch axis)
    a = ax[0][1]
    for i, s in enumerate([1, 2, 3]):
        if has(f"denovo_s{s}.csv"):
            d = ld(f"denovo_s{s}.csv"); a.plot(ep(d), d["true_repl_frac"], color=Z80C, lw=1.4, alpha=0.6,
                  label=("Z80, one niche (16k progs)" if i == 0 else None))
    if has("denovo6502_notasks_s1.csv"):
        d = ld("denovo6502_notasks_s1.csv"); a.plot(ep(d), d["true_repl_frac"], color=M65C, lw=2.0,
              label="6502, one niche (16k, 10⁶ ep)")
    if has("grid6502_2h.csv"):
        d = ld("grid6502_2h.csv"); a.plot(ep(d), d["true_repl_frac"], color=GRIDC, lw=2.4,
              label="6502, full grid (524k progs)")
    a.set_xscale("log"); a.set_xlabel("epoch"); a.set_ylabel("true-replicator fraction")
    a.set_title("(B) De novo emergence is a matter of scale\nZ80 easy; 6502 needs the full grid's search")
    a.set_ylim(-.02, 1.02); a.legend(fontsize=7.5, loc="center left"); a.grid(alpha=.3)
    a.annotate("6502 grid\noriginates\n~epoch 61k", xy=(61000, 0.6), xytext=(1500, 0.7),
               fontsize=8, color=GRIDC, arrowprops=dict(arrowstyle="->", color=GRIDC))
    a.annotate("6502 one niche:\nnever, in 10⁶ ep", xy=(3e5, 0.02), xytext=(2e4, 0.28),
               fontsize=8, color=M65C, arrowprops=dict(arrowstyle="->", color=M65C))

    # C: mutational robustness (build/robust 300000)
    a = ax[0][2]
    ks = [1, 2, 4, 8]; zr = [81.21, 66.08, 44.23, 19.57]; mr = [69.26, 47.42, 21.19, 3.40]
    a.plot(ks, zr, color=Z80C, lw=2, marker="o", ms=6, label="Z80  (LDIR, 5-byte machinery)")
    a.plot(ks, mr, color=M65C, lw=2, marker="s", ms=6, label="6502 (copy loop+halt, 10-byte)")
    a.set_xticks(ks); a.set_xlabel("random byte mutations applied"); a.set_ylabel("% still self-copying")
    a.set_title("(C) The 6502 loop-replicator is less robust\n(bigger machinery = bigger mutational target)")
    a.set_ylim(0, 90); a.legend(fontsize=8); a.grid(alpha=.3)
    for k, zv, mv in zip(ks, zr, mr):
        a.text(k, zv + 2, f"{zv:.0f}", ha="center", fontsize=7, color=Z80C)
        a.text(k, mv - 5, f"{mv:.0f}", ha="center", fontsize=7, color=M65C)

    # D: competence-gating on the 6502 (linear epoch axis)
    a = ax[1][0]
    if has("compete6502.csv"):
        c = ld("compete6502.csv")
        a.plot(ep(c), c["solve_frac"], color=teal, lw=2, marker="o", ms=3, label="solve fraction (2n+1)")
        a.plot(ep(c), c["func_repl_frac"], color="gray", ls="--", lw=1.2, label="functional replicators")
    a.set_xlabel("epoch"); a.set_ylabel("fraction of population"); a.set_xlim(0, 6000)
    a.set_title("(D) Competence-gating works on the 6502\n(seeded solvers vs matched non-solvers)")
    a.set_ylim(-.02, 1.02); a.legend(fontsize=8, loc="center right"); a.grid(alpha=.3)
    a.annotate("solvers fix\n(0.3% → 98%)", xy=(150, 0.96), xytext=(1200, 0.62),
               fontsize=8, arrowprops=dict(arrowstyle="->", color="black"))

    # E: 6502 grid co-evolution + emergent curriculum (linear epoch axis)
    a = ax[1][1]
    if has("grid6502_2h.csv"):
        g = ld("grid6502_2h.csv"); e = [x/1000 for x in g["epoch"]]
        a.plot(e, g["true_repl_frac"], color=GRIDC, lw=2, label="true replicators")
        a.plot(e, g["solve_frac"], color=teal, lw=2, label="solve fraction")
        a.plot(e, [n/32 for n in g["niches_solved"]], color=purple, lw=2, ls="--", label="niches solved / 32")
    a.set_xlabel("epoch (×1000)"); a.set_ylabel("fraction")
    a.set_title("(E) 6502 full grid: replication then task-solving\nco-evolve; curriculum spreads to 16/32 niches")
    a.set_ylim(-.02, 1.02); a.legend(fontsize=8, loc="center right"); a.grid(alpha=.3)

    # F: metabolic dynamics — 6502 can't early-halt (linear epoch axis)
    a = ax[1][2]
    if has("grid32_1e5.csv"):
        z = ld("grid32_1e5.csv"); a.plot(ep(z), z["avg_steps"], color=Z80C, lw=2, label="Z80 grid")
    if has("grid6502_2h.csv"):
        g = ld("grid6502_2h.csv"); a.plot(ep(g), g["avg_steps"], color=GRIDC, lw=2, label="6502 grid")
    a.set_xlabel("epoch"); a.set_ylabel("avg execution steps (of 512)"); a.set_xlim(0, 100000)
    a.ticklabel_format(axis="x", style="sci", scilimits=(0, 0))
    a.set_title("(F) The Z80 evolves short early-halting solvers;\nthe 6502 can't — copying IS a long loop")
    a.set_ylim(0, 520); a.legend(fontsize=8, loc="center right"); a.grid(alpha=.3)

    fig.suptitle("A different CPU in the primordial soup: MOS 6502 vs Z80 — the origin of replication is substrate-dependent, and the barrier is scale",
                 fontsize=13, y=1.005)
    fig.tight_layout(); fig.savefig(out, dpi=120, bbox_inches="tight"); print("wrote", out)


# --------------------------------- dispatch ---------------------------------
MODES = {"dynamics": mode_dynamics, "final": mode_final, "6502": mode_6502}

def main():
    if len(sys.argv) < 2 or sys.argv[1] not in MODES:
        print(__doc__); sys.exit(1)
    MODES[sys.argv[1]](sys.argv[2:])

if __name__ == "__main__":
    main()
