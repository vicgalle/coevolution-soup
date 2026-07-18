#!/usr/bin/env python3
"""Final figure for the replication.
Usage: python3 plot_final.py out.png denovo_s1.csv denovo_s2.csv denovo_s3.csv compete.csv
"""
import sys, csv
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

def load(p):
    d={}
    with open(p) as f:
        r=csv.DictReader(f)
        for k in r.fieldnames: d[k]=[]
        for row in r:
            for k in r.fieldnames:
                try: d[k].append(float(row[k]))
                except: d[k].append(0.0)
    return d
def ep(d): return [max(x,1) for x in d["epoch"]]

out=sys.argv[1]
denovo=[load(p) for p in sys.argv[2:-1]]
comp=load(sys.argv[-1])
greens=plt.cm.Greens([0.5,0.7,0.9])
purple="#453781"; teal="#1f9e89"

fig,ax=plt.subplots(2,2,figsize=(13,9))

# A: de novo self-replication, multiple seeds
a=ax[0][0]
for i,d in enumerate(denovo):
    a.plot(ep(d), d["true_repl_frac"], color=greens[i], lw=1.8, marker="o", ms=2,
           label=f"seed {i+1}: true replicators")
a.plot(ep(denovo[0]), denovo[0]["repl_sig_frac"], color="gray", ls="--", lw=1, label="seed 1: replicator signature")
a.set_xscale("log"); a.set_xlabel("epoch"); a.set_ylabel("fraction of population")
a.set_title("(A) Spontaneous self-replication emerges de novo\n(one 128×128 niche, task 2n+1)")
a.set_ylim(-.02,1.02); a.legend(fontsize=7); a.grid(alpha=.3)

# B: competence-gated task solving
a=ax[0][1]
a.plot(ep(comp), comp["solve_frac"], color=teal, lw=2, marker="o", ms=3, label="solve fraction")
a.plot(ep(comp), comp["repl_sig_frac"], color="gray", ls="--", lw=1, label="replicator signature")
a.set_xscale("log"); a.set_xlabel("epoch"); a.set_ylabel("fraction")
a.set_title("(B) Competence-gating selects task-solvers\n(seeded solver+replicators vs matched non-solvers)")
a.set_ylim(-.02,1.02); a.legend(fontsize=7); a.grid(alpha=.3)
a.annotate("solvers fix\n(0.3% → 98%)", xy=(120,0.95), xytext=(300,0.55),
           fontsize=8, arrowprops=dict(arrowstyle="->",color="black"))

# C: entropy collapse at replicator emergence
a=ax[1][0]
for i,d in enumerate(denovo):
    a.plot(ep(d), d["zero_frac"], color=greens[i], lw=1.6, label=f"seed {i+1}")
a.axhline(1/256, color="gray", ls=":", lw=1, label="random-init baseline")
a.set_xscale("log"); a.set_xlabel("epoch"); a.set_ylabel("mean fraction of zero bytes")
a.set_title("(C) Tape entropy: heat-death pressure, then\ncollapse as replicators fill memory")
a.legend(fontsize=7); a.grid(alpha=.3)

# D: metabolic execution length
a=ax[1][1]
a.plot(ep(denovo[0]), denovo[0]["avg_steps"], color=purple, lw=2, label="de novo replicators")
a.plot(ep(comp), comp["avg_steps"], color=teal, lw=2, label="competence-gated solvers")
a.set_xscale("log"); a.set_xlabel("epoch"); a.set_ylabel("avg execution steps (of 512)")
a.set_title("(D) Metabolic dynamics: early-halting solvers\nevolve short execution")
a.legend(fontsize=7); a.grid(alpha=.3)

fig.suptitle("Co-evolution of self-replication and function in a Z80 primordial soup — replication of Cicala et al. (2607.09211)",
             fontsize=11, y=1.005)
fig.tight_layout()
fig.savefig(out,dpi=130,bbox_inches="tight")
print("wrote",out)
