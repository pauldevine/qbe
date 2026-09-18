#!/usr/bin/env python3
"""locate.py <module>: bisect which statement line inside the failing function
triggers minic's error (for errors minic reports at end-of-function).
Paths follow common.sh: $CKW (default build/ckermit-triage), $MODEL, $OUTSUFFIX."""
import os, subprocess, sys, re
Q = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
MINIC = os.path.join(Q, 'minic', 'minic')
MODEL = os.environ.get('MODEL', 'large')
W = os.environ.get('CKW', os.path.join(Q, 'build', 'ckermit-triage'))
f = os.path.join(W, f'out-{MODEL}{os.environ.get("OUTSUFFIX", "")}', f'{sys.argv[1]}.c')
EXTRA = os.environ.get('MINICFLAGS', '').split()
lines = open(f).read().split('\n')
def run(ls):
    p = subprocess.run([MINIC, '-m', MODEL] + EXTRA, input='\n'.join(ls).encode(), capture_output=True)
    return p.returncode, p.stderr.decode()
rc, err = run(lines)
m = re.search(r'error:(\d+): (.*)', err)
if rc == 0 or not m: print('no error'); sys.exit()
L, msg = int(m.group(1)) - 1, m.group(2).strip()
start = max(i for i in range(0, L + 1) if lines[i].startswith('{'))
end = L
def isdecl(x):
    x = x.strip()
    if x.startswith('return'): return False
    return bool(re.match(r'(const\s+|static\s+|extern\s+|unsigned\s+|struct\s+)*[A-Za-z_]\w*\s+[\s*]*\w+\s*(\[[^]]*\])?\s*[;=,]', x))
cand = [i for i in range(start + 1, end) if lines[i].rstrip().endswith(';') and '{' not in lines[i] and '}' not in lines[i] and not isdecl(lines[i])]
def fails(removed):
    ls = [('' if i in removed else x) for i, x in enumerate(lines)]
    rc, e = run(ls)
    return rc != 0 and msg in e
if fails(set(cand)): print('removing all candidates does not clear it'); sys.exit()
lo, hi = 0, len(cand)          # fails(cand[:lo]) true, fails(cand[:hi]) false
while hi - lo > 1:
    mid = (lo + hi) // 2
    if fails(set(cand[:mid])): lo = mid
    else: hi = mid
print(msg); i = cand[hi-1]; print(f'{i+1}: {lines[i]}')
