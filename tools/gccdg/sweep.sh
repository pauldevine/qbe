#!/bin/bash
# sweep.sh [model] : compile-only triage of the wrapped tests through the
# EXACT build-example.sh stage-1..4 pipeline (cpp flags + type-normalising
# sed copied verbatim, minic, qbe -t i8086, asm_to_omf, nasm).  One line per
# test in $OUT/sweep.txt: <id> <TAB> <stage> <TAB> <first error line>, where
# stage is PASS or the first failing stage: cpp minic qbe omf nasm.  A minic
# or qbe CRASH (signal) is reported as stage minic-crash / qbe-crash.
# Env: ONLY="id id ..." restricts the set.
set -u
[ $# -ge 1 ] && export MODEL="$1"
. "$(dirname "$0")/common.sh"
[ -f "$W/tests.txt" ] || { echo "sweep: no $W/tests.txt -- run $TOOLS/wrap.sh first" >&2; exit 1; }
mkdir -p "$OUT"
NORMALIZE_TYPES='
s/\bunsigned short int\b/unsigned short/g
s/\bunsigned long int\b/unsigned long/g
s/\bsigned short int\b/short/g
s/\bsigned long int\b/long/g
s/\blong long int\b/long long/g
s/\blong int\b/long/g
s/\bshort int\b/short/g
s/\bsigned char\b/char/g
s/\bsigned short\b/short/g
s/\bsigned long long\b/long long/g
s/\bsigned long\b/long/g
s/\bsigned int\b/int/g
s/\bsigned\b//g
'
: > "$OUT/sweep.txt"
ids="${ONLY:-$(cut -f1 "$W/tests.txt")}"
for id in $ids; do
	b="$PFX$id"; src="$WRAPPED/$b.c"; err="$OUT/$b.err"; : > "$err"
	stage() { printf '%s\t%s\t%s\n' "$id" "$1" "$(head -1 "$err" | sed "s|$OUT/||; s|$WRAPPED/||" | cut -c1-160)" >> "$OUT/sweep.txt"; }
	if ! cpp -P -nostdinc -isysroot/var/empty -DDOS -D__TURBOC__ -Dmain=newlibc_test_main \
			"-I$Q/minic/include" "-I$WRAPPED" "$src" 2>"$err" | tr -d '\r\032' | sed "$NORMALIZE_TYPES" > "$OUT/$b.pp.c"; then
		stage cpp; continue; fi
	timeout 20 "$Q/minic/minic" -m "$MODEL" < "$OUT/$b.pp.c" > "$OUT/$b.ssa" 2>"$err"; rc=$?
	if [ $rc -ne 0 ]; then [ $rc -ge 124 ] && [ ! -s "$err" ] && echo "signal/timeout rc=$rc" > "$err"; [ $rc -ge 124 ] && stage minic-crash || stage minic; continue; fi
	timeout 20 "$Q/qbe" -t i8086 -m "$MODEL" "$OUT/$b.ssa" > "$OUT/$b.asm" 2>"$err"; rc=$?
	if [ $rc -ne 0 ]; then [ $rc -ge 124 ] && [ ! -s "$err" ] && echo "signal/timeout rc=$rc" > "$err"; [ $rc -ge 124 ] && stage qbe-crash || stage qbe; continue; fi
	if ! "$Q/tools/asm_to_omf.py" "--model=$MODEL" "$b" "$OUT/$b.asm" "$OUT/$b.omf.asm" 2>"$err"; then stage omf; continue; fi
	if ! nasm -w-label-redef-late -f obj "$OUT/$b.omf.asm" -o "$OUT/$b.obj" 2>"$err"; then stage nasm; continue; fi
	stage PASS
done
echo "=== gccdg sweep (model=$MODEL): $(wc -l < "$OUT/sweep.txt" | tr -d ' ') tests ==="
cut -f2 "$OUT/sweep.txt" | sort | uniq -c | sort -rn
echo "--- minic error classes:"
awk -F'\t' '$2=="minic"{print $3}' "$OUT/sweep.txt" | sed -E 's/[0-9]+/N/g' | sort | uniq -c | sort -rn | head -12
echo "--- qbe rejects / crashes:"
awk -F'\t' '$2 ~ /^(qbe|minic-crash|qbe-crash|omf|nasm)$/ {print "  " $1 ": " $3}' "$OUT/sweep.txt"
