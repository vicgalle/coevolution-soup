#!/usr/bin/env python3
"""Phase diagram of the ORIGIN of self-replication on the synthetic tunable VM
(research direction 1b).

Dials: copy granularity g (minimal-copier length L*=1+32/g) and opcode-bucket
count nb (per-op prob 1/nb). Two variants of the soup:
  normal  : a partial copy propagates its partial machinery -> stepping stones.
  atomic  : replication propagates ONLY when complete -> no stepping stones
            (a loop replicator, whose half-built forms copy nothing).

Three panels:
  A  density law         rho vs L* (exponential); real Z80/6502 overlaid.
  B  two origin regimes  origin time vs 1/rho: normal ~log (stepping stones),
                         atomic ~1/rho (density-limited). Z80 is 14x faster than
                         density-limited (has partial forms); 6502 sits on the
                         density-limited line (loop, no partial forms).
  C  the engine          partial-copier fraction leads the full-copier fraction
                         (normal); with no partial spread (atomic) it stays flat.

  usage: python3 plot_min.py [out.png]
"""
import sys, os, csv, glob, re, math
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D
import numpy as np

R   = os.path.join(os.path.dirname(os.path.abspath(__file__)), "results")
OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(R, "fig_phase.png")
Z80_RHO, Z80_T_LO, Z80_T_HI = 9.1e-7, 150, 2500
M65_RHO, M65_T_CENS         = 8.0e-9, 1_000_000
Z80_LSTAR, M65_LSTAR        = 3, 6
NPOP = 128*128
GREEN, RED = "#1b7837", "#b2182b"

def load_csv(path):
    d = {}
    with open(path) as f:
        r = csv.DictReader(f)
        for k in r.fieldnames: d[k] = []
        for row in r:
            for k in r.fieldnames:
                try: d[k].append(float(row[k]))
                except: d[k].append(0.0)
    return {k: np.array(v) for k, v in d.items()}

def med(xs):
    xs = sorted(xs); n = len(xs)
    return None if n == 0 else (xs[n//2] if n % 2 else 0.5*(xs[n//2-1]+xs[n//2]))

def tfirst_of(path):
    d = load_csv(path); ep, fr = d["epoch"], d["func_repl_frac"]
    return next((ep[i] for i in range(len(ep)) if fr[i] > 0.0), None)

# density table ----------------------------------------------------------------
dens = {}
for fn in ("density_min.csv", "density_min_extra.csv", "density_min_fill.csv"):
    p = os.path.join(R, fn)
    if not os.path.exists(p): continue
    with open(p) as f:
        for row in csv.DictReader(f):
            try: nb, g, rho = int(row["nb"]), int(row["g"]), float(row["density"])
            except (ValueError, TypeError): continue
            if rho > 0: dens[(nb, g)] = (int(row["lstar"]), rho)

def load_origin(prefix):
    runs = {}
    for path in sorted(glob.glob(os.path.join(R, f"{prefix}nb*_g*_s*.csv"))):
        m = re.search(rf"{prefix}nb(\d+)_g(\d+)_s(\d+)\.csv", os.path.basename(path))
        if not m: continue
        nb, g = int(m.group(1)), int(m.group(2))
        runs.setdefault((nb, g), []).append(tfirst_of(path))
    pts = []
    for (nb, g), tfs in runs.items():
        if (nb, g) not in dens: continue
        lstar, rho = dens[(nb, g)]
        fin = [t for t in tfs if t is not None]
        pts.append(dict(nb=nb, g=g, lstar=lstar, rho=rho, t=med(fin)))
    return pts

pts_n = load_origin("origin_")
pts_a = load_origin("atomic_")

plt.rcParams.update({"font.size": 10.5})
fig, ax = plt.subplots(1, 3, figsize=(19.5, 5.7))
NBCOL = {5:"#1b7837",6:"#4d9221",7:"#66a61e",8:"#2166ac",10:"#5aae61",
         12:"#e08214",14:"#d6604d",16:"#b2182b",18:"#762a83",20:"#40004b",24:"#9970ab"}
def col(nb): return NBCOL.get(nb, "gray")

# ---- A: density law ----------------------------------------------------------
a = ax[0]
for nb in sorted({nb for (nb, g) in dens}):
    xy = sorted([(dens[(nb, g)][0], dens[(nb, g)][1]) for g in (32,16,8,4,2) if (nb, g) in dens])
    if len(xy) < 2: continue
    a.plot([p[0] for p in xy], [p[1] for p in xy], "o-", color=col(nb), lw=1.4, ms=4, label=f"nb={nb}")
a.scatter([Z80_LSTAR],[Z80_RHO], marker="*", s=280, color="k", zorder=5)
a.annotate("Z80 (LDIR)", (Z80_LSTAR, Z80_RHO), textcoords="offset points", xytext=(8,6), fontsize=9, fontweight="bold")
a.scatter([M65_LSTAR],[M65_RHO], marker="X", s=130, color="k", zorder=5)
a.annotate("6502 (loop)", (M65_LSTAR, M65_RHO), textcoords="offset points", xytext=(6,-14), fontsize=9, fontweight="bold")
a.axhline(1/NPOP, color="gray", ls="--", lw=1); a.text(16, 1/NPOP*1.4, "1 / population", fontsize=7.5, color="gray", ha="right")
a.set_yscale("log"); a.set_ylim(1e-9, 2)
a.set_xlabel("minimal-copier length  L*  (instructions)"); a.set_ylabel("spontaneous copier density  ρ")
a.set_title("A  Density collapses exponentially in L*")
a.grid(True, which="both", ls=":", alpha=0.4); a.legend(fontsize=7, ncol=2)

# ---- B: two origin regimes ---------------------------------------------------
b = ax[1]
def scat(pts, mode):
    color = GREEN if mode == "normal" else RED
    for p in pts:
        if p["t"] is None: continue
        mk = "o" if p["g"] == 4 else "s"                     # circle=g4 (L*9), square=g2 (L*17)
        if mode == "normal":                                 # filled green
            b.scatter(1/p["rho"], max(p["t"],1), s=70, marker=mk, color=color,
                      edgecolor="k", lw=0.5, zorder=4)
        else:                                                # open red
            b.scatter(1/p["rho"], max(p["t"],1), s=70, marker=mk, facecolor="none",
                      edgecolor=color, lw=1.6, zorder=4)
scat(pts_n, "normal"); scat(pts_a, "atomic")

def fitpts(pts):
    xs, ys = [], []
    for p in pts:
        if p["t"] and p["t"] > 0 and p["rho"] < 1.0/NPOP:
            xs.append(1/p["rho"]); ys.append(p["t"])
    return np.array(xs), np.array(ys)
xn, yn = fitpts(pts_n); xa, ya = fitpts(pts_a)
Bc = Ac = s = ic = None
if len(xn) >= 2:
    Bc, Ac = np.polyfit(np.log10(xn), yn, 1)
    xx = np.logspace(math.log10(min(xn)), math.log10(max(xn)), 60)
    b.plot(xx, Ac + Bc*np.log10(xx), "-", color=GREEN, lw=1.8)
if len(xa) >= 2:
    s, ic = np.polyfit(np.log10(xa), np.log10(ya), 1)
    xx = np.logspace(math.log10(min(xa)), math.log10(1/M65_RHO)+0.2, 80)
    b.plot(xx, 10**ic * xx**s, "--", color=RED, lw=1.8)
b.errorbar([1/Z80_RHO], [math.sqrt(Z80_T_LO*Z80_T_HI)],
           yerr=[[math.sqrt(Z80_T_LO*Z80_T_HI)-Z80_T_LO],[Z80_T_HI-math.sqrt(Z80_T_LO*Z80_T_HI)]],
           fmt="*", ms=19, color="k", capsize=5, zorder=6)
b.scatter([1/M65_RHO],[M65_T_CENS], marker="X", s=160, color="k", zorder=6)
b.annotate("", xy=(1/M65_RHO, M65_T_CENS*2.3), xytext=(1/M65_RHO, M65_T_CENS), arrowprops=dict(arrowstyle="->", color="k"))

# explicit legend: colour/fill = variant, shape = g (i.e. L*)
handles = [
    Line2D([0],[0], color=GREEN, lw=1.8,
           label=(f"normal fit  T ≈ {Ac:.0f}+{Bc:.0f}·log₁₀(1/ρ)" if Bc is not None else "normal fit")),
    Line2D([0],[0], color=RED, lw=1.8, ls="--",
           label=(f"atomic fit  T ∝ (1/ρ)$^{{{s:.2f}}}$" if s is not None else "atomic fit")),
    Line2D([0],[0], marker="o", ls="none", markerfacecolor=GREEN, markeredgecolor="k",
           ms=8, label="normal (filled, green): stepping stones"),
    Line2D([0],[0], marker="o", ls="none", markerfacecolor="none", markeredgecolor=RED,
           mew=1.5, ms=8, label="atomic (open, red): no stepping stones"),
    Line2D([0],[0], marker="o", ls="none", markerfacecolor="gray", markeredgecolor="k",
           ms=8, label="circle: g=4  (L*=9)"),
    Line2D([0],[0], marker="s", ls="none", markerfacecolor="gray", markeredgecolor="k",
           ms=8, label="square: g=2  (L*=17)"),
    Line2D([0],[0], marker="*", ls="none", markerfacecolor="k", markeredgecolor="k",
           ms=14, label="Z80 (measured: 14× faster)"),
    Line2D([0],[0], marker="X", ls="none", markerfacecolor="k", markeredgecolor="k",
           ms=10, label="6502 (measured: never in 1e6)"),
]
b.legend(handles=handles, fontsize=7.2, loc="upper left")
b.set_xscale("log"); b.set_yscale("log")
b.set_xlabel("1 / ρ   (copier rarity)"); b.set_ylabel("de-novo origin epoch  T")
b.set_title("B  Origin regime set by stepping-stone availability")
b.grid(True, which="both", ls=":", alpha=0.4)

# ---- C: the engine (partial-copier wave) -------------------------------------
c = ax[2]
p = os.path.join(R, "mech_normal_g2nb6.csv")
if os.path.exists(p):
    d0 = load_csv(p)
    c.plot(np.maximum(d0["epoch"],1), d0["partial_frac"], color="#e08214", lw=2, label="partial copiers (16–29 bytes)")
    c.plot(np.maximum(d0["epoch"],1), d0["func_repl_frac"], color=GREEN, lw=2, label="full copiers (≥30)")
pa = os.path.join(R, "mech_atomic_g2nb6.csv")
if os.path.exists(pa):
    da = load_csv(pa)
    c.plot(np.maximum(da["epoch"],1), da["partial_frac"], color="#e08214", lw=1.3, ls=":", alpha=0.8,
           label="partial copiers (atomic)")
c.set_xscale("log"); c.set_xlabel("epoch"); c.set_ylabel("fraction of population")
c.set_title("C  A wave of partial copiers precedes full ones")
c.grid(True, which="both", ls=":", alpha=0.4); c.legend(fontsize=8, loc="center left")

fig.suptitle("Origin of self-replication on a tunable substrate: density sets rarity, but recombination "
             "of partial copiers (stepping stones) sets the origin law", fontsize=13, y=1.02)
fig.tight_layout(); fig.savefig(OUT, dpi=140, bbox_inches="tight")
print("wrote", OUT)

# ---- text summary ------------------------------------------------------------
if Bc is not None: print(f"normal: T ≈ {Ac:.0f} + {Bc:.1f}·log10(1/rho)")
if s is not None:
    print(f"atomic: T ∝ (1/rho)^{s:.2f}")
    print(f"  Z80 rho {Z80_RHO:.0e}: atomic predicts {10**ic*(1/Z80_RHO)**s:.0f}, actual ~612 (14x faster)")
    print(f"  6502 rho {M65_RHO:.0e}: atomic predicts {10**ic*(1/M65_RHO)**s:.0f} single-niche; /32 grid = {10**ic*(1/M65_RHO)**s/32:.0f} (obs ~61000)")
