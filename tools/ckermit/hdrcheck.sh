#!/bin/bash
# hdrcheck.sh : do OUR headers (minic/dos/ckermit/include) make C-Kermit
# compile the same code as Open Watcom's headers do?  Two checks, both against
# wcc in the ia16-ubuntu-2 container (see pp.sh):
#
#  1. macros -- every identifier C-Kermit tests in #if/#ifdef/#ifndef/#elif,
#     evaluated after ckvictor.h + all 26 system headers the build includes:
#     defined-ness under Watcom vs ours, then the expansions of those both
#     define that appear in #if/#elif expressions.  Printed as a diff
#     (< Watcom only, > ours only).  Known, accepted Watcom-only entries are
#     listed in ACCEPTED below; anything else is a finding.
#  2. statements -- each module preprocessed both ways with line markers;
#     the C-Kermit-source statements (split on ; { }) are compared as
#     multisets per module.  Macro-VALUE differences (O_* flags, errno codes,
#     ctype tables vs calls, FILE macros vs functions, MK_FP forms) show up as
#     paired W-only/O-only statements; a code path present in only one build
#     unbalances the W/O counts.  Balanced counts for every module = pass.
#
# Outputs under build/ckermit/hdrcheck/.  Exit 1 on any finding.
set -u
. "$(dirname "$0")/common.sh"
MODEL=large
. "$TOOLS/cppflags.sh"
H="$Q/build/ckermit/hdrcheck"; mkdir -p "$H/w" "$H/o"
CT=ia16-ubuntu-2
if ! container list 2>/dev/null | grep -q "^$CT .*running"; then
	container system start >/dev/null 2>&1 || true
	container start "$CT" >/dev/null || { echo "hdrcheck: needs the $CT container" >&2; exit 77; }
fi
rel() { echo "/mnt/projects/${1#$HOME/projects/}"; }
wcc() { # wcc <flag -p|-pl> <src-in-CK> <out>
	container exec "$CT" sh -c "cd $(rel "$CK") && WATCOM=/opt/open-watcom-v2/rel && PATH=\$WATCOM/arml64:\$PATH && \
	  wcc $1 -ml -0 -os -zq -zc -bt=dos -fr=/dev/null -i=victorow -i=victor -i=\$WATCOM/h $4 -fo=$(rel "$3") $2"
}
# Watcom-only macros already judged harmless (the §3 survey, NEXT_SESSION item 3):
ACCEPTED='__WATCOMC__ _INTEGRAL_MAX_BITS _INTPTR_T_DEFINED _MAX_FNAME far feof getchar interrupt isprint LLONG_MAX major min putc putchar stdin ULLONG_MAX'
rc=0

# ---- 1. macros
(cd "$CK" && cat ck*.c ck*.h ckcpro.w | grep -E '^\s*#\s*(if|ifdef|ifndef|elif)' \
	| grep -oE '\b[A-Za-z_][A-Za-z0-9_]*\b' | sort -u \
	| grep -vxE 'if|ifdef|ifndef|elif|else|endif|defined|include|define|undef|error|pragma|line|warning') > "$H/ids.txt"
(cd "$CK" && cat ck*.c ck*.h | grep -E '^\s*#\s*(if|elif)\b' | grep -oE '\b[A-Za-z_][A-Za-z0-9_]*\b' | sort -u) > "$H/ifids.txt"
HDRS="ctype.h direct.h dirent.h dos.h errno.h fcntl.h i86.h io.h limits.h setjmp.h signal.h stdarg.h stdio.h stdlib.h string.h sys/stat.h sys/types.h time.h unistd.h utime.h sys/ioctl.h sys/termios.h termios.h pwd.h sys/time.h sys/utsname.h"
probe() { # probe <file> <line-printf-format>
	{ echo '#include "ckvictor.h"'; for h in $HDRS; do echo "#include <$h>"; done
	  while read id; do printf "$2" "$id" "$id"; done; } > "$1"
}
run_both() { # run_both <name> : $H/<name>.c -> $H/<name>.w.i, $H/<name>.o.i
	cp "$H/$1.c" "$CK/zz_hdrcheck_$1.c"
	wcc -p "zz_hdrcheck_$1.c" "$H/$1.w.i" ""
	rm -f "$CK/zz_hdrcheck_$1.c"
	clang "${CPPFLAGS[@]}" -P "$H/$1.c" -o "$H/$1.o.i" 2>/dev/null
}
probe "$H/def.c" '#ifdef %s\nHAVE_%s\n#endif\n' < "$H/ids.txt"
run_both def
grep -ho 'HAVE_[A-Za-z0-9_]*' "$H/def.w.i" | sed 's/^HAVE_//' | sort > "$H/def.w.txt"
grep -ho 'HAVE_[A-Za-z0-9_]*' "$H/def.o.i" | sed 's/^HAVE_//' | sort > "$H/def.o.txt"
echo "== macro definedness (< Watcom only, > ours only)"
diff "$H/def.w.txt" "$H/def.o.txt" | grep '^[<>]' | while read side id; do
	case " $ACCEPTED " in *" $id "*) [ "$side" = "<" ] && continue ;; esac
	echo "$side $id"
done | tee "$H/def.findings"
[ -s "$H/def.findings" ] && rc=1
comm -12 "$H/def.w.txt" "$H/def.o.txt" | comm -12 - "$H/ifids.txt" > "$H/val.ids"
probe "$H/val.c" 'VAL_%s: %s\n' < "$H/val.ids"
run_both val
echo "== #if values of shared macros ($(wc -l < "$H/val.ids" | tr -d ' ') compared)"
diff <(grep '^ *VAL_' "$H/val.w.i" | tr -d ' \t' | sort) <(grep '^ *VAL_' "$H/val.o.i" | tr -d ' \t' | sort) || rc=1

# ---- 2. statements
echo "== statement balance per module"
for m in $MODULES; do
	wcc -pl "$m.c" "$H/w/$m.i" "-fi=ckvictor.h"
	clang "${CPPFLAGS[@]}" -include "$CK/ckvictor.h" "$CK/$m.c" -o "$H/o/$m.i" 2>/dev/null
done
python3 - "$H" $MODULES <<'EOF' || rc=1
import re, sys, os, collections
H = sys.argv[1]
def load(path, watcom):
    out = collections.defaultdict(list); cur = None
    mark = re.compile(r'#line \d+ "([^"]*)"' if watcom else r'# \d+ "([^"]*)"')
    for raw in open(path, errors='replace'):
        m = mark.match(raw)
        if m: cur = os.path.basename(m.group(1)); continue
        s = raw.strip()
        if cur and cur.startswith('ck') and s and not s.startswith('#pragma'):
            out[cur].append(s)
    return out
def stmts(lines):
    t = re.sub(r'\s+', '', ' '.join(lines))
    return collections.Counter(p for p in re.split(r'[;{}]', t) if p)
bad = 0
with open(f'{H}/stmts.txt', 'w') as rep:
    for mod in sys.argv[2:]:
        w = load(f'{H}/w/{mod}.i', True); o = load(f'{H}/o/{mod}.i', False)
        nw = no = 0
        for f in sorted(set(w) | set(o)):
            a, b = stmts(w.get(f, [])), stmts(o.get(f, []))
            for k, v in (a - b).items(): nw += v; rep.write(f'{mod} {f} W-only x{v}: {k[:240]}\n')
            for k, v in (b - a).items(): no += v; rep.write(f'{mod} {f} O-only x{v}: {k[:240]}\n')
        flag = '' if nw == no else '   <-- UNBALANCED'
        if flag: bad = 1
        print(f'{mod:9s} W-only={nw:3d} O-only={no:3d}{flag}')
print(f'(details: {H}/stmts.txt)')
sys.exit(bad)
EOF
[ $rc = 0 ] && echo "hdrcheck: PASS" || echo "hdrcheck: FINDINGS"
exit $rc
