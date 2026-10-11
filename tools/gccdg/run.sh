#!/bin/bash
# run.sh [model] : build every sweep-PASS test with tools/build-example.sh and
# run them all under DOSBox (tools/run-dos-batch.sh, in chunks; a hang is
# attributed to the first program that produced no output and the rest of
# its chunk is re-run).  Verdicts in $OUT/results.txt: <id> <TAB> <verdict>
#   PASS ABORT FAIL EXIT-NONZERO   from the driver's GCCDG: line
#   NOVERDICT   ran but printed no GCCDG: line (crashed to DOS mid-test)
#   HANG        no output and the batch watchdog fired on it
#   BUILDFAIL   build-example.sh failed (link stage; the sweep passed)
# Then compares against $TOOLS/baseline-<model>.txt if present: any test that
# was PASS there and is not PASS now is a REGRESSION (exit 1).
# Flags: --save-baseline writes results.txt as the new baseline.
# Env: ONLY="id ..." restricts the set; JOBS (parallel builds, default 4);
#      CHUNK (programs per DOSBox boot, default 120).
set -u
SAVE=0
for a in "$@"; do case "$a" in --save-baseline) SAVE=1 ;; *) export MODEL="$a" ;; esac; done
. "$(dirname "$0")/common.sh"
JOBS="${JOBS:-4}"; CHUNK="${CHUNK:-120}"
[ -f "$OUT/sweep.txt" ] || { echo "run: no $OUT/sweep.txt -- run $TOOLS/sweep.sh $MODEL first" >&2; exit 1; }
mkdir -p "$OUT/run" "$OUT/build"
ids="${ONLY:-$(awk -F'\t' '$2=="PASS"{print $1}' "$OUT/sweep.txt")}"
: > "$OUT/results.txt"
verdict() { printf '%s\t%s\n' "$1" "$2" >> "$OUT/results.txt"; }

# ---- build (first one serial to warm the newlibc support cache, then parallel)
build_one() { # build_one <id>
	local b="$PFX$1"
	rm -f "$Q/build/examples/$b/$b.exe"
	"$Q/tools/build-example.sh" "--model=$MODEL" --softfloat "$WRAPPED/$b.c" > "$OUT/build/$b.log" 2>&1
}
export -f build_one; export Q MODEL WRAPPED OUT PFX
first=$(echo $ids | awk '{print $1}')
[ -n "$first" ] || { echo "run: nothing to run"; exit 0; }
echo "run: building $(echo $ids | wc -w | tr -d ' ') tests (model=$MODEL, jobs=$JOBS) ..."
build_one "$first"
echo $ids | tr ' ' '\n' | tail -n +2 | xargs -P "$JOBS" -I{} bash -c 'build_one "$@"' _ {}
pending=""
for id in $ids; do
	if [ -f "$Q/build/examples/$PFX$id/$PFX$id.exe" ]; then pending="$pending $id"; else verdict "$id" BUILDFAIL; fi
done

# ---- run in chunks, re-running the tail of a chunk after a hang
rm -f "$OUT"/run/*.txt
while [ -n "$pending" ]; do
	chunk=$(echo $pending | tr ' ' '\n' | head -n "$CHUNK"); rest=$(echo $pending | tr ' ' '\n' | tail -n +$((CHUNK + 1)))
	m="$OUT/manifest.tsv"; : > "$m"
	for id in $chunk; do printf '%s\t%s\n' "$Q/build/examples/$PFX$id/$PFX$id.exe" "$OUT/run/$id.txt" >> "$m"; done
	"$Q/tools/run-dos-batch.sh" "$m" > "$OUT/batch.log" 2>&1; rc=$?
	[ $rc -eq 77 ] && { echo "run: DOSBox not found"; exit 77; }
	retry=""; hung=0
	for id in $chunk; do
		f="$OUT/run/$id.txt"
		if [ $hung -eq 1 ]; then retry="$retry $id"; continue; fi
		if [ -f "$f" ]; then
			v=$(grep -o 'GCCDG:[A-Z-]*' "$f" | tail -1 | sed 's/GCCDG://'); verdict "$id" "${v:-NOVERDICT}"
		else
			verdict "$id" HANG; hung=1
		fi
	done
	pending="$retry $rest"; pending="${pending# }"
done

# ---- summary + baseline compare
echo "=== gccdg run (model=$MODEL): $(wc -l < "$OUT/results.txt" | tr -d ' ') tests ==="
cut -f2 "$OUT/results.txt" | sort | uniq -c | sort -rn
BASE="$TOOLS/baseline-$MODEL.txt"
if [ $SAVE -eq 1 ]; then sort "$OUT/results.txt" > "$BASE"; echo "baseline saved: $BASE"; exit 0; fi
if [ -f "$BASE" ]; then
	reg=$(join -t $'\t' <(awk -F'\t' '$2=="PASS"' "$BASE" | sort) <(sort "$OUT/results.txt") | awk -F'\t' '$3!="PASS"{print "  " $1 ": PASS -> " $3}')
	if [ -n "$reg" ]; then echo "REGRESSIONS vs $BASE:"; echo "$reg"; exit 1; fi
	new=$(join -t $'\t' <(awk -F'\t' '$2!="PASS"' "$BASE" | sort) <(sort "$OUT/results.txt") | awk -F'\t' '$3=="PASS"{print "  " $1 ": " $2 " -> PASS"}')
	[ -n "$new" ] && { echo "newly passing (update the baseline with --save-baseline):"; echo "$new"; }
	echo "no regressions vs $BASE"
fi
