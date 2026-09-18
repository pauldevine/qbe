#!/usr/bin/env python3
"""omfsize.py <watcom-obj-dir> <module...>: per-module code/data bytes of our
OMF objects ($OUTD, default out-large) vs Watcom's.  Run from the work dir."""
import sys
def idx(b, i):
    v = b[i]
    return ((v & 0x7f) << 8 | b[i+1], i+2) if v & 0x80 else (v, i+1)
def sizes(path):
    b = open(path, 'rb').read(); i = 0; lnames = ['']; segs = []
    while i < len(b):
        t = b[i]; ln = b[i+1] | b[i+2] << 8; body = b[i+3:i+3+ln-1]
        if t in (0x96,):
            j = 0
            while j < len(body):
                n = body[j]; lnames.append(body[j+1:j+1+n].decode('latin1')); j += 1 + n
        elif t in (0x98, 0x99):
            a = body[0]; j = 1
            if (a >> 5) == 0: j += 3
            if t == 0x98: L = body[j] | body[j+1] << 8; j += 2
            else: L = body[j] | body[j+1] << 8 | body[j+2] << 16 | body[j+3] << 24; j += 4
            if a & 2: L = 0x10000
            ni, j = idx(body, j); ci, j = idx(body, j)
            segs.append((lnames[ni], lnames[ci], L))
        i = i + 3 + ln
    code = sum(L for n, c, L in segs if 'CODE' in c.upper())
    data = sum(L for n, c, L in segs if 'CODE' not in c.upper() and 'STACK' not in c.upper())
    return code, data
import os
OUTD = os.environ.get("OUTD", "out-large")
tc = [0,0,0,0]
for m in sys.argv[2:]:
    a = sizes(f'{OUTD}/{m}.obj'); w = sizes(f'{sys.argv[1]}/{m}.obj')
    tc = [tc[0]+a[0], tc[1]+w[0], tc[2]+a[1], tc[3]+w[1]]
    print(f'{m:10s} code ours={a[0]:7d} watcom={w[0]:7d} ({a[0]/max(w[0],1):4.2f}x)   data ours={a[1]:6d} watcom={w[1]:6d}')
print(f'{"TOTAL":10s} code ours={tc[0]:7d} watcom={tc[1]:7d} ({tc[0]/tc[1]:4.2f}x)   data ours={tc[2]:6d} watcom={tc[3]:6d}')
