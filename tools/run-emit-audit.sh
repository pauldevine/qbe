#!/bin/bash
# run-emit-audit.sh — §4y emit-bracket audit corpus driver.
#
# Rebuilds every gate probe under its gate model (tools/test-dos.sh entries),
# regenerates its asm with QBE_EMIT_CHK=1 markers, and runs
# tools/check_emit_brackets.py over the lot.  Also sweeps any prebuilt
# MicroPython per-TU .ssa in build/mp-link (compact).
#
# Usage: tools/run-emit-audit.sh [--probes-only|--mp-only]
set -u
cd "$(dirname "$0")/.."

OUT=build/chk-corpus
mkdir -p "$OUT"
MODE="${1:-all}"

gen() { # gen <ssa> <model> <tag>
	QBE_EMIT_CHK=1 ./qbe -t i8086 -m "$2" "$1" > "$OUT/$3.asm" 2>/dev/null \
		|| { echo "audit: qbe failed on $1 ($2)" >&2; rm -f "$OUT/$3.asm"; }
}

if [ "$MODE" != "--probes-only" ]; then
	for f in build/mp-link/*.ssa; do
		[ -f "$f" ] || continue
		gen "$f" compact "mp-$(basename "$f" .ssa)"
	done
fi

if [ "$MODE" != "--mp-only" ]; then
	# Pull "file:golden:model" entries out of the gate's RUNTIME_TESTS list.
	grep -oE '"minic/dos/examples/[a-z0-9_]+\.c:[^:]*:[a-z]+"' tools/test-dos.sh \
	| tr -d '"' \
	| while IFS=: read -r src _golden model; do
		base=$(basename "$src" .c)
		tag="$base-$model"
		[ -f "$OUT/$tag.asm" ] && continue
		sf=""
		case "$base" in
		softfloat_probe|float_literal_probe|float_fardata_probe|softlibm_probe|\
		softtrig_probe|double_float_probe|float_arg_coerce_probe|float_cmp_cx_probe)
			sf="--softfloat" ;;
		esac
		fs=0
		case "$base" in
		fardata_probe|farglobal_probe|farstruct_ptr_probe|slotarray_probe|\
		gc_bigheap_probe|gc_churn_probe)
			fs=1 ;;
		esac
		ss=""
		case "$base" in split_stack_probe) ss="--split-stack" ;; esac
		if ! QBE_FAR_STATIC_DATA="$fs" tools/build-example.sh \
				--model="$model" $sf $ss "$src" \
				> "$OUT/$tag.build.log" 2>&1; then
			echo "audit: build failed: $base ($model)" >&2
			continue
		fi
		gen "build/examples/$base/$base.ssa" "$model" "$tag"
	done
fi

echo "=== emit-bracket audit over $(ls "$OUT"/*.asm 2>/dev/null | wc -l | tr -d ' ') files ==="
python3 tools/check_emit_brackets.py "$OUT"/*.asm
