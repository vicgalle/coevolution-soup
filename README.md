# Replicating "Co-evolution of self-replication and function in a digital primordial soup"

<p align="center">
  <img src="results/hero.gif" alt="Evolution of the 32-niche Z80 soup over 100,000 epochs" width="100%">
</p>

<p align="center"><em>The full grid of 32 niches (2<sup>19</sup> programs) over 100,000 epochs, with niches tiled in order of increasing task difficulty. The soup starts as random noise; self-replicators (blue) then spread through every niche; and task-solvers (green) appear spontaneously around epoch 50,000 and propagate across 27 of the 32 niches. The five hardest niches, whose targets are cubic polynomials (bottom-right), keep replicating but never solve their task.</em></p>

This repository reproduces the main experiment of Cicala et al. ([arXiv:2607.09211](https://arxiv.org/abs/2607.09211)): a primordial soup of random Z80 assembly programs in which self-replication is not built in but must emerge from random bytes, while the ability to solve a task (evaluating a polynomial) co-evolves under a competence-gated interaction rule. The code, the figures below, and the animation above can all be regenerated from scratch with the commands at the end.

## The model

Following the paper (Section 2.2, Fig. 1E), the population consists of 32-byte Z80 programs arranged on a grid divided into 32 niches, each niche assigned a target polynomial `f(x)`. Every epoch applies Algorithm 1. Each program is mutated with probability 1/64. Each still-available program is then paired with an available von Neumann neighbour, except that with probability `π = 0.05` the partner is drawn from the whole population instead, which the paper calls cross-niche pollination. The first program of the pair is validated on its polynomial for three sampled inputs, and the concatenation `concat(P1, P2)` is executed with probability `p_succ = 1.0` if the program validated and `p_base = 0.3` otherwise, the validated probability being discounted by a metabolic penalty `C·k/B`. The resulting 64-byte tape is split back into the two cells.

Reproduction is never issued as a command. It happens only when the executed code copies one program's bytes over the other's memory, so a program that both copies itself and solves its task is the one selected most often. The point of the experiment is that neither ability is provided in advance; both have to be discovered by the population.

> **Companion experiment: a different CPU.** [`README_6502.md`](README_6502.md) reruns the same model on the **MOS 6502** to ask whether the spontaneous *origin* of replication is substrate-general. Lacking the Z80's one-instruction block move `LDIR`, the 6502 forms a replicator ~100× more rarely from random code; on a single niche it never originates one even in the paper's 10⁶-epoch budget, but on the **full 32-niche grid** (2¹⁹ programs) it does: replication ignites de novo around epoch 61k, sweeps to 91%, and task-solving then co-evolves across 16 of 32 niches, exactly as on the Z80, just far later and slower.

> **Companion experiment: what sets the origin barrier.** [`README_stepping_stones.md`](README_stepping_stones.md) replaces the two fixed CPUs with a synthetic instruction set whose copy primitive can be dialed, asking not *whether* but *what* makes replication easy or hard to originate. The density of copiers in random code falls exponentially with copier length, as expected, but the time for one to *originate* does not follow density. When partial copiers can recombine into complete ones (**stepping stones**), origin is fast and grows only logarithmically with rarity; remove that path and it reverts to a density-limited law `T ∝ 1/ρ`. This is the Z80/6502 split at a mechanistic level: the Z80's block move has partial forms and originates ~14× faster than density alone predicts, whereas the 6502's loop has none and, on a single niche, effectively never originates.

## Repository

| file | purpose |
|------|---------|
| `src/z80.h` | floooh/chips cycle-accurate Z80, used as the reference emulator (MIT/zlib) |
| `src/z80fast.h` | fast instruction-stepped Z80 core (differential-tested against chips) |
| `src/sandbox.h` | 64-byte execution sandbox matching Methods 4.1 |
| `src/soup.c` | Algorithm 1 and all measurement, parallelised with OpenMP |
| `src/analyze.c` | verifies a dumped population for genuine replication |
| `src/grid_analyze.c` | per-niche solve rates for the 32-niche grid |
| `src/disasm.h` | small Z80 disassembler, used to read evolved replicators |
| `src/difftest.c` | differential test of `z80fast.h` against chips over 3M tapes |
| `plot.py`, `viz_grid.py`, `make_gif.py` | figures (`dynamics`/`final`/`6502` modes) and the animation |

### The Z80 sandbox

Two 32-byte programs are concatenated into a 64-byte memory, and every address is taken modulo 64. Execution begins at `PC = 0` and runs for at most `B = 512` instructions. Registers are initialised with `A = SP = 0xFF`, `HL = BC = E = 0`, `D` set to the task input `x` during validation or to `0` during interaction, and everything else zero; the output is read from register `E`, modulo 2^8. Execution stops at a `HALT` or when the budget is exhausted, and `IN` returns an open-bus `0xFF`. I checked the sandbox against the replicators the paper describes: the `LDIR` seed `1E 20 ED B0` produces an exact 32-byte self-copy, the Load-Push seed fills the partner half, and hand-written `2n+1` and `n^2` programs compute the right values.

### Algorithm 1

The implementation follows the pseudocode closely. For performance (see below), the pairing step, which is sequential and cheap, is separated from the executions, which are expensive: it emits a list of disjoint `(P1, P2)` pairs, and the executions then run in an OpenMP parallel loop over that list. Each pair draws from a deterministic per-pair RNG and mutation is seeded per program index, so the whole simulation is reproducible bit for bit regardless of the number of threads (I verified that two- and eight-thread runs produce identical output).

## Parameters

The defaults follow the paper's Table 1: program length 32 bytes, instruction budget `B = 512`, `p_base = 0.3`, `p_succ = 1.0`, `π = 0.05`, mutation rate 1/64, validation on three inputs drawn without replacement from `{0,…,15}`, output modulo 2^8, and metabolic coefficient `C = 0.3`. The full grid is `512 × 1024 = 2^19` programs across 32 niches; a single niche is `128 × 128`, the same size as one niche in the paper.

## Performance

Almost all of the cost is Z80 execution, because random programs rarely halt (about 11% do) and the rest run the full 512-instruction budget. Three choices keep the simulation fast enough to run the paper's grid on a laptop.

First, validation short-circuits: a program that already fails the first sampled input is not run on the other two, which for the mostly non-validating early population means about one execution per pair rather than three.

Second, as noted above, the executions are parallelised over disjoint pairs and scale nearly linearly to eight cores.

Third, and most importantly, the cycle-accurate chips core is replaced by `z80fast.h`, a hand-written instruction-stepped core. I checked it against chips on three million random tapes and found exact agreement on the final 64-byte memory, the output register, the step count, and the halt flag, with no opcode approximations and full coverage of the CB/ED/DD/FD prefix pages, undocumented flag bits included. It is 6.1 times faster in isolation (39 to 240 million instructions per second on one thread) and 7.1 times faster inside the soup, and the two builds produce byte-identical CSVs. With it, a `128 × 128` niche runs at roughly 2 ms per epoch and the full `2^19`-program grid at roughly 67 ms per epoch. The fast core is the default; `-DUSE_CHIPS` selects the reference core, and the differential test can be run with `clang -O3 -march=native -o build/difftest src/difftest.c && ./build/difftest`.

## Results

### Co-emergence at the paper's scale

A single run of the full 32-niche grid (`2^19` programs) for 100,000 epochs, from one random seed, reproduces the paper's main result: self-replication and task-solving both emerge from random bytes. Replicators spread through every niche within the first few hundred epochs. Task-solving then stays at the noise floor until about epoch 50,000, when a working solver assembles by chance in one niche; solutions propagate from there, and the number of solved niches rises from 0 to 18 (the linear and quadratic tasks) and, after a plateau, to 27 of 32. This staged spread across niches is the emergent curriculum the paper describes (Fig. 5), with cross-niche pollination carrying solutions between niches. All 18 linear and all 9 quadratic tasks are solved and none of the 5 cubic tasks is, which matches the difficulty ordering the paper reports. The genotypes that come to dominate both solve and copy themselves (true-replicator fraction about 0.85), and the mean execution length falls from about 500 to 66 steps as early-halting solvers take over. The run takes about two hours on eight cores, which is what the fast core makes feasible.

![Full 32-niche grid over 100,000 epochs. Left: replicators spread early, then task-solving appears around epoch 50,000 and rises to a solve fraction near 0.75. Right: the number of solved niches rises from 0 to 18, plateaus, and then reaches 27 of 32 in a second wave, as solutions spread between niches.](results/fig_grid1e5.png)

### Self-replication emerges de novo

Self-replication appears as a sharp phase transition: from random bytes, replicators arise and sweep the grid. Across three seeds of a single niche the transition happens at varying epochs, roughly between 150 and 2,500, with the true-replicator fraction peaking between 0.35 and 0.65 before settling into a mutation–selection balance in which about 13% are perfect replicators and about 84% still carry the copy machinery. The mean fraction of zero bytes, which I track as a heat-death indicator, first climbs to about 0.36 (stack writes tend to zero memory) and then collapses at the transition, as replicators fill memory with non-zero copies. I do not count replicators by byte-signature alone, since those signatures overcount (short sequences such as `01 C5` occur by chance in low-complexity tapes); instead I use a recursive functional test, in which the offspring, placed against a fresh non-zero partner, must itself reproduce. This rejects, for example, the trivial all-`NOP` zero tape that a signature search would wrongly accept. The same emergence is seen at the full `2^19`-program grid, where the true-replicator fraction reaches about 0.70 by epoch 400 across all niches.

### The replicator architecture turns over: Load-Push, then LDIR

The dominant replicator architecture changes over time, as the paper reports (Fig. 2D). Counting byte-signatures on the evolved populations:

| run | LDIR family | Load-Push |
|-----|:-----------:|:---------:|
| early, short single niche (≤ 12k epochs) | 0% | ~83% |
| full 32-niche grid after 100k epochs | 99.3% | 0.03% |

Load-Push appears first. In short runs the dominant replicator is `LD BC,$01C5 ; PUSH BC` repeated, which is the mechanism the paper identifies as the first to appear: it loads two program bytes into a register and pushes them onto the stack, which grows down into the partner's half, thereby copying the program. It uses the entire 32-byte tape.

LDIR takes over once function co-evolves. In the 100k full-grid run, where task-solving emerged, Load-Push is essentially gone and 99.3% of tapes carry an LDIR-family copy loop. Every dominant genotype is a bounded LDIR replicator with task code appended, for instance the disassembled winner from niche 0 (whose task is `n`):

```
LD E,$20 ; LD C,E ; LDIR ; … compute … ; LD E,<result> ; HALT
```

Here `LD E,$20` points the copy at byte 32 (the partner half), `LD C,E` sets the count to 32, and `LDIR` copies the tape, which is exactly the LDIR replicator of Fig. 2B (`HL = 0`, `DE = 32`, `BC = 32`). The reason this architecture wins is that the copy loop takes only about six bytes and leaves the rest of the tape free for the code that computes `f(x)`, whereas a Load-Push replicator consumes the whole tape and has no room for a solver. Once solving becomes advantageous through the `p_succ` gate, the compact architecture that can do both displaces the one that cannot. In the paper's terms, the pressure to solve tasks reshapes the reproductive machinery: function does not simply sit on top of replication, it changes which replicator wins.

![Spatial structure of the soup, with each program coloured by its first bytes. Top: the initial random population, appearing as coloured noise. Bottom: after 6,000 epochs, Load-Push lineages (whose prolog `01 C5` maps to green) dominate every niche, interspersed with darker low-complexity tapes and coloured mutants.](results/fig_grid.png)

### Competence-gating selects task-solvers

Assembling a solver de novo is very unlikely at reduced scale, because validation is all-or-nothing (the output must exactly equal `f(x)` on the sampled inputs) and so a partial solver receives no fitness gradient. To exhibit the selection mechanism on its own, I seed the soup with two matched genotypes that share the same bounded-LDIR copy loop and differ only in whether they solve: a solver-plus-replicator (`LD A,D; ADD A,A; INC A; LD BC,32; LD DE,32; LDIR; LD E,A; HALT`, which validates `2n+1` on all 16 inputs and also self-copies) and a non-solving control that is identical but with the arithmetic instructions replaced by `NOP`s. Seeded at about 0.3% each, the solver-plus-replicator fixes at about 98% of the grid within roughly 400 epochs, outcompeting both the control and the de novo background, because solving grants `p_succ = 1.0` against `p_base = 0.3`, roughly a threefold advantage in interaction rate. This is the paper's central co-evolutionary claim, that a program that solves its task is far more likely to be selected than one that does not.

### Metabolic and halting dynamics

As the early-halting solver-plus-replicator comes to dominate, the mean execution length falls from about 473 to about 34 steps, because the programs evolve to `HALT` as soon as the output is ready. This is the metabolic efficiency the paper reports (Fig. 3).

![Reduced-scale dynamics on a single 128×128 niche. (A) Spontaneous self-replication across three seeds, showing the signature count overstating replication relative to the recursive functional test. (B) Competence-gating fixing seeded task-solvers near 98%. (C) Tape entropy rising under heat-death pressure and then collapsing as replicators fill memory. (D) Execution length shortening as early-halting solvers dominate.](results/fig_final.png)

### A different CPU: the MOS 6502

As a companion experiment (see [`README_6502.md`](README_6502.md)), I reran the identical model on the MOS 6502, the other CPU core in floooh/chips, to ask whether the spontaneous origin of replication is a general property of these soups or specific to the Z80. It turns out to be specific, and the difference is one of scale. The 6502 has no single-instruction block move like `LDIR`, so a replicator must be a copy loop plus an explicit halt; this makes it about 100× rarer to assemble from random bytes (114× for functional self-copiers and 62× for true replicators, measured over 500M tapes per CPU) and about six times less robust to mutation. A single niche therefore never originates a replicator even in the paper's 10⁶-epoch budget, whereas the Z80 does so within a few hundred epochs. On the full 32-niche grid, however, the larger search suffices: replication ignites de novo around epoch 61,000, sweeps to 91%, and task-solving then co-evolves across 16 of the 32 niches, exactly as on the Z80 — only later, slower, and without the metabolic shortening a compact `LDIR` replicator permits, since the 6502's copy loop keeps execution long throughout.

![MOS 6502 vs Z80. (A) Spontaneous replicators are ~100× rarer in random 6502 code. (B) De novo emergence is scale-dependent: fast on a Z80 niche, absent on a 6502 niche over 10⁶ epochs, but present on the full 6502 grid at epoch ~61k. (C) The 6502 loop-replicator is less mutationally robust. (D) Competence-gating still selects task-solvers. (E) On the grid, replication and then task-solving co-evolve, the curriculum reaching 16/32 niches. (F) The Z80 evolves short early-halting solvers; the 6502 cannot, because copying is a long loop.](results/fig_6502.png)

### What sets the origin barrier: a tunable substrate

In a further companion experiment (see [`README_stepping_stones.md`](README_stepping_stones.md)), I replaced the two fixed CPUs with a synthetic instruction set whose copy primitive can be dialed, turning the length of the minimal self-copier and its density in random code into continuous knobs. This separates two things the 6502 comparison had left entangled. The density of spontaneous copiers falls exponentially with copier length, as the 6502 result already suggested. The de novo origin time, however, does not track density: with stepping stones (partial copiers that reproduce only part of themselves but copy their own prefix, so recombination composes complete ones from partial parts), origin is fast, grows only logarithmically with rarity, and is independent of population size and mutation rate; removing that recombination path recovers a density-limited law `T ∝ 1/ρ`. The two real CPUs sit on the two laws: the Z80's block-move and load-push copiers have partial forms and originate about 14× faster than density alone predicts, whereas the 6502's loop has none and lands on the density-limited line, which is why it never originates on a single niche but does on the 32× larger grid. The barrier is therefore set by whether the copy primitive admits incremental, recombinable assembly, and not by rarity alone.

![Origin of self-replication on a tunable substrate. (A) Copier density falls exponentially with minimal-copier length L*, and the real Z80 and 6502 lie on the same trend. (B) De novo origin time against copier rarity 1/ρ: with stepping stones (green) it grows only logarithmically, without them (red, the atomic control) it follows the density-limited law T ∝ 1/ρ, with the Z80 far below the line and the 6502 on it. (C) A wave of partial copiers precedes the full ones, the recombination that makes origin fast.](results/fig_phase.png)

## Scope and deviations

Two remarks on how faithful this is, and where it falls short of the paper.

The combined de novo emergence of both replication and task-solving is reproduced at the paper's grid size (`2^19` programs, 32 niches, cross-niche pollination) in a single 100k-epoch run, which reached 27 of 32 niches solved. The paper runs 10^6 epochs across 100 seeds, so this is one seed at a tenth of the epoch budget; the five cubic tasks were not solved here, consistent with the paper's observation that the hardest polynomials need more compute or a deeper curriculum.

Three smaller differences are worth noting. The fraction of the grid selected each epoch settles around 0.78 rather than the paper's 0.56, because the single-proposal matching on a 4-regular torus pairs more aggressively; this affects the speed of the dynamics but not the qualitative outcome. Validation is read-only, following the pseudocode, so only interaction writes tapes back; the prose mentions validation self-modification persisting, which I do not model. Finally, the 32 polynomials are reconstructed from the axis labels of Fig. 5, since the paper gives no explicit list, but the exact choice of the harder targets does not affect the emergence of replication.

## Reproducing the results

```bash
LIBOMP=/opt/homebrew/opt/libomp   # macOS; on Linux use plain -fopenmp
clang -O3 -march=native -Xpreprocessor -fopenmp -I$LIBOMP/include \
      -L$LIBOMP/lib -lomp -o build/soup src/soup.c
clang -O3 -march=native -o build/analyze src/analyze.c

# self-replication in one niche (task 10 = 2n+1), three seeds
for s in 1 2 3; do
  ./build/soup --niches 1 --w 128 --h 128 --epochs 5000 --task 10 --C 0.3 \
               --log 25 --seed $s --out results/denovo_s$s.csv
done
./build/soup --niches 1 --w 128 --h 128 --epochs 12000 --task 10 --C 0.3 \
             --log 200 --seed 1 --dump results/denovo.bin --out /tmp/d.csv
./build/analyze results/denovo.bin 10          # verify and disassemble evolved replicators

# competence-gating: seeded solvers against matched non-solving controls
./build/soup --niches 1 --w 128 --h 128 --epochs 8000 --task 10 --C 0.3 \
             --plant_solver 50 --plant_dud 50 --log 20 --seed 5 --out results/compete_fine.csv

# reduced-scale dynamics figure
python3 plot.py final results/fig_final.png \
        results/denovo_s1.csv results/denovo_s2.csv results/denovo_s3.csv results/compete_fine.csv

# full grid: 32 niches x 128x128 = 2^19 programs, with cross-niche pollination
./build/soup --niches 32 --w 128 --h 128 --epochs 100000 --C 0.3 --pi 0.05 \
             --log 500 --seed 1 --out results/grid32_1e5.csv --dump results/grid32_1e5.bin
clang -O3 -march=native -o build/grid_analyze src/grid_analyze.c
./build/grid_analyze results/grid32_1e5.bin    # per-niche solve rates
```

The main command-line flags are `--niches`, `--w`, `--h`, `--epochs`, `--task`, `--C`, `--pi`, `--seed`, `--log`, and `--budget`, together with `--notasks` (the Fig. 2D control, which skips validation), `--plant_solver K` and `--plant_dud K` (the competence-gating experiment), `--dump FILE`, and `--anim` with `--animout FILE` (the animation frames).
