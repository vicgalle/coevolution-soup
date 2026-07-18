# Replicating "Co-evolution of self-replication and function in a digital primordial soup"

Replication of the main experiment from Cicala et al. (arXiv:2607.09211): a
primordial soup of random **Z80 assembly** programs in which self-replication
must emerge spontaneously from random bytes, and task-solving (evaluating
polynomials) co-evolves under a competence-gated interaction rule.

## What the paper does (main experiment, §2.2 / Fig 1E)

- Population of 32-byte Z80 programs on a grid, divided into 32 niches; each
  niche has a target polynomial `f(x)`.
- Each epoch (Algorithm 1): mutate each program w.p. 1/64; pair each available
  program with an available von Neumann neighbour (w.p. `π=0.05` a random global
  partner = cross-niche pollination); **validate** P1 on its polynomial (3
  sampled inputs); execute `concat(P1,P2)` with probability `p_succ=1.0` if
  validated else `p_base=0.3` (validated prob discounted by a metabolic penalty
  `C·k/B`); split the 64-byte result back into the two cells.
- **Replication is never built in.** It only happens when executed code copies
  one program's bytes over the other's memory. Programs that both copy
  themselves *and* solve their task are selected fastest.

## This repository

| file | purpose |
|------|---------|
| `src/z80.h` | floooh/chips cycle-accurate Z80 (reference emulator, MIT/zlib) |
| `src/sandbox.h` | 64-byte execution sandbox matching Methods 4.1 |
| `src/soup.c` | Algorithm 1 + all measurement, OpenMP-parallel |
| `src/analyze.c` | verifies dumped populations for **genuine** replication |
| `src/disasm.h` | compact Z80 disassembler (reads evolved replicators) |
| `src/z80fast.h` | fast instruction-stepped Z80 core (differential-tested) |
| `src/difftest.c` | 3M-tape differential test: `z80fast.h` vs chips (100% match) |
| `plot_final.py` | the main results figure (`results/fig_final.png`) |

### The Z80 sandbox (Methods 4.1)
Two 32-byte programs are concatenated into a **64-byte** memory; every address
is taken `mod 64`. Execution starts at `PC=0` for at most `B=512` instructions.
Register init: `A=SP=0xFF`, `HL=BC=E=0`, `D=x` (validation input) or `0`
(interaction), everything else 0. Output is register `E` (result `mod 2^8`).
Stops at `HALT` or the instruction budget. `IN` returns open-bus `0xFF`.

Validated on the paper's known replicators: the **LDIR** seed `1E 20 ED B0`
produces an exact 32-byte self-copy; the **Load-Push** seed fills the partner
half; hand-written `2n+1` and `n^2` programs compute correctly.

### Algorithm 1 (`soup.c`)
Faithful to the pseudocode. Key engineering for speed (see below): the
**sequential** pairing step (shuffle + greedy availability marking, ~0.78 of the
grid selected) produces a list of *disjoint* (P1,P2) pairs; the **expensive Z80
executions run in an OpenMP parallel-for** over that list, each pair driven by a
deterministic per-pair RNG. Mutation is seeded per-index. Result: the simulation
is **bit-for-bit reproducible regardless of core count** (verified: 2-thread and
8-thread outputs identical).

## Parameters (paper Table 1)
`ℓ=32`, `B=512`, `p_base=0.3`, `p_succ=1.0`, `π=0.05`, mutation `1/64`,
validation = 3 inputs (no replacement) from `{0..15}`, output `mod 2^8`,
`C=0.3`. The full 32-niche grid is `512×1024=2^19` programs; here a single niche
is `128×128` (one paper niche) unless noted.

## Speed (the load-bearing engineering)
Random programs run ~461 of 512 steps (only ~11% halt), so Z80 execution
dominates cost. Four levers:
- **Validation short-circuit**: a non-validating P1 (≈every program early on)
  fails input #1 and skips runs 2–3 — ~1 execution/pair instead of 3.
- **Decoupled parallel execution**: the cheap sequential pairing emits *disjoint*
  (P1,P2) pairs; the expensive executions run in an OpenMP `parallel for` over
  them → near-linear scaling to 8 cores.
- **`z80fast.h`** — a from-scratch **instruction-stepped** Z80 replacing the
  cycle-accurate chips core. It is **differential-tested against chips over 3,000,000
  random tapes with 100.0000% exact agreement** on final 64-byte memory, output
  `E`, step count, and halt flag (zero opcode approximations; full CB/ED/DD/FD/
  DDCB/FDCB coverage, undocumented flag bits included). Standalone: **6.15×**
  (39 → 240 M inst/s single-thread); **in the full soup: 7.13×** (2000 single-niche
  epochs: 37.3 s → 5.2 s). The two soup builds produce **byte-identical** CSVs.
- Net: one `128×128` epoch ≈ **2 ms**; the **full 32-niche 2¹⁹-program grid ≈
  67 ms/epoch** on 8 cores — the paper's grid size, tractable on a laptop.

Build the fast soup by default; `-DUSE_CHIPS` selects the reference core. Verify
the core yourself: `clang -O3 -march=native -o build/difftest src/difftest.c && ./build/difftest`.

## Results

### 0. Full de novo co-emergence at paper scale (headline)
A single 100,000-epoch run of the **full 32-niche 2¹⁹-program grid** (~2 h on 8
cores, seed 1) reproduces the paper's central result — *both* self-replication
and task-solving emerging from random bytes:
- Replicators sweep every niche by ~epoch 500. Task-solving stays at the noise
  floor until **~epoch 50,000**, when a working solver spontaneously assembles,
  then spreads: **niches solved 0 → 18 (linear+quadratic) → 27/32**, `solve_frac`
  → 0.75, in two distinct waves — the emergent-curriculum signature (Fig 5),
  cross-niche pollination carrying solutions between niches.
- **All 18 linear and all 9 quadratic tasks solve; all 5 cubic (n³) tasks
  resist** — exactly the paper's difficulty ordering (cubics need more compute /
  a deeper curriculum). The evolved winners both solve and copy (`true_repl`→0.85),
  and `avg_steps` drops ~500 → 66 as early-halting solvers dominate.
- An evolved de novo solver+replicator for niche 0 (task `n`), disassembled,
  interleaves junk with a functional core `… LD B,D … LDIR … LD E,B ; HALT` —
  it copies its 32 bytes and returns `E=D`. Real evolved code, not the hand-built
  genotype of §3.

This is the combined emergence I had expected to require the paper's cluster
compute; it turns out ~2 h at full grid size on a laptop is enough to catch it
(the 7× fast core is what makes that feasible).

![De novo co-emergence on the full 32-niche 2¹⁹-program grid. Left: replicators sweep early, then task-solving assembles around epoch 50,000 and rises to solve_frac≈0.75. Right: niches solved jumps 0→18 (linear+quadratic), plateaus, then a second wave to 27/32 — the emergent curriculum spreading solutions between niches; the 5 cubic tasks resist.](results/fig_grid1e5.png)

### 1. Self-replication emerges de novo (verified)
From random bytes, a sharp phase transition: replicators appear and sweep the
grid. Across 3 seeds the transition occurs at stochastically varying epochs
(~150 to ~2500 at `128×128`), true-replicator fraction spiking to 0.35–0.65
before settling to a mutation–selection balance (~13% perfect replicators, ~84%
carrying the copy machinery). `zero_frac` (mean fraction of zero bytes — a
heat-death indicator) first climbs to ~0.36 as stack writes zero memory, then
**collapses exactly at the transition** as replicators fill memory with non-zero
copies. Replication is confirmed not by byte-signatures alone — which *overcount*
(sparse `01 C5` bytes appear in low-complexity tapes) — but by a **recursive
functional test**: the offspring, placed against a fresh non-zero partner, must
itself reproduce. This rejects the trivial all-NOP zero tape that byte-signatures
would miss. The same emergence occurs at the **full 2¹⁹-program 32-niche grid**
(`trepl`→0.70 by epoch 400, all niches).

### 2. The evolved replicator is the paper's Load-Push (Fig 2A)
Disassembling the evolved population shows the dominant replicator is
`LD BC,$01C5 ; PUSH BC ; …` repeated — the exact Load-Push mechanism the paper
reports as *first to appear*: load two program bytes into a register, push them
onto the stack (which grows down into the partner's half), copying the program.
LDIR replicators (which "take over later" in the paper, over 10⁴–10⁵ epochs) are
absent at these epoch counts — consistent with the paper's turnover ordering.

![Spatial structure of the soup (paper Fig 1E analogue), each program coloured by its prolog bytes. Top: initial random bytes — rainbow speckle. Bottom: after 6000 epochs — Load-Push replicator lineages (prolog `01 C5` → green) dominate every niche, with black patches (low-complexity tapes) and coloured speckle (mutants).](results/fig_grid.png)

### 3. Competence-gating selects task-solvers
De novo assembly of a *solver* is essentially unreachable at reduced scale:
validation is all-or-nothing (E must exactly equal `f(x)` on 3 inputs), so
partial solvers get no fitness gradient. To demonstrate the **selection**
mechanism directly, the soup is seeded with a matched pair of genotypes that use
the *same* bounded-LDIR copy mechanism and differ only in whether they solve:
- solver+replicator (13 bytes): `LD A,D; ADD A,A; INC A; LD BC,32; LD DE,32;
  LDIR; LD E,A; HALT` — validates `2n+1` on all 16 inputs **and** self-copies.
- non-solving dud: identical but the compute bytes NOP-ed.

Seeded at ~0.3% each, the **solver+replicator fixes at ~98% of the grid within
~400 epochs**, outcompeting both the dud and the de novo background — because
solving grants `p_succ=1.0` vs `p_base=0.3` (~3× interaction advantage). This is
the paper's core co-evolutionary claim: "a program that solves its task is far
more likely to be selected for interaction than one that does not."

### 4. Metabolic / halting dynamics (Fig 3)
As the early-halting solver+replicator dominates, mean execution length `k` drops
from ~473 to ~34 steps — programs evolve to `HALT` as soon as the output is
ready, exactly the metabolic-efficiency behaviour the paper reports.

![Reduced-scale dynamics (one 128×128 niche unless noted). (A) Spontaneous self-replication across 3 seeds, with the phase transition at stochastically varying epochs and the signature metric overcounting relative to the recursive true-replicator test. (B) Competence-gating fixes seeded task-solvers at ~98%. (C) Tape entropy climbs (heat-death pressure) then collapses as replicators fill memory. (D) Metabolic execution length shortens as early-halting solvers dominate.](results/fig_final.png)

## Honest deviations / scope
- **Scale.** Full de novo co-emergence of *both* replication and task-solving is
  reproduced at the paper's grid size (2¹⁹ programs, 32 niches, CNP) in a 100k-epoch
  run (§0). The paper runs 10⁶ epochs × 100 seeds; here a single 100k-epoch seed
  reached 27/32 niches solved. The 5 cubic tasks did not solve — matching the
  paper's finding that the hardest polynomials need more compute / curriculum.
- **Selection fraction** settles at ~0.78 (vs the paper's ~0.56); the greedy
  single-proposal matching on a 4-regular torus pairs more aggressively. Affects
  dynamics speed, not the qualitative result.
- **Validation writeback.** Following Algorithm 1's pseudocode, validation is
  read-only (only interaction writes tapes back); the prose mentions validation
  self-modification persisting, which is not modelled here.
- **The 32 polynomials** are reconstructed from Fig 5's axis labels (the paper
  gives no explicit table); the exact harder polynomials do not affect
  replication emergence.

## Running
```bash
LIBOMP=/opt/homebrew/opt/libomp   # macOS; on Linux use plain -fopenmp
clang -O3 -march=native -Xpreprocessor -fopenmp -I$LIBOMP/include \
      -L$LIBOMP/lib -lomp -o build/soup src/soup.c
clang -O3 -march=native -o build/analyze src/analyze.c

# de novo self-replication in one niche (task 10 = 2n+1), 3 seeds
for s in 1 2 3; do
  ./build/soup --niches 1 --w 128 --h 128 --epochs 5000 --task 10 --C 0.3 \
               --log 25 --seed $s --out results/denovo_s$s.csv
done
./build/soup --niches 1 --w 128 --h 128 --epochs 12000 --task 10 --C 0.3 \
             --log 200 --seed 1 --dump results/denovo.bin --out /tmp/d.csv
./build/analyze results/denovo.bin 10          # verify + disassemble evolved replicators

# competence-gating: seeded solvers vs matched non-solving replicators
./build/soup --niches 1 --w 128 --h 128 --epochs 8000 --task 10 --C 0.3 \
             --plant_solver 50 --plant_dud 50 --log 20 --seed 5 --out results/compete_fine.csv

# reduced-scale dynamics figure (results/fig_final.png)
python3 plot_final.py results/fig_final.png \
        results/denovo_s1.csv results/denovo_s2.csv results/denovo_s3.csv results/compete_fine.csv

# full paper-scale grid: 32 niches x 128x128 = 2^19 programs, cross-niche pollination
./build/soup --niches 32 --w 128 --h 128 --epochs 100000 --C 0.3 --pi 0.05 \
             --log 500 --seed 1 --out results/grid32_1e5.csv --dump results/grid32_1e5.bin
clang -O3 -march=native -o build/grid_analyze src/grid_analyze.c
./build/grid_analyze results/grid32_1e5.bin    # per-niche solve% + which tasks solved
python3 viz_grid.py results/grid32_1e5.bin 128 128 results/fig_grid_after.png 4
```

CLI flags: `--niches --w --h --epochs --task --C --pi --seed --log --budget`,
plus experiment switches `--notasks` (Fig 2D control, skip validation),
`--plant_solver K` / `--plant_dud K` (competence-gating demo), `--dump FILE`.
