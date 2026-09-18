#!/bin/bash
# build-ckermit.sh -- compile C-Kermit (the Victor 9000 port in
# ~/projects/ckermit) with THIS toolchain, against OUR headers.
#
#   tools/build-ckermit.sh [--model=large] [--keep-going] [module...]
#
# Per module: clang -E (minic/dos/ckermit/include, no Watcom headers) ->
# strip Watcom language extensions (tools/ckermit/fixups.pl, W rules) ->
# minic -G -> qbe -> asm_to_omf -> nasm.  Objects land in
# build/ckermit/<model>/.  ckcpro.c is regenerated from ckcpro.w by the host
# tool wart (built from ckwart.c), as victorow.mak does.
#
# The ckermit tree is read-only here: its authoritative build is Open Watcom
# (victorow.mak).  Configuration comes from ckvictor.h, force-included first,
# exactly as there.  Linking (runtime + libc fill) is not done yet.
#
# Preprocessor flags: tools/ckermit/cppflags.sh.  tools/ckermit/hdrcheck.sh
# verifies that our headers select the same C-Kermit code as Watcom's.
set -u
Q="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CK="${CK:-$HOME/projects/ckermit}"
MODEL=large
KEEP_GOING=0
MODS=()
for a in "$@"; do
	case "$a" in
	--model=*) MODEL="${a#--model=}" ;;
	--keep-going|-k) KEEP_GOING=1 ;;
	-*) echo "unknown option $a" >&2; exit 2 ;;
	*) MODS+=("$a") ;;
	esac
done
case "$MODEL" in
compact|large|huge) ;;
*) echo "build-ckermit: far-data model required (compact/large/huge), got $MODEL" >&2; exit 2 ;;
esac
[ -f "$CK/ckvictor.h" ] || { echo "build-ckermit: no C-Kermit tree at $CK" >&2; exit 77; }

ALL_MODS="ckcmai ckclib ckcfns ckcfn2 ckcfn3 ckcpro ckucmd ckuusr ckuus2 ckuus3
 ckuus4 ckuus5 ckuus6 ckuus7 ckuusx ckuusy ckutio ckufio ckusig ckuxla ckcuni
 ckcnet ckctel ckvictor"
[ ${#MODS[@]} -eq 0 ] && MODS=($ALL_MODS)

OUT="$Q/build/ckermit/$MODEL"
mkdir -p "$OUT"
FIXUPS="$Q/tools/ckermit/fixups.pl"
MINIC="$Q/minic/minic"
QBE="$Q/qbe"

. "$Q/tools/ckermit/cppflags.sh"

NORMALIZE='s/\bunsigned short int\b/unsigned short/g;s/\bunsigned long int\b/unsigned long/g;s/\bsigned short int\b/short/g;s/\bsigned long int\b/long/g;s/\blong long int\b/long long/g;s/\blong int\b/long/g;s/\bshort int\b/short/g;s/\bsigned char\b/char/g;s/\bsigned long long\b/long long/g;s/\bsigned long\b/long/g;s/\bsigned int\b/int/g'

# ckcpro.c from ckcpro.w (host tool, as victorow.mak), kept apart from the
# per-module outputs ($OUT/<mod>.c is the fixed-up preprocessed text).
GEN="$Q/build/ckermit/gen"
mkdir -p "$GEN"
WART="$GEN/wart"
if [ ! -x "$WART" ] || [ "$CK/ckwart.c" -nt "$WART" ]; then
	cc -w -DSIGTYP=void -o "$WART" "$CK/ckwart.c" || { echo "build-ckermit: wart build failed" >&2; exit 1; }
fi
if [ ! -f "$GEN/ckcpro.c" ] || [ "$CK/ckcpro.w" -nt "$GEN/ckcpro.c" ] || [ "$WART" -nt "$GEN/ckcpro.c" ]; then
	(cd "$CK" && "$WART" ckcpro.w "$GEN/ckcpro.c") >/dev/null || { echo "build-ckermit: wart failed" >&2; exit 1; }
fi

fail=0
for m in "${MODS[@]}"; do
	src="$CK/$m.c"
	[ "$m" = ckcpro ] && src="$GEN/ckcpro.c"
	err="$OUT/$m.err"
	stage() { echo "$m $1: $(head -c 400 "$err" | tr '\n' ' ')"; fail=1; }
	if ! clang "${CPPFLAGS[@]}" -P -include "$CK/ckvictor.h" "$src" -o "$OUT/$m.i" 2>"$err"; then stage CPP
	elif ! perl -p "$FIXUPS" "$OUT/$m.i" | tr -d '\r\032' | perl -pe "$NORMALIZE" > "$OUT/$m.c" 2>"$err"; then stage FIXUPS
	elif ! "$MINIC" -m "$MODEL" -G < "$OUT/$m.c" > "$OUT/$m.ssa" 2>"$err"; then stage MINIC
	elif ! "$QBE" -t i8086 -m "$MODEL" "$OUT/$m.ssa" > "$OUT/$m.asm" 2>"$err"; then stage QBE
	elif ! "$Q/tools/asm_to_omf.py" "--model=$MODEL" "$m" "$OUT/$m.asm" "$OUT/$m.omf.asm" 2>"$err"; then stage OMF
	elif ! nasm -w-label-redef-late -f obj "$OUT/$m.omf.asm" -o "$OUT/$m.obj" 2>"$err"; then stage NASM
	else echo "$m PASS"; continue
	fi
	[ $KEEP_GOING = 1 ] || exit 1
done
exit $fail
