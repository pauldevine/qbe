#!/bin/bash
# Complete C to DOS .COM build pipeline
# Usage: ./tools/c-to-com.sh input.c output.com

set -e

if [ $# -lt 2 ]; then
    echo "Usage: $0 input.c output.com"
    exit 1
fi

INPUT_C="$1"
OUTPUT_COM="$2"

# Derived filenames
BASENAME=$(basename "$INPUT_C" .c)
SSA_FILE="/tmp/${BASENAME}.ssa"
ASM_FILE="/tmp/${BASENAME}.asm"
FULL_ASM="/tmp/${BASENAME}_full.asm"

# Paths
QBE="./qbe"
MINIC="./minic/minic"
DOS_CRT0="./minic/dos/crt0.asm"
DOS_SYSCALLS="./minic/dos/dos_syscalls.asm"

echo "=== C to DOS .COM Pipeline ==="
echo "Input: $INPUT_C"
echo "Output: $OUTPUT_COM"
echo

# Step 1: C to QBE SSA
echo "[1/5] Compiling C to QBE SSA..."
if [ ! -f "$MINIC" ]; then
    echo "Error: MiniC compiler not found at $MINIC"
    echo "Run 'make -C minic' first"
    exit 1
fi
"$MINIC" < "$INPUT_C" > "$SSA_FILE"
echo "  Generated: $SSA_FILE"

# Step 2: QBE SSA to i8086 assembly
echo "[2/5] Compiling SSA to i8086 assembly..."
if [ ! -f "$QBE" ]; then
    echo "Error: QBE not found at $QBE"
    echo "Run 'make' first"
    exit 1
fi
"$QBE" -t i8086 "$SSA_FILE" > "$ASM_FILE"
echo "  Generated: $ASM_FILE"

# Step 3: Convert MASM syntax to NASM syntax
echo "[3/5] Converting to NASM syntax..."
sed -i 's/word ptr \[/word [/g' "$ASM_FILE"
sed -i 's/byte ptr \[/byte [/g' "$ASM_FILE"
sed -i 's/dword ptr \[/dword [/g' "$ASM_FILE"
echo "  Fixed syntax in $ASM_FILE"

# Step 4: Combine with DOS runtime
echo "[4/5] Combining with DOS runtime..."
cat > "$FULL_ASM" << EOF
BITS 16
CPU 8086

section .text

; Entry point
global _start
_start:
    call _main
    mov ah, 4Ch
    int 21h

; DOS putchar syscall
global _dos_putchar
_dos_putchar:
dos_putchar:
    push bp
    mov bp, sp
    mov dx, [bp+4]    ; Get character argument
    mov ah, 02h       ; DOS function 02h - write character
    int 21h
    pop bp
    ret

; Generated code from QBE
EOF

# Append generated code (skip directives, comments, and dos_putchar function)
grep -v "^\.text" "$ASM_FILE" | \
    grep -v "^\.balign" | \
    grep -v "^\.section" | \
    grep -v "^\.globl" | \
    sed '/^_dos_putchar:/,/^\/\* end function dos_putchar \*\//d' | \
    sed 's/^\/\* /; /g' >> "$FULL_ASM"

echo "  Generated: $FULL_ASM"

# Step 5: Assemble to .COM
echo "[5/5] Assembling to DOS .COM..."
nasm -f bin -o "$OUTPUT_COM" "$FULL_ASM"
echo "  Generated: $OUTPUT_COM"

# Show file info
SIZE=$(stat -c%s "$OUTPUT_COM")
echo
echo "=== Build Complete ==="
echo "Output file: $OUTPUT_COM"
echo "Size: $SIZE bytes"
echo
echo "To test in DOSBox:"
echo "  dosbox -c \"mount c $(dirname $(realpath $OUTPUT_COM))\" -c \"c:\" -c \"$(basename $OUTPUT_COM)\" -c \"exit\""
