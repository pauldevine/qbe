#!/bin/bash
# Enhanced DOS Build Script
# Builds complete DOS programs with C library support
# Usage: ./tools/build-dos-full.sh program.c output.com

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
OBJ_FILE="/tmp/${BASENAME}.obj"
FULL_ASM="/tmp/${BASENAME}_full.asm"

# Library files
LIBC_SSA="/tmp/libc.ssa"
LIBC_ASM="/tmp/libc.asm"
DOSLIB_OBJ="/tmp/doslib.obj"

# Paths
QBE="./qbe"
MINIC="./minic/minic"
DOSLIB_ASM="./minic/dos/doslib.asm"
LIBC_C="./minic/dos/libc.c"

echo "=== Enhanced DOS Build Pipeline ==="
echo "Input: $INPUT_C"
echo "Output: $OUTPUT_COM"
echo

# Step 1: Compile DOS library to object file (only if needed)
if [ ! -f "$DOSLIB_OBJ" ] || [ "$DOSLIB_ASM" -nt "$DOSLIB_OBJ" ]; then
    echo "[1/7] Assembling DOS library..."
    nasm -f bin -o "$DOSLIB_OBJ" "$DOSLIB_ASM" 2>/dev/null || {
        echo "Note: NASM assembly of doslib failed, will inline"
    }
fi

# Step 2: Compile C library to SSA
echo "[2/7] Compiling C library..."
if [ -f "$LIBC_C" ]; then
    "$MINIC" < "$LIBC_C" > "$LIBC_SSA" 2>/dev/null || {
        echo "Note: C library compilation skipped"
        touch "$LIBC_SSA"
    }

    # Convert to assembly
    if [ -s "$LIBC_SSA" ]; then
        "$QBE" -t i8086 "$LIBC_SSA" > "$LIBC_ASM" 2>/dev/null || true
    fi
fi

# Step 3: Compile main program to SSA
echo "[3/7] Compiling C to QBE SSA..."
"$MINIC" < "$INPUT_C" > "$SSA_FILE"

# Step 4: QBE SSA to i8086 assembly
echo "[4/7] Compiling SSA to i8086 assembly..."
"$QBE" -t i8086 "$SSA_FILE" > "$ASM_FILE"

# Step 5: Convert MASM syntax to NASM syntax
echo "[5/7] Converting to NASM syntax..."
sed -i 's/word ptr \[/word [/g' "$ASM_FILE"
sed -i 's/byte ptr \[/byte [/g' "$ASM_FILE"
sed -i 's/dword ptr \[/dword [/g' "$ASM_FILE"

# Step 6: Combine everything into one assembly file
echo "[6/7] Combining with DOS runtime..."
cat > "$FULL_ASM" << 'EOF'
BITS 16
CPU 8086
ORG 0x100

section .text

; ============================================================================
; DOS Entry Point
; ============================================================================
_start:
    call _main
    mov ah, 4Ch
    mov al, 0
    int 21h

; ============================================================================
; DOS Library Functions (inline)
; ============================================================================

; void dos_putchar(int ch)
_dos_putchar:
    push bp
    mov bp, sp
    mov dx, [bp+4]
    mov ah, 02h
    int 21h
    pop bp
    ret

; int dos_getchar(void)
_dos_getchar:
    push bp
    mov bp, sp
    mov ah, 01h
    int 21h
    xor ah, ah
    pop bp
    ret

; int dos_getch(void)
_dos_getch:
    push bp
    mov bp, sp
    mov ah, 08h
    int 21h
    xor ah, ah
    pop bp
    ret

; void dos_puts(char *str)
_dos_puts:
    push bp
    mov bp, sp
    push si
    mov si, [bp+4]
.loop:
    lodsb
    cmp al, 0
    je .done
    mov dl, al
    mov ah, 02h
    int 21h
    jmp .loop
.done:
    pop si
    pop bp
    ret

; int dos_open(char *filename, int mode)
_dos_open:
    push bp
    mov bp, sp
    mov dx, [bp+4]
    mov al, [bp+6]
    mov ah, 3Dh
    int 21h
    jc .error
    pop bp
    ret
.error:
    mov ax, -1
    pop bp
    ret

; int dos_create(char *filename)
_dos_create:
    push bp
    mov bp, sp
    mov dx, [bp+4]
    xor cx, cx
    mov ah, 3Ch
    int 21h
    jc .error
    pop bp
    ret
.error:
    mov ax, -1
    pop bp
    ret

; void dos_close(int handle)
_dos_close:
    push bp
    mov bp, sp
    mov bx, [bp+4]
    mov ah, 3Eh
    int 21h
    pop bp
    ret

; int dos_read(int handle, char *buffer, int count)
_dos_read:
    push bp
    mov bp, sp
    mov bx, [bp+4]
    mov dx, [bp+6]
    mov cx, [bp+8]
    mov ah, 3Fh
    int 21h
    jc .error
    pop bp
    ret
.error:
    mov ax, -1
    pop bp
    ret

; int dos_write(int handle, char *buffer, int count)
_dos_write:
    push bp
    mov bp, sp
    mov bx, [bp+4]
    mov dx, [bp+6]
    mov cx, [bp+8]
    mov ah, 40h
    int 21h
    jc .error
    pop bp
    ret
.error:
    mov ax, -1
    pop bp
    ret

; int dos_delete(char *filename)
_dos_delete:
    push bp
    mov bp, sp
    mov dx, [bp+4]
    mov ah, 41h
    int 21h
    jc .error
    xor ax, ax
    pop bp
    ret
.error:
    mov ax, -1
    pop bp
    ret

; int dos_get_version(void)
_dos_get_version:
    push bp
    mov bp, sp
    mov ah, 30h
    int 21h
    pop bp
    ret

; ============================================================================
; C Library Functions (inline)
; ============================================================================

; int strlen(char *s)
_strlen:
    push bp
    mov bp, sp
    push si
    mov si, [bp+4]
    xor ax, ax
.loop:
    cmp byte [si], 0
    je .done
    inc ax
    inc si
    jmp .loop
.done:
    pop si
    pop bp
    ret

; char *strcpy(char *dest, char *src)
_strcpy:
    push bp
    mov bp, sp
    push si
    push di
    mov di, [bp+4]  ; dest
    mov si, [bp+6]  ; src
.loop:
    lodsb
    stosb
    cmp al, 0
    jne .loop
    mov ax, [bp+4]  ; return dest
    pop di
    pop si
    pop bp
    ret

; char *itoa(int value, char *str, int base)
_itoa:
    push bp
    mov bp, sp
    push si
    push di

    mov ax, [bp+4]  ; value
    mov di, [bp+6]  ; str pointer
    mov cx, [bp+8]  ; base

    ; Simple conversion for base 10
    push di
    xor dx, dx

    ; Check if negative
    test ax, ax
    jns .positive
    neg ax
    push ax
    mov byte [di], '-'
    inc di
    pop ax

.positive:
    ; Convert to string (reversed)
    mov si, di
.convert:
    xor dx, dx
    div cx
    add dl, '0'
    cmp dl, '9'
    jle .store
    add dl, 7  ; For hex A-F
.store:
    mov [di], dl
    inc di
    test ax, ax
    jnz .convert

    ; Null terminate
    mov byte [di], 0

    ; Reverse the string
    dec di
.reverse:
    cmp si, di
    jge .done
    mov al, [si]
    mov ah, [di]
    mov [si], ah
    mov [di], al
    inc si
    dec di
    jmp .reverse

.done:
    pop ax  ; return str
    pop di
    pop si
    pop bp
    ret

; ============================================================================
; Generated Program Code
; ============================================================================

EOF

# Append generated code (skip directives and library function dups)
grep -v "^\.text" "$ASM_FILE" | \
    grep -v "^\.balign" | \
    grep -v "^\.section" | \
    grep -v "^\.globl" | \
    sed 's/^\/\* /; /g' >> "$FULL_ASM"

echo "  Generated: $FULL_ASM"

# Step 7: Assemble to .COM
echo "[7/7] Assembling to DOS .COM..."
nasm -f bin -o "$OUTPUT_COM" "$FULL_ASM"

# Show file info
SIZE=$(stat -c%s "$OUTPUT_COM")
echo
echo "=== Build Complete ==="
echo "Output file: $OUTPUT_COM"
echo "Size: $SIZE bytes"
echo
echo "To test in DOSBox:"
echo "  dosbox -c \"mount c $(dirname $(realpath $OUTPUT_COM))\" -c \"c:\" -c \"$(basename $OUTPUT_COM)\" -c \"exit\""
