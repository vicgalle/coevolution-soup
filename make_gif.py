#!/usr/bin/env python3
"""Render an .anim file (from soup --anim or render_dump) into a PNG (1 frame)
or an animated GIF (many frames).

Colour scheme (tells the story):
  status 0 (inert/random)     -> near-black, faint prolog tint
  status 1 (self-replicator)  -> vivid cool hue (cyan..magenta) by lineage prolog
  status 2 (solves its task)  -> bright green glow

The 32 niches are tiled 4 rows x 8 cols in task order, so the 5 cubic niches sit
bottom-right and visibly stay dark while the green "function" wave floods in.
Usage: python3 make_gif.py in.anim out.(png|gif)
"""
import sys, struct, numpy as np
from matplotlib.colors import hsv_to_rgb
from PIL import Image, ImageDraw, ImageFont
import matplotlib.font_manager as fm

ROWS, COLS = 4, 8
GRID = (30, 32, 40)          # gridline colour between niches
BAR_H = 46                   # caption bar height

def load(path):
    with open(path,"rb") as f:
        magic,W,H,L = struct.unpack("<4i", f.read(16))
        recs = L*W*H
        frames=[]
        while True:
            hdr=f.read(4)
            if len(hdr)<4: break
            ep=struct.unpack("<i",hdr)[0]
            buf=f.read(recs*4)
            if len(buf)<recs*4: break
            frames.append((ep, np.frombuffer(buf,dtype=np.uint8).reshape(recs,4)))
    return W,H,L,frames

def colours(d):
    b0,b1,b2,st = d[:,0].astype(np.float64), d[:,1].astype(np.float64), d[:,2].astype(np.float64), d[:,3]
    h = ((b0*7 + b1*13 + b2*29) % 256)/256.0
    # inert: dim colourful primordial static; replicator: vivid cool hue by lineage;
    # solver: bright green glow.
    hue = np.where(st==2, 0.30+0.09*h, np.where(st==1, 0.50+0.50*h, h))
    sat = np.where(st==2, 0.90, np.where(st==1, 0.88, 0.55))
    val = np.where(st==2, 1.00, np.where(st==1, 0.96, 0.30))
    rgb = hsv_to_rgb(np.stack([hue,sat,val],axis=1))
    return (rgb*255).astype(np.uint8)

def tile(rgb, W,H,L):
    img = np.zeros((ROWS*H, COLS*W, 3), np.uint8)
    NSZ=W*H
    for nch in range(L):
        block = rgb[nch*NSZ:(nch+1)*NSZ].reshape(H,W,3)
        r,c = nch//COLS, nch%COLS
        img[r*H:(r+1)*H, c*W:(c+1)*W] = block
    for r in range(1,ROWS): img[r*H-1:r*H+1,:] = GRID
    for c in range(1,COLS): img[:,c*W-1:c*W+1] = GRID
    return img

def niches_solved(d, W,H,L):
    NSZ=W*H; n=0
    for k in range(L):
        st=d[k*NSZ:(k+1)*NSZ,3]
        if (st==2).mean()>=0.10: n+=1
    return n

def font(sz):
    try: return ImageFont.truetype(fm.findfont("DejaVu Sans"), sz)
    except Exception: return ImageFont.load_default()

def frame_image(ep,d,W,H,L, scale=1.0):
    grid = tile(colours(d), W,H,L)
    gh,gw = grid.shape[:2]
    canvas = Image.new("RGB",(gw, gh+BAR_H),(8,9,14))
    canvas.paste(Image.fromarray(grid),(0,0))
    dr=ImageDraw.Draw(canvas)
    f1,f2=font(24),font(16)
    ns=niches_solved(d,W,H,L)
    dr.text((14, gh+11), f"epoch {ep:,}", font=f1, fill=(235,235,245))
    # legend
    lx=gw//2-150
    for (col,txt) in [((70,235,120),"solves task"),((90,150,235),"self-replicator"),((70,72,86),"inert")]:
        dr.rectangle([lx,gh+16,lx+16,gh+32],fill=col); dr.text((lx+22,gh+15),txt,font=f2,fill=(210,210,220)); lx+=145
    tr=f"niches solved  {ns}/{L}"
    w=dr.textlength(tr,font=f1); dr.text((gw-w-14, gh+11), tr, font=f1, fill=(120,240,150))
    if scale!=1.0:
        canvas=canvas.resize((int(canvas.width*scale),int(canvas.height*scale)),Image.LANCZOS)
    return canvas

def main():
    inp,out=sys.argv[1],sys.argv[2]
    W,H,L,frames=load(inp)
    print(f"{len(frames)} frame(s), {L} niches {W}x{H}")
    if out.endswith(".png") or len(frames)==1:
        frame_image(*frames[-1],W,H,L).save(out); print("wrote",out); return
    # keep dense early (the bloom) and late (the green wave); thin the stable middle
    frames=[(ep,d) for ep,d in frames if ep<3200 or ep>=44000 or ep%6000==0]
    print("using", len(frames), "frames after subsampling")
    scale=0.5
    imgs=[frame_image(ep,d,W,H,L,scale) for ep,d in frames]
    # duration: hold first & last, slow through the green-spread window
    eps=[ep for ep,_ in frames]
    durs=[]
    for i,ep in enumerate(eps):
        if i==0: durs.append(1100)
        elif i==len(eps)-1: durs.append(2600)
        elif ep>=45000: durs.append(150)     # the curriculum wave — linger
        else: durs.append(80)
    # one shared palette (from the final frame — it has every colour family) applied
    # to all frames: no per-frame palette flicker, and far smaller than RGB GIF.
    pal = imgs[-1].quantize(colors=128, method=Image.FASTOCTREE)
    pimgs=[im.quantize(palette=pal, dither=Image.NONE) for im in imgs]
    pimgs[0].save(out,save_all=True,append_images=pimgs[1:],duration=durs,loop=0,
                  optimize=True,disposal=2)
    print("wrote",out,"frames",len(imgs),"size",imgs[0].size)

if __name__=="__main__": main()
