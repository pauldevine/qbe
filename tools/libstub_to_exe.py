#!/usr/bin/env python3
"""libstub_to_exe.py — convert near-call libstub to medium-model far-call libstub.

The .COM build (small model) calls every libstub function via `call`,
which pushes a 2-byte return address and uses `ret`.  The .EXE build
(medium model) calls every libstub function via `call far`, which
pushes a 4-byte return address (CS:IP) and uses `retf`.

Mechanical transforms:
  - `ret` (alone or end-of-line) → `retf`.  Skip when inside a `.cs:.int_op`
    self-mod block where `ret` is part of an opcode comment.
  - Every positive [bp+N] gets N += 2 (since the far-call return address
    occupies an extra 2 bytes between saved bp and the first arg).
    Negative offsets ([bp-N], local variables) are unchanged.
  - Wrap in NASM OMF segment directives matching the rest of the build
    (group DGROUP _DATA _BSS, segment LIBSTUB_TEXT class=CODE).

In:  minic/dos/libstub.asm
Out: <argv[1]>
"""
import re
import sys

PROLOGUE = """\
; Auto-generated from minic/dos/libstub.asm by tools/libstub_to_exe.py
; Medium-memory-model libc/runtime stubs for stevie .EXE build.
; All functions use far-call ABI: retf, args at [bp+6] and up.
bits 16
cpu 8086

%define _heap_size 32768

; Declare all segments with attributes up-front so subsequent `segment X`
; references inherit the attrs (NASM warns on redeclaration with attrs).
segment LIBSTUB_TEXT class=CODE align=2 use16
segment _DATA class=DATA align=2 use16
segment _BSS class=BSS align=2 use16
group DGROUP _DATA _BSS
"""

# malloc/free are replaced with .EXE-specific versions that bump from
# a fixed _BSS heap buffer instead of from _heap_end_of_image (a .COM
# image-end label).  This block is appended at the very end so it
# overrides nothing — the auto-converter detects malloc/free in the
# original libstub.asm and skips them.
MALLOC_EXE = """\

; -------- medium-model malloc/free --------
;
; void *malloc(size_t sz)
;   sz at [bp+6] (far-call ABI: 4 bytes return addr + 2 bytes saved bp).
;   Returns DX:AX, AX = offset within DGROUP, DX = 0 (caller treats as
;   near pointer; the upper 2 bytes are ignored when assigning to a `w`).
;
; cdecl callee-save: BX, SI, DI, BP.  We touch BX and CX; CX is caller-
; save and needs no protection, but BX must be preserved.
;
; The heap is a fixed 32KB buffer in _BSS.  Bump-only; free is a no-op.
global _malloc
_malloc:
    push bp
    mov bp, sp
    push bx                     ; preserve callee-save BX
    mov ax, [bp+6]
    add ax, 1
    and ax, 0xFFFE              ; word-align the size
    cmp word [_heap_initialized], 0
    jne .post_init
    mov word [_heap_ptr], _heap_buf
    mov word [_heap_top], _heap_buf + _heap_size
    mov word [_heap_initialized], 1
.post_init:
    mov bx, [_heap_ptr]
    mov cx, bx
    add cx, ax
    cmp cx, [_heap_top]
    ja .fail
    mov [_heap_ptr], cx
    mov ax, bx                  ; offset into DGROUP
    xor dx, dx
    pop bx                      ; restore callee-save BX
    pop bp
    retf
.fail:
    xor ax, ax
    xor dx, dx
    pop bx
    pop bp
    retf

global _free
_free:                          ; bump-allocator can't free
    retf

segment _DATA
_heap_initialized:  dw 0
_heap_ptr:          dw 0
_heap_top:          dw 0

segment _BSS
_heap_buf:          resb _heap_size
"""

# -------- File I/O via DOS INT 21h --------
#
# Replaces the always-fail _fopen/_getc/_fclose stubs in libstub.asm
# with real DOS-backed implementations (read-only for now).  A fixed
# pool of FILE slots lives in _BSS; each slot is 520 bytes:
#   +0  handle  (word)
#   +2  in_use  (byte)
#   +3  eof     (byte)
#   +4  buf_pos (word)
#   +6  buf_len (word)
#   +8  buf[512]
FILEIO_EXE = """

; -------- medium-model file I/O --------

%define _FBUF_SZ 512
%define _FILE_SZ (8 + _FBUF_SZ)
%define _NUM_FILES 4

segment LIBSTUB_TEXT

global _fopen
_fopen:
    push bp
    mov bp, sp
    push bx
    push si
    push di

    ; Find a free FILE slot.
    mov si, _file_slots
    mov di, _NUM_FILES
.fop_find:
    cmp byte [si + 2], 0          ; in_use?
    je .fop_found
    add si, _FILE_SZ
    dec di
    jnz .fop_find
    xor ax, ax                    ; no free slot -> NULL
    jmp .fop_done

.fop_found:
    ; INT 21h AH=3D AL=0 — open for reading.  DS:DX = filename.
    mov dx, [bp+6]                ; name (near in DGROUP)
    mov ax, 0x3D00
    int 0x21
    jc .fop_fail

    mov [si], ax                  ; handle
    mov byte [si+2], 1            ; in_use
    mov byte [si+3], 0            ; eof
    mov word [si+4], 0            ; buf_pos
    mov word [si+6], 0            ; buf_len

    mov ax, si                    ; return slot pointer
    jmp .fop_done

.fop_fail:
    xor ax, ax

.fop_done:
    pop di
    pop si
    pop bx
    pop bp
    retf


global _getc
_getc:
    push bp
    mov bp, sp
    push bx
    push si

    mov si, [bp+6]                ; FILE slot
    test si, si
    jz .gc_eof

.gc_try:
    mov ax, [si+4]                ; buf_pos
    cmp ax, [si+6]                ; buf_len
    jb .gc_byte

    cmp byte [si+3], 0            ; eof already?
    jne .gc_eof

    ; Refill: INT 21h AH=3F, BX=handle, CX=count, DS:DX=buf
    mov bx, [si]
    mov cx, _FBUF_SZ
    mov dx, si
    add dx, 8                     ; buf at slot+8
    mov ah, 0x3F
    int 0x21
    jc .gc_set_eof
    test ax, ax
    jz .gc_set_eof

    mov [si+6], ax                ; buf_len
    mov word [si+4], 0            ; buf_pos
    jmp .gc_try

.gc_byte:
    mov bx, [si+4]                ; buf_pos
    mov al, [si + bx + 8]
    xor ah, ah
    inc word [si+4]
    jmp .gc_done

.gc_set_eof:
    mov byte [si+3], 1
.gc_eof:
    mov ax, -1

.gc_done:
    pop si
    pop bx
    pop bp
    retf


global _fclose
_fclose:
    push bp
    mov bp, sp
    push bx
    push si

    mov si, [bp+6]                ; FILE slot
    test si, si
    jz .fc_done

    mov bx, [si]                  ; handle
    mov byte [si+2], 0            ; free slot

    ; INT 21h AH=3E, BX=handle
    mov ah, 0x3E
    int 0x21

.fc_done:
    xor ax, ax
    pop si
    pop bx
    pop bp
    retf


segment _BSS
_file_slots: resb (_NUM_FILES * _FILE_SZ)
"""

EPILOGUE = MALLOC_EXE + FILEIO_EXE


def shift_bp_offset(line):
    """Bump every `[bp+N]` (N>=0) by +2 so far-call args land correctly."""
    def repl(m):
        n = int(m.group(1))
        if n >= 0:
            return '[bp+{}]'.format(n + 2)
        return m.group(0)
    return re.sub(r'\[bp\+(\d+)\]', repl, line)


def transform(line):
    # Convert bare `ret` (with optional whitespace + optional trailing
    # comment) to `retf`.  Don't touch `retf` (already), `ret <something>`,
    # or `ret` appearing inside a string/comment.
    stripped = line.lstrip()
    indent = line[:len(line) - len(stripped)]
    # Detect whether line is a `ret` instruction
    m = re.match(r'^ret\b(?!\w)(.*)$', stripped)
    if m:
        rest = m.group(1)
        # rest may be empty or " ; comment"
        return indent + 'retf' + rest

    return shift_bp_offset(line)


def main():
    if len(sys.argv) != 3:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    in_path, out_path = sys.argv[1], sys.argv[2]
    with open(in_path) as f:
        src = f.read()

    out_lines = [PROLOGUE, '']

    # Track section state.  libstub.asm has no explicit segment
    # directives in the .COM build — everything is in one stream that
    # build-stevie.sh dumps into the .COM image.  For .EXE we need to
    # explicitly route content into LIBSTUB_TEXT (code) or _DATA (vars).
    # Heuristic: a global symbol followed by `dw`/`db`/`dd` is data;
    # otherwise it's code.
    in_code_seg = False
    pending = []
    cur_section = None

    def open_code():
        nonlocal cur_section
        if cur_section != 'text':
            out_lines.append('')
            out_lines.append('segment LIBSTUB_TEXT')
            cur_section = 'text'

    def open_data():
        nonlocal cur_section
        if cur_section != 'data':
            out_lines.append('')
            out_lines.append('segment _DATA')
            cur_section = 'data'

    # Emit the entire libstub as code; leave any `dw`/`db` definitions
    # interleaved (NASM OMF segment directives can switch back and forth).
    # Scan ahead for label-followed-by-data to route those into _DATA.
    lines = src.splitlines()

    # First pass: find all top-level labels and classify as code vs data
    # by looking at the next non-comment, non-blank line.
    label_kind = {}
    last_label = None
    for raw in lines:
        s = raw.strip()
        if not s or s.startswith(';'):
            continue
        m = re.match(r'^([A-Za-z_][\w]*):\s*(?:;.*)?$', s)
        if m:
            last_label = m.group(1)
            continue
        # Detect data declaration on the same line (e.g.
        # `_stdin:  dw 1`)
        m_lbl_data = re.match(r'^([A-Za-z_][\w]*):\s*(d[bwdq])\b', s)
        if m_lbl_data:
            label_kind[m_lbl_data.group(1)] = 'data'
            last_label = None
            continue
        if last_label and re.match(r'^d[bwdq]\b', s):
            label_kind[last_label] = 'data'
            last_label = None

    open_code()  # default

    # malloc/free/heap state are .COM-specific (they reference
    # _heap_end_of_image, a label injected by the .COM build script).
    # The .EXE replacements live in MALLOC_EXE (appended at end of
    # output).  Detect entry into these blocks and skip until the next
    # `global _xxx` for an unrelated symbol.
    SKIP_GLOBALS = {'_malloc', '_free', '_fopen', '_fclose', '_getc'}
    SKIP_LABELS  = {'_heap_initialized', '_heap_ptr', '_heap_top'}

    # Second pass: route lines.  When we encounter a label whose kind is
    # 'data', switch to _DATA segment until the next code label or end
    # of section.
    i = 0
    in_data_block = False
    skipping = False
    while i < len(lines):
        raw = lines[i]
        s = raw.strip()

        # `global _foo` declarations.  If sym is in SKIP_GLOBALS, enter
        # skip mode — we drop subsequent lines (the function body, any
        # heap-state data labels) until the next `global _xxx` for a
        # NON-skipped symbol.
        gm = re.match(r'^global\s+([A-Za-z_][\w]*)', s)
        if gm:
            sym = gm.group(1)
            if sym in SKIP_GLOBALS:
                skipping = True
                out_lines.append('; (skipped .COM-only ' + sym + '; see MALLOC_EXE override)')
                i += 1
                continue
            else:
                # Exit any prior skip region, emit this global verbatim.
                skipping = False
                out_lines.append(raw)
                i += 1
                continue

        # Top-level data labels we explicitly want to skip (e.g.
        # _heap_initialized, _heap_ptr, _heap_top — owned by malloc).
        m = re.match(r'^([A-Za-z_][\w]*):\s*(?:;.*)?$', s)
        m_lbl_data = re.match(r'^([A-Za-z_][\w]*):\s*(d[bwdq])\b', s)
        if m_lbl_data and m_lbl_data.group(1) in SKIP_LABELS:
            i += 1
            continue
        if m and m.group(1) in SKIP_LABELS:
            i += 1
            continue

        if skipping:
            i += 1
            continue

        if m_lbl_data and label_kind.get(m_lbl_data.group(1)) == 'data':
            open_data()
            out_lines.append(raw)
            in_data_block = True
            i += 1
            continue
        if m and label_kind.get(m.group(1)) == 'data':
            open_data()
            out_lines.append(raw)
            in_data_block = True
            i += 1
            continue
        if m and label_kind.get(m.group(1)) != 'data':
            in_data_block = False
            open_code()

        # Comments & blank — emit verbatim
        if not s or s.startswith(';'):
            out_lines.append(raw)
            i += 1
            continue

        # Data block: emit raw (no transform), stay in _DATA
        if in_data_block and re.match(r'^d[bwdq]\b', s):
            out_lines.append(raw)
            i += 1
            continue

        # Otherwise it's code: apply transform (ret→retf, bp+ shift)
        if in_data_block:
            in_data_block = False
            open_code()
        out_lines.append(transform(raw))
        i += 1

    out_lines.append(EPILOGUE)

    with open(out_path, 'w') as f:
        f.write('\n'.join(out_lines) + '\n')


if __name__ == '__main__':
    main()
