#!/usr/bin/env python3
"""Render soup niches as spatial RGB images (paper Fig 1E analogue).
Each 32-byte program -> a colour from 3 of its bytes; replicator domains show as
homogeneous patches. Usage: python3 viz_grid.py dump.bin W H out.png [nniches]
"""
import sys, numpy as np
import matplotlib; matplotlib.use("Agg")
import matplotlib.pyplot as plt

path, W, H, out = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), sys.argv[4]
nshow = int(sys.argv[5]) if len(sys.argv) > 5 else 4
data = np.fromfile(path, dtype=np.uint8).reshape(-1, 32)
niche_sz = W * H
nn = data.shape[0] // niche_sz

# colour each program by bytes that vary among replicators (mid-tape) so domains
# are visible; fold via a hash to spread the palette.
def colour(block):
    # Colour by the program prolog (first executed bytes). Replicators sharing a
    # lineage share a prolog -> share an exact colour, so domains show as solid
    # patches; random tapes are rainbow speckle.
    return block[:, 0:3].astype(np.uint8)

nshow = min(nshow, nn)
cols = min(nshow, 4); rows = (nshow + cols - 1)//cols
fig, ax = plt.subplots(rows, cols, figsize=(3.1*cols, 3.1*rows), squeeze=False)
for k in range(nshow):
    block = data[k*niche_sz:(k+1)*niche_sz]
    img = colour(block).reshape(H, W, 3)
    a = ax[k//cols][k%cols]; a.imshow(img, interpolation="nearest")
    a.set_title(f"niche {k}", fontsize=9); a.set_xticks([]); a.set_yticks([])
for k in range(nshow, rows*cols): ax[k//cols][k%cols].axis("off")
fig.suptitle(f"Spatial structure of the soup ({nn} niches x {W}x{H}) — replicator domains", fontsize=11)
fig.tight_layout()
fig.savefig(out, dpi=120, bbox_inches="tight")
print("wrote", out)
