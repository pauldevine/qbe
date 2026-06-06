#!/bin/bash
# Asm->obj spike: push each per-TU i8086 .asm (produced by run-codegen.sh into
# cg/<base>.asm) through the real build's asm->obj stages -- asm_to_omf.py wrap
# then `nasm -f obj` -- one TU at a time, and catalog where each stops.  This is
# the next layer down from run-codegen.sh (which stops at qbe -> .asm).  No
# linking: per-TU signal only, to surface asm-syntax / OMF-wrap gaps cheaply
# before attempting a full link.
#
# Usage: bash run-asmobj.sh <base names...>      (base = cg/<base>.asm)
#   e.g. bash run-asmobj.sh $(cut -f1 codegen.tsv)
#        bash run-asmobj.sh obj objstr runtime
#
# Reuses the cg/<base>.asm files produced by run-codegen.sh (run that first).
QBE=/Users/pauldevine/projects/qbe
ASM2OMF=$QBE/tools/asm_to_omf.py
MODEL=medium
OUT=$QBE/build/mp-spike
CG="$OUT/cg"
mkdir -p "$OUT/obj" "$OUT/objerr"
summary="$OUT/asmobj.tsv"
: > "$summary"
for base in "$@"; do
  asm="$CG/$base.asm"
  omf="$OUT/obj/$base.omf.asm"
  obj="$OUT/obj/$base.obj"
  oerr="$OUT/objerr/$base.omf.err"
  nerr="$OUT/objerr/$base.nasm.err"
  if [ ! -f "$asm" ]; then
    printf "%s\tNO_ASM\tcg/%s.asm missing -- run run-codegen.sh first\n" "$base" "$base" >> "$summary"
    continue
  fi
  if ! "$ASM2OMF" "--model=$MODEL" "$base" "$asm" "$omf" 2>"$oerr"; then
    msg=$(grep -m1 -iE "error|traceback|exception" "$oerr" | head -c120)
    [ -z "$msg" ] && msg=$(tail -1 "$oerr" | head -c120)
    printf "%s\tOMF_FAIL\t%s\n" "$base" "$msg" >> "$summary"
    continue
  fi
  if ! nasm -w-label-redef-late -f obj "$omf" -o "$obj" 2>"$nerr"; then
    msg=$(grep -m1 -iE "error" "$nerr" | head -c160)
    [ -z "$msg" ] && msg=$(tail -1 "$nerr" | head -c160)
    printf "%s\tNASM_FAIL\t%s\n" "$base" "$msg" >> "$summary"
    continue
  fi
  printf "%s\tOK\t\n" "$base" >> "$summary"
done
echo "=== ASM->OBJ TALLY ==="
cut -f2 "$summary" | sort | uniq -c