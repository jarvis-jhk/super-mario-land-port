"""Checks the widescreen view against the game itself.

The frontend saves a picture series (--widescreen 32:9 --screenshot DIR/f
--screenshot-every N). For pairs of pictures GAP frames apart, the right
side of the earlier one (level columns the game has not drawn yet, decoded
from the level data) must show what the game's own 160x144 picture shows
once it has scrolled there. Enemies, animated tiles and Mario differ, so the
pixel match is measured over the whole TAS and compared to a floor.

    widescreen_check.py DIR STEP GAP MIN_MATCH
"""
import sys, os, zlib, struct
def gray(p):
    d=open(p,'rb').read(); i=8; idat=b''
    while i<len(d):
        n=struct.unpack('>I',d[i:i+4])[0]; t=d[i+4:i+8]; c=d[i+8:i+8+n]; i+=12+n
        if t==b'IHDR': w,h,bd,ct=struct.unpack('>IIBB',c[:10])
        elif t==b'IDAT': idat+=c
    raw=zlib.decompress(idat); bpp={0:1,2:3}[ct]; st=w*bpp; rows=[]
    for y in range(h):
        f=raw[y*(st+1)]; assert f==0, f
        line=raw[y*(st+1)+1:(y+1)*(st+1)]
        rows.append(line[::bpp])
    return w,h,rows
d, step, gap, floor = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), float(sys.argv[4])
frames=sorted(int(f[2:8]) for f in os.listdir(d) if f.startswith('f-'))
tot=good=pairs=checked=0; worst=[]
for t in frames[::step]:
    a=os.path.join(d,'f-%06d.png'%t); b=os.path.join(d,'f-%06d.png'%(t+gap))
    if not os.path.exists(b): continue
    w,h,A=gray(a); _,_,B=gray(b); ext=(w-160)//2
    # sides blank (not a level) -> skip
    if all(A[y][x]==A[20][0] for y in range(16,144,8) for x in range(w-ext,w,8)): continue
    pairs+=1
    best=None
    for s in range(0,ext):
        m=n=0
        for y in range(16,144,2):
            for x in range(0,160,2):
                xa=ext+x+s
                if xa<w: n+=1; m+= A[y][xa]==B[y][ext+x]
        if n and (best is None or m/n>best[0]): best=(m/n,s)
    r,s=best
    if s<24: continue
    m=n=0
    for y in range(16,144):
        for x in range(160-s,160):
            xa=ext+x+s
            if ext+160<=xa<w: n+=1; m+= A[y][xa]==B[y][ext+x]
    checked+=1; tot+=n; good+=m
    worst.append((m/n if n else 1,t,s))
worst.sort()
print('pairs %d checked %d side-pixel match %.4f'%(pairs,checked,good/max(tot,1)))
print('worst', worst[:8])
if checked < 50 or good / max(tot, 1) < floor:
    sys.exit('widescreen_check: FAILED (%d pairs checked, floor %.3f)' % (checked, floor))
print('widescreen_check: passed')
