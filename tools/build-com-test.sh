#!/bin/bash
# Build a single C source through the i8086 small-model (.COM) pipeline.
#
# Pipeline:
#   1. cpp  -> preprocessed C
#   2. minic -m small -> SSA
#   3. qbe   -t i8086 -m small -> i8086 asm
#   4. sed/awk/perl normalisation -> NASM-syntax .asm
#   5. crt0.asm + pruned libstub.asm + per-TU asm -> concat -> nasm -f bin
#
# Output: build/com-test/<base>/<base>.com
#
# Fails (exit 1) if the resulting .COM exceeds 65 536 bytes -- the .COM
# single-segment ceiling, which is the entire point of the tiny model.
#
# Usage: tools/build-com-test.sh path/to/source.c [size_limit]

set -eu

MODEL="small"
SRC=""
LIMIT="65536"
for arg in "$@"; do
	case "$arg" in
		--model=*) MODEL="${arg#--model=}" ;;
		-h|--help)
			echo "usage: $0 [--model=<tiny|small>] <source.c> [size_limit]" >&2
			exit 0 ;;
		--*) echo "$0: unknown option: $arg" >&2; exit 2 ;;
		*)
			if [ -z "$SRC" ]; then SRC="$arg"
			else LIMIT="$arg"
			fi ;;
	esac
done

if [ -z "$SRC" ]; then
	echo "usage: $0 [--model=<m>] <source.c> [size_limit]" >&2
	exit 2
fi

QBE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
if [ ! -f "$SRC" ]; then SRC="$QBE_DIR/$SRC"; fi
if [ ! -f "$SRC" ]; then
	echo "$0: cannot find source" >&2
	exit 2
fi

base="$(basename "$SRC" .c)"
OUT_DIR="$QBE_DIR/build/com-test/$base"
MINIC="$QBE_DIR/minic/minic"
INC_DIR="$QBE_DIR/minic/include"
QBE="$QBE_DIR/qbe"
DOS_DIR="$QBE_DIR/minic/dos"

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

mkdir -p "$OUT_DIR"
ERR="$OUT_DIR/build.err"
: > "$ERR"

# Stage 1 + 2: cpp -> minic -m small
pp="$OUT_DIR/$base.pp.c"
cpp -P -nostdinc -isysroot/var/empty -DDOS -D__TURBOC__ \
	"-I$INC_DIR" "-I$(dirname "$SRC")" \
	"$SRC" 2>"$ERR" | tr -d '\r\032' | sed "$NORMALIZE_TYPES" > "$pp"
"$MINIC" -m "$MODEL" < "$pp" > "$OUT_DIR/$base.ssa" 2>>"$ERR"

# Stage 3: qbe -t i8086 -m <MODEL>
"$QBE" -t i8086 -m "$MODEL" "$OUT_DIR/$base.ssa" > "$OUT_DIR/$base.asm" 2>>"$ERR"

# Stage 4: normalise to NASM (same pipeline as build-example.sh /
# build-stevie.sh; .COM-side prefixes labels so multi-TU concats don't
# collide -- harmless for the single-TU case).
prefix="${base}_"
asm_clean="$OUT_DIR/$base.nasm.asm"
grep -v -E '^\.(text|data|bss|balign|section|globl|type|size|local|file|ident|string|p2align|model|code)' \
		"$OUT_DIR/$base.asm" \
	| sed -e 's/; TODO: 32-bit op [0-9]*/; XXX 32-bit op stub - codegen incomplete/' \
	      -e 's/^[[:space:]]*\.ascii "\(.*\)"$/.nasm_str \1/' \
	| awk '
		/^\.nasm_str / {
			s = substr($0, length(".nasm_str ")+1)
			out = ""
			for (n = 1; n <= length(s); n++) {
				ch = substr(s, n, 1)
				if (ch == "`") out = out "\\`"
				else out = out ch
			}
			print "db `" out "`"
			next
		}
		{ print }
	' \
	| perl -pe '
		BEGIN { $p = "'"$prefix"'"; }
		s/^(l\d+(?:_l\d+)?):/${p}$1:/;
		s/^(\s*j[a-z]+\s+)(l\d+(?:_l\d+)?)\b/$1${p}$2/;
		s/^(\s*jnz\s+[^,]+,\s*)(l\d+(?:_l\d+)?)(\s*,\s*)(l\d+(?:_l\d+)?)\b/$1${p}$2$3${p}$4/;
		s/^_?glo(\d+):/${p}glo$1:/;
		s/\b_?glo(\d+)\b/${p}glo$1/g;
	' \
	| sed -e 's/word ptr \([a-z][a-z]:\)\[/word \1[/g' \
	      -e 's/byte ptr \([a-z][a-z]:\)\[/byte \1[/g' \
	      -e 's/dword ptr \([a-z][a-z]:\)\[/dword \1[/g' \
	      -e 's/word ptr \[/word [/g' \
	      -e 's/byte ptr \[/byte [/g' \
	      -e 's/dword ptr \[/dword [/g' \
	      -e 's|/\* \(.*\) \*/|; \1|' \
	      -e 's/^\([A-Za-z_][A-Za-z0-9_]*\) proc near$/\1:/' \
	      -e 's/^\([A-Za-z_][A-Za-z0-9_]*\) proc far$/\1:/' \
	      -e 's/^\([A-Za-z_][A-Za-z0-9_]*\) endp$/; \1 endp/' \
	      -e '/^:$/d' \
	      -e 's/^[[:space:]]*\.byte \(.*\)$/db \1/' \
	      -e 's/^[[:space:]]*\.short \(.*\)$/dw \1/' \
	      -e 's/^[[:space:]]*\.long \(.*\)$/dd \1/' \
	      -e 's/^[[:space:]]*\.int \(.*\)$/dw \1/' \
	      -e 's/^[[:space:]]*\.word \(.*\)$/dw \1/' \
	      -e 's/^[[:space:]]*\.quad \(.*\)$/dq \1/' \
	      -e 's/^[[:space:]]*\.zero \(.*\)$/times \1 db 0/' \
	      -e 's/^[[:space:]]*\.fill \([0-9]*\),1,0$/times \1 db 0/' \
	      -e 's/^[[:space:]]*\.fill \([0-9]*\),2,0$/times \1 dw 0/' \
	      -e 's/^[[:space:]]*\.fill \([0-9]*\),4,0$/times \1 dd 0/' \
	      -e 's/^[[:space:]]*\.fill \([0-9]*\)$/times \1 db 0/' \
	      -e '/^[[:space:]]*\.section/d' \
	      -e '/^[[:space:]]*\.balign/d' \
	      -e '/^[[:space:]]*\.local/d' \
	      -e '/^[[:space:]]*\.globl/d' \
	      -e '/^[[:space:]]*\.type/d' \
	      -e '/^[[:space:]]*\.size/d' \
	      -e '/^[[:space:]]*\.text/d' \
	      -e '/^[[:space:]]*\.data/d' \
	      -e '/^[[:space:]]*\.bss/d' \
	      -e '/^[[:space:]]*$/d' \
	| perl -pe '
		s/^(\s*test\s+)(es|ds|cs|ss),\s*\g{2}\b/$1ax, ax ; XXX was test $2,$2/g;
	' \
	> "$asm_clean"

# Stage 5: concat crt0 + pruned libstub + per-TU asm; nasm -f bin
echo "=== Linking (.COM) ==="
LINK_ASM="$OUT_DIR/$base.full.asm"
strip_runtime() {
	grep -v -E '^(BITS|CPU|section|global|extern)\b' "$1"
}

PRUNED_LIBSTUB="$OUT_DIR/libstub.pruned.asm"
"$QBE_DIR/tools/libstub_prune.py" "$DOS_DIR/libstub.asm" \
	"$PRUNED_LIBSTUB" "$DOS_DIR/crt0.asm" "$asm_clean" 2>>"$ERR" || {
	echo "  FAIL libstub-prune"; cat "$ERR"; exit 1; }

{
	echo "BITS 16"
	echo "CPU 8086"
	echo "ORG 0x100"
	echo
	echo "section .text"
	echo "; ===== crt0 ====="
	strip_runtime "$DOS_DIR/crt0.asm"
	echo
	echo "; ===== libstub (pruned) ====="
	strip_runtime "$PRUNED_LIBSTUB"
	echo
	echo "; ===== $base ====="
	cat "$asm_clean"
	echo
	echo "; ===== heap marker ====="
	echo "_heap_end_of_image:"
} > "$LINK_ASM"

if ! nasm -w-label-redef-late -w-pp-open-string -f bin "$LINK_ASM" \
		-o "$OUT_DIR/$base.com" 2>"$ERR"; then
	echo "  FAIL nasm: $(head -3 "$ERR")"
	exit 1
fi

SIZE=$(wc -c <"$OUT_DIR/$base.com")
echo "  OK: $OUT_DIR/$base.com ($SIZE bytes, limit $LIMIT)"

if [ "$SIZE" -gt "$LIMIT" ]; then
	echo "  FAIL: .COM exceeds $LIMIT-byte tiny-model ceiling" >&2
	exit 1
fi
