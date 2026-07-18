#!/usr/bin/env python3
"""Plot soup replication/task dynamics from one or more run CSVs.
Usage: python3 plot.py out.png label1=run1.csv [label2=run2.csv ...]
"""
import sys, csv
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

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

def ep(d): return [max(x, 1) for x in d["epoch"]]

def main():
    out = sys.argv[1]
    runs = []
    for a in sys.argv[2:]:
        label, path = a.split("=", 1)
        runs.append((label, load(path)))
    colors = plt.cm.viridis([0.15, 0.75, 0.45, 0.9])
    mk = dict(marker="o", ms=2.5, lw=1.8)

    fig, ax = plt.subplots(2, 2, figsize=(13, 9))

    a = ax[0][0]
    for i,(label,d) in enumerate(runs):
        a.plot(ep(d), d["true_repl_frac"], color=colors[i%4], label=f"{label}: true replicators", **mk)
        a.plot(ep(d), d["repl_sig_frac"], color=colors[i%4], ls="--", alpha=.6, lw=1,
               label=f"{label}: signature")
    a.set_xscale("log"); a.set_xlabel("epoch"); a.set_ylabel("fraction of population")
    a.set_title("Emergence of self-replication"); a.set_ylim(-.02,1.02)
    a.legend(fontsize=7); a.grid(alpha=.3)

    a = ax[0][1]
    for i,(label,d) in enumerate(runs):
        a.plot(ep(d), d["solve_frac"], color=colors[i%4], label=f"{label}", **mk)
    a.set_xscale("log"); a.set_xlabel("epoch"); a.set_ylabel("fraction solving all 16 inputs")
    a.set_title("Emergence of task solving (2n+1)"); a.set_ylim(-.02,1.02)
    a.legend(fontsize=7); a.grid(alpha=.3)

    a = ax[1][0]
    for i,(label,d) in enumerate(runs):
        a.plot(ep(d), d["zero_frac"], color=colors[i%4], label=f"{label}", **mk)
    a.set_xscale("log"); a.set_xlabel("epoch"); a.set_ylabel("mean fraction of zero bytes")
    a.set_title("Tape entropy (zero-byte fraction)"); a.legend(fontsize=7); a.grid(alpha=.3)
    a.axhline(1/256, color="gray", ls=":", lw=1)

    a = ax[1][1]
    for i,(label,d) in enumerate(runs):
        a.plot(ep(d), d["avg_steps"], color=colors[i%4], label=f"{label}", **mk)
    a.set_xscale("log"); a.set_xlabel("epoch"); a.set_ylabel("avg execution steps (of 512)")
    a.set_title("Mean execution length (metabolic)"); a.legend(fontsize=7); a.grid(alpha=.3)

    fig.tight_layout()
    fig.savefig(out, dpi=130)
    print("wrote", out)

if __name__ == "__main__":
    main()
