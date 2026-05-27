#!/bin/bash
# Build a single DOS example .EXE from minic/dos/examples/ (or any path).
# Modeled on tools/build-int86x-probe.sh, parameterized by source file.
#
# Usage: tools/build-example.sh path/to/source.c
# Output: build/examples/<name>/<name>.exe

set -eu

MODEL="medium"
SRC=""
for arg in "$@"; do
	case "$arg" in
		--model=*) MODEL="${arg#--model=}" ;;
		-h|--help)
			echo "usage: $0 [--model=<tiny|small|medium|compact|large|huge>] <source.c>" >&2
			exit 0 ;;
		--*) echo "$0: unknown option: $arg" >&2; exit 2 ;;
		*)
			if [ -z "$SRC" ]; then SRC="$arg"
			else echo "$0: extra argument: $arg" >&2; exit 2
			fi ;;
	esac
done

if [ -z "$SRC" ]; then
	echo "usage: $0 [--model=<m>] <source.c>" >&2
	exit 2
fi

QBE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
if [ ! -f "$SRC" ]; then
	SRC="$QBE_DIR/$SRC"
fi
if [ ! -f "$SRC" ]; then
	echo "$0: cannot find source file" >&2
	exit 2
fi

base="$(basename "$SRC" .c)"
OUT_DIR="$QBE_DIR/build/examples/$base"
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

# Stage 1: C → preprocessed → SSA
pp="$OUT_DIR/$base.pp.c"
cpp -P -nostdinc -isysroot/var/empty -DDOS -D__TURBOC__ \
	"-I$INC_DIR" "-I$(dirname "$SRC")" \
	"$SRC" 2>"$ERR" | tr -d '\r\032' | sed "$NORMALIZE_TYPES" > "$pp"
"$MINIC" -m "$MODEL" < "$pp" > "$OUT_DIR/$base.ssa" 2>>"$ERR"

# Stage 2: SSA → ASM
"$QBE" -t i8086 -m "$MODEL" "$OUT_DIR/$base.ssa" > "$OUT_DIR/$base.asm" 2>>"$ERR"

# Stage 3: ASM normalize (same sed/awk/perl pipeline as build-int86x-probe.sh).
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

# Stage 4: OMF wrap and assemble
"$QBE_DIR/tools/asm_to_omf.py" "$base" \
	"$OUT_DIR/$base.asm" "$OUT_DIR/$base.omf.asm" 2>>"$ERR"
nasm -w-label-redef-late -f obj "$OUT_DIR/$base.omf.asm" \
	-o "$OUT_DIR/$base.obj" 2>>"$ERR"

# Stage 5: crt0_exe.obj and libstub_exe.obj
# Far-data models need crt0_exe to build argv as 4-byte far ptrs to
# match main()'s `char *argv[]` parameter ABI.
CRT0_FLAGS=""
case "$MODEL" in
	compact|large|huge) CRT0_FLAGS="-DFAR_DATA=1" ;;
esac
nasm $CRT0_FLAGS -f obj "$DOS_DIR/crt0_exe.asm" -o "$OUT_DIR/crt0_exe.obj" 2>>"$ERR"
"$QBE_DIR/tools/libstub_to_exe.py" "--model=$MODEL" \
	"$DOS_DIR/libstub.asm" "$OUT_DIR/libstub_exe.asm" 2>>"$ERR"
nasm -f obj "$OUT_DIR/libstub_exe.asm" -o "$OUT_DIR/libstub_exe.obj" 2>>"$ERR"

# Stage 6: Link
"$QBE_DIR/tools/omf_link.py" \
	-o "$OUT_DIR/$base.exe" \
	--map "$OUT_DIR/$base.map" \
	--entry _start \
	--stack-size 8192 \
	"$OUT_DIR/crt0_exe.obj" \
	"$OUT_DIR/$base.obj" \
	"$OUT_DIR/libstub_exe.obj" 2>>"$ERR"

echo "  OK: $OUT_DIR/$base.exe ($(wc -c <"$OUT_DIR/$base.exe") bytes)"
