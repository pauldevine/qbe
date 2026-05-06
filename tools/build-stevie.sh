#!/bin/bash
# Build script for stevie-dos: minic → QBE i8086 backend → NASM → DOS .COM/.EXE
#
# Stages:
#   1. C   → SSA   via minic_cpp_v2 (preprocessor + minic)
#   2. SSA → ASM   via qbe -t i8086
#   3. ASM → OBJ   via nasm -f obj
#   4. Link        crt0 + sources + doslib → stevie.com (or .exe)
#
# Usage: tools/build-stevie.sh [--keep-going]
#
# With --keep-going, continues past per-file failures and reports the
# final list at the end.  Without it, stops on the first failure.

set -u
KEEP_GOING=0
[ "${1-}" = "--keep-going" ] && KEEP_GOING=1

QBE_DIR="$(cd "$(dirname "$0")/.." && pwd)"
SRC_DIR="$QBE_DIR/stevie-dos"
OUT_DIR="$QBE_DIR/build/stevie-dos"
MINIC_CPP="$QBE_DIR/minic/minic_cpp_v2"
QBE="$QBE_DIR/qbe"
DOS_DIR="$QBE_DIR/minic/dos"

mkdir -p "$OUT_DIR"

# Files compiled in order. tos.c is Atari-only; skip.
SOURCES=(
	alloc.c
	cmdline.c
	dos.c
	edit.c
	fileio.c
	help.c
	hexchars.c
	linefunc.c
	main.c
	mark.c
	misccmds.c
	normal.c
	param.c
	ptrfunc.c
	regexp.c
	regsub.c
	screen.c
	search.c
)

stage_pass=()
stage_fail_minic=()
stage_fail_qbe=()

for src in "${SOURCES[@]}"; do
	base="${src%.c}"
	ssa="$OUT_DIR/$base.ssa"
	asm="$OUT_DIR/$base.asm"
	obj="$OUT_DIR/$base.obj"
	err="$OUT_DIR/$base.err"

	# Stage 1: C → SSA
	if ! "$MINIC_CPP" "$SRC_DIR/$src" "$ssa" 2>"$err"; then
		stage_fail_minic+=("$src")
		[ $KEEP_GOING -eq 0 ] && { echo "FAIL minic: $src"; cat "$err"; exit 1; }
		continue
	fi

	# Stage 2: SSA → ASM
	if ! "$QBE" -t i8086 "$ssa" >"$asm" 2>"$err"; then
		stage_fail_qbe+=("$src: $(head -1 "$err")")
		[ $KEEP_GOING -eq 0 ] && { echo "FAIL qbe: $src"; cat "$err"; exit 1; }
		continue
	fi

	# Stage 3: convert QBE-emitted MASM/GNU-as mix to NASM syntax.
	# QBE i8086 currently outputs a hybrid: GNU-as section directives
	# (.text, .balign, .globl), MASM module headers (.model small, .code),
	# MASM proc syntax (`name proc near` / `endp`), and MASM operand
	# qualifier `word ptr [...]` / `byte ptr [...]`.  Convert all to NASM.
	asm_clean="$OUT_DIR/$base.nasm.asm"
	# Prefix local labels (l0, l1, ..., l_NNN, glo1, glo2, ...) with the
	# source basename to avoid conflicts when concatenating multiple
	# translation units into a single NASM .COM build.
	#
	# QBE i8086 emits local labels as `lN:` and globals (string literals
	# etc.) as `_gloN:`.  Function names and `_dos_*` runtime symbols are
	# kept unprefixed so cross-TU calls still resolve.
	prefix="${base}_"
	grep -v -E '^\.(text|data|bss|balign|section|globl|type|size|local|file|ident|string|p2align|model|code)' "$asm" \
		| sed -e 's/; TODO: 32-bit op [0-9]*/; XXX 32-bit op stub - codegen incomplete/' \
		      -e 's/^[[:space:]]*\.ascii "\(.*\)"$/.nasm_str \1/' \
		| awk '
			# Re-emit `.nasm_str <text>` lines as NASM backtick strings,
			# escaping any literal backticks in the content.
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
			# Label definitions: lN: or lN_lM: at start of line
			s/^(l\d+(?:_l\d+)?):/${p}$1:/;
			# Jump targets: jXX <label> (one operand).  Matches lN and lN_lM.
			s/^(\s*j[a-z]+\s+)(l\d+(?:_l\d+)?)\b/$1${p}$2/;
			# jnz val, lN, lM (two operands)
			s/^(\s*jnz\s+[^,]+,\s*)(l\d+(?:_l\d+)?)(\s*,\s*)(l\d+(?:_l\d+)?)\b/$1${p}$2$3${p}$4/;
			# Global glo: definitions and references (with or without leading
			# underscore — QBE emits both forms in different contexts).
			s/^_?glo(\d+):/${p}glo$1:/;
			s/\b_?glo(\d+)\b/${p}glo$1/g;
		' \
		| sed -e 's/word ptr \[/word [/g' \
		      -e 's/byte ptr \[/byte [/g' \
		      -e 's/dword ptr \[/dword [/g' \
		      -e 's|/\* \(.*\) \*/|; \1|' \
		      -e 's/^\([A-Za-z_][A-Za-z0-9_]*\) proc near$/\1:/' \
		      -e 's/^\([A-Za-z_][A-Za-z0-9_]*\) endp$/; \1 endp/' \
		      -e '/^:$/d' \
		      -e 's/^[[:space:]]*\.byte \(.*\)$/db \1/' \
		      -e 's/^[[:space:]]*\.short \(.*\)$/dw \1/' \
		      -e 's/^[[:space:]]*\.long \(.*\)$/dd \1/' \
		      -e 's/^[[:space:]]*\.int \(.*\)$/dw \1/' \
		      -e 's/^[[:space:]]*\.word \(.*\)$/dw \1/' \
		      -e 's/^[[:space:]]*\.quad \(.*\)$/dq \1/' \
		      -e 's/^[[:space:]]*\.zero \(.*\)$/times \1 db 0/' \
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
			# `test es, es` (or any segment register self-test) — nonsense
			# from the rname[] segment-register fallback.  Replace with a
			# noop test that the assembler will accept.
			s/^(\s*test\s+)(es|ds|cs|ss),\s*\g{2}\b/$1ax, ax ; XXX was test $2,$2/g;
		' \
		> "$asm_clean"
	# Skip stand-alone NASM object assembly — we concatenate all .asm
	# files in the link stage into one BIN unit.

	stage_pass+=("$src")
done

echo
echo "=== Build summary ==="
echo "  PASS: ${#stage_pass[@]}/${#SOURCES[@]}"
echo
if [ ${#stage_fail_minic[@]} -gt 0 ]; then
	echo "  Failed at minic (${#stage_fail_minic[@]}):"
	for f in "${stage_fail_minic[@]}"; do echo "    $f"; done
fi
if [ ${#stage_fail_qbe[@]} -gt 0 ]; then
	echo "  Failed at qbe (${#stage_fail_qbe[@]}):"
	for f in "${stage_fail_qbe[@]}"; do echo "    $f"; done
fi

if [ ${#stage_pass[@]} -eq 0 ]; then
	echo
	echo "No sources compiled; nothing to link."
	exit 1
fi
if [ ${#stage_pass[@]} -ne ${#SOURCES[@]} ]; then
	echo
	echo "Note: ${#stage_pass[@]}/${#SOURCES[@]} sources compiled."
	echo "Linking with stubs for the missing ones."
fi

# Stage 4: Link via NASM concatenation (no real linker on macOS toolchain).
# crt0.asm provides _start; doslib.asm provides DOS API helpers.
# Concatenate into one file and assemble as a flat .COM (ORG 0x100).
#
# crt0.asm uses NASM "section" directives and BITS/CPU.  We strip those
# and rely on the wrapper-supplied prologue.  Same for doslib.asm.
echo
echo "=== Linking ==="
LINK_ASM="$OUT_DIR/stevie.full.asm"
strip_runtime() {
	grep -v -E '^(BITS|CPU|section|global|extern)\b' "$1"
}
{
	echo "BITS 16"
	echo "CPU 8086"
	echo "ORG 0x100"
	echo
	echo "section .text"
	echo "; ===== crt0 ====="
	strip_runtime "$DOS_DIR/crt0.asm"
	echo
	echo "; ===== doslib ====="
	strip_runtime "$DOS_DIR/doslib.asm"
	echo
	echo "; ===== libstub (placeholder libc) ====="
	strip_runtime "$DOS_DIR/libstub.asm"
	echo
	for src in "${SOURCES[@]}"; do
		base="${src%.c}"
		[ -f "$OUT_DIR/$base.nasm.asm" ] || continue
		echo "; ===== $src ====="
		cat "$OUT_DIR/$base.nasm.asm"
		echo
	done
} > "$LINK_ASM"

if nasm -w-label-redef-late -w-pp-open-string -f bin "$LINK_ASM" -o "$OUT_DIR/stevie.com" 2>"$OUT_DIR/link.err"; then
	echo "  OK: $OUT_DIR/stevie.com ($(wc -c <"$OUT_DIR/stevie.com") bytes)"
else
	echo "  FAIL link: $(head -3 "$OUT_DIR/link.err")"
	exit 1
fi
