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

Usage: libstub_to_exe.py [--model=<m>] <in.asm> <out.asm>

Near-code memory models (tiny / small / compact) collapse all CODE
into a shared `_TEXT` segment so the linker can coalesce the libstub
with user code into a single 64KB CS frame.  Far-code models
(medium / large / huge) keep the dedicated `LIBSTUB_TEXT` segment so
each module gets its own physical 64KB code segment and the runtime
uses `call far` / `retf` to traverse them.
"""
import re
import sys

PROLOGUE_TEMPLATE = """\
; Auto-generated from minic/dos/libstub.asm by tools/libstub_to_exe.py
; {model_label} libc/runtime stubs for stevie .EXE build.
; {abi_label}
bits 16
cpu 8086

; Heap budget for the medium-model .EXE.  omf_link puts SS=DS=DGROUP
; for near-pointer correctness, so DGROUP + stack must fit in 64KB.
; Non-heap DGROUP ≈ 21KB + ~2KB _file_slots, stack 4KB, leaving ≈ 35KB
; for the heap.  We use 34816 (34KB) — the proven-working budget from
; stevie_MEDIUM.exe testing.  A bigger heap + smaller _file_slots was
; attempted but stevie's readfile failed with "alloc() is unable to
; find memory!" — root cause not yet pinned down, keep proven layout.
%define _heap_size 34816

; Declare all segments with attributes up-front so subsequent `segment X`
; references inherit the attrs (NASM warns on redeclaration with attrs).
segment {code_seg} class=CODE align=2 use16
segment _DATA class=DATA align=2 use16
segment _BSS class=BSS align=2 use16
group DGROUP _DATA _BSS

; Paragraph base of DGROUP, patched at load time by the MZ relocation the
; `dw DGROUP` segment-base fixup produces.  Helpers that must (re)load
; DS = DGROUP read it via a cs: override (`mov ds, [cs:_dgroup_para]`)
; instead of the historical `push ss / pop ds` — under omf_link's
; --separate-stack the stack has its own segment, so SS is NOT a synonym
; for DGROUP.  Lives in the code segment so cs: reaches it even while DS
; is swapped away (which is exactly when these sites run).
segment {code_seg}
_dgroup_para: dw DGROUP
"""

# malloc/free are replaced with .EXE-specific versions that allocate
# from a fixed _BSS heap buffer.  Freelist-based: free() actually
# reclaims memory and adjacent free blocks coalesce, so stevie's
# allocate-free-allocate edit cycle no longer leaks.
#
# Block layout: 2-byte size header (total block bytes incl. header,
# always even ≥ 4), then payload.  Free blocks chain via a freelist
# whose `next` pointers live in the payload area.
MALLOC_EXE = """\

; -------- medium-model malloc/free (freelist allocator) --------
;
; void *malloc(size_t sz)
;   sz at [bp+6] (far-call ABI: 4 bytes return addr + 2 bytes saved bp).
;   Returns DX:AX, AX = offset within DGROUP, DX = SS (= DGROUP segment).
;   Medium-model callers read only AX (void* is 2 bytes); the DX=SS write
;   is harmless there.  Far-data callers (compact/large/huge) read DX:AX
;   as a 4-byte far pointer.
;
; cdecl callee-save: BX, SI, DI, BP — preserved.
;
; Each block has a 2-byte size header (total bytes including header).
; Free blocks are chained through a sorted freelist using the first
; word of their payload as a `next` offset (0 terminates the list).
; On free we coalesce with adjacent free blocks.
global _malloc
_malloc:
    push bp
    mov bp, sp
    push si
    push di
    push bx
    mov ax, [bp+6]              ; sz
    add ax, 3                   ; +2 header + 1 round-up bit
    and ax, 0xFFFE              ; word-align (total even)
    cmp ax, 4
    jae .ok_size
    mov ax, 4                   ; min block size
.ok_size:
    ; AX = total block size needed (incl. header)

    cmp word [_heap_initialized], 0
    jne .scan_free
    mov word [_heap_ptr], _heap_buf
    mov word [_heap_top], _heap_buf + _heap_size
    mov word [_freelist_head], 0
    mov word [_heap_initialized], 1

.scan_free:
    ; First-fit walk of the freelist.
    ; DI = address of the previous node's `next` slot (head ptr to start).
    ; SI = current block address (0 if list end).
    mov di, _freelist_head
    mov si, [di]
.scan_loop:
    test si, si
    jz .no_fit
    mov bx, [si]                ; current block size
    cmp bx, ax
    jb .next_node
    ; Found a fit.  Decide whether to split.
    mov cx, bx
    sub cx, ax                  ; CX = leftover bytes
    cmp cx, 4
    jae .split

    ; Use the whole block; unlink from freelist.
    mov bx, [si+2]              ; current's `next`
    mov [di], bx                ; prev's `next` = current's `next`
    mov ax, si
    add ax, 2                   ; return payload
    jmp .alloc_done

.split:
    ; Carve the tail of the current block off as the allocation.
    ; Keeping the head in the freelist avoids touching freelist links.
    mov [si], cx                ; shrink current free block to CX
    add si, cx                  ; SI now points at the tail (new alloc)
    mov [si], ax                ; new alloc block size
    mov ax, si
    add ax, 2                   ; payload
    jmp .alloc_done

.next_node:
    lea di, [si+2]              ; addr of current's `next` slot
    mov si, [si+2]              ; advance
    jmp .scan_loop

.no_fit:
    ; No freelist match — bump from the high-water mark.
    mov bx, [_heap_ptr]
    mov cx, bx
    add cx, ax
    cmp cx, [_heap_top]
    ja .fail
    mov [_heap_ptr], cx
    mov [bx], ax                ; size header
    mov ax, bx
    add ax, 2                   ; payload

.alloc_done:
    mov dx, [cs:_dgroup_para]   ; DGROUP segment (far-data ABI; the heap
                                ; lives in DGROUP _BSS — NOT `mov dx, ss`,
                                ; which breaks under --separate-stack)
    pop bx
    pop di
    pop si
    pop bp
    retf

.fail:
    xor ax, ax
    xor dx, dx
    pop bx
    pop di
    pop si
    pop bp
    retf

; void free(void *p)
;   p at [bp+6].  free(NULL) is a no-op.
;   Inserts the block into the address-sorted freelist and coalesces
;   with adjacent free neighbours.
global _free
_free:
    push bp
    mov bp, sp
    push si
    push di
    push bx
    mov bx, [bp+6]
    test bx, bx
    jz .free_done
    sub bx, 2                   ; BX = block header addr
    mov cx, [bx]                ; CX = block size

    ; Walk freelist to find the insertion point.
    ; DI = addr of preceding `next` slot (head ptr to start).
    ; SI = next free block (0 if BX goes at the end).
    mov di, _freelist_head
    mov si, [di]
.free_walk:
    test si, si
    jz .free_insert
    cmp si, bx
    ja .free_insert
    lea di, [si+2]
    mov si, [si+2]
    jmp .free_walk

.free_insert:
    ; Splice BX between prev (whose `next` lives at DI) and SI.
    mov [di], bx
    mov [bx+2], si

    ; Try to coalesce with the next free block.
    test si, si
    jz .no_merge_next
    mov ax, bx
    add ax, cx
    cmp ax, si
    jne .no_merge_next
    add cx, [si]                ; size += next's size
    mov [bx], cx
    mov ax, [si+2]
    mov [bx+2], ax              ; next link skips over si
.no_merge_next:

    ; Try to coalesce with the previous free block.
    ; DI points at the prev's `next` field (i.e. prev_hdr + 2) unless
    ; DI == _freelist_head (no prev).
    cmp di, _freelist_head
    je .free_done
    sub di, 2                   ; DI = prev block header
    mov ax, [di]                ; prev size
    add ax, di                  ; prev_end
    cmp ax, bx
    jne .free_done
    ; Merge prev with current.
    mov ax, [di]
    add ax, cx
    mov [di], ax
    mov ax, [bx+2]
    mov [di+2], ax

.free_done:
    pop bx
    pop di
    pop si
    pop bp
    retf

segment _DATA
_heap_initialized:  dw 0
_heap_ptr:          dw 0
_heap_top:          dw 0
_freelist_head:     dw 0

segment _BSS
_heap_buf:          resb _heap_size
"""

# -------- File I/O via DOS INT 21h --------
#
# Replaces the always-fail _fopen/_getc/_fclose stubs in libstub.asm
# with real DOS-backed implementations.  A fixed pool of FILE slots
# lives in _BSS; each slot is 520 bytes:
#   +0  handle  (word)   — DOS file handle
#   +2  in_use  (byte)   — slot allocated
#   +3  flags   (byte)   — bit0 = eof, bit1 = writing
#   +4  buf_pos (word)   — read mode: cursor in buf
#   +6  buf_len (word)   — read mode: bytes valid in buf
#   +8  buf[512]
#
# Write paths are unbuffered: every fputc/fputs/fwrite/fprintf call
# turns into one INT 21h AH=40 write directly on the handle.  fprintf
# formats into a 512-byte stack buffer via a re-entrant call to
# _sprintf, then writes the result.
#
# stdout/stderr "FILE*" sentinels in libstub.asm are just word
# variables holding the standard DOS handles (1, 2).  The write
# routines extract the handle as `*(word *)file` which works uniformly
# for both real FILE slots and these single-word sentinels.
FILEIO_EXE = """

; -------- medium-model file I/O --------

%define _FBUF_SZ  512
%define _FILE_SZ  (8 + _FBUF_SZ)
%define _NUM_FILES 4
%define _FP_BUFSZ 512                 ; stack scratch for _fprintf/_printf

segment LIBSTUB_TEXT

; ----------------------------------------------------------------------
; FILE *fopen(const char *name, const char *mode)
;
; Supports "r", "rb", "w", "wb", "a", "ab" (and "+" variants treated as
; the base mode).  Returns NULL on failure or when the FILE-slot pool
; is exhausted.
; ----------------------------------------------------------------------
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
    ; Inspect mode[0].
    mov bx, [bp+8]                ; mode
    mov al, [bx]
    cmp al, 'r'
    je .fop_read
    cmp al, 'w'
    je .fop_write
    cmp al, 'a'
    je .fop_append
    xor ax, ax                    ; unknown mode -> NULL
    jmp .fop_done

.fop_read:
    mov dx, [bp+6]                ; filename
    mov ax, 0x3D00                ; open existing, read-only
    int 0x21
    jc .fop_fail
    mov byte [si+3], 0            ; flags: read mode
    jmp .fop_install

.fop_write:
    mov dx, [bp+6]
    xor cx, cx                    ; attribute = normal
    mov ah, 0x3C                  ; create / truncate
    int 0x21
    jc .fop_fail
    mov byte [si+3], 2            ; flags: bit1 = writing
    jmp .fop_install

.fop_append:
    ; Try to open existing for read+write; if missing, create.
    mov dx, [bp+6]
    mov ax, 0x3D02
    int 0x21
    jnc .fop_seek_end
    ; Create new (file didn't exist).
    mov dx, [bp+6]
    xor cx, cx
    mov ah, 0x3C
    int 0x21
    jc .fop_fail
    mov byte [si+3], 2
    jmp .fop_install

.fop_seek_end:
    ; AX = handle.  Seek to EOF: AH=42 AL=2, CX:DX = 0.
    mov bx, ax
    xor cx, cx
    xor dx, dx
    mov ax, 0x4202
    int 0x21
    mov ax, bx                    ; restore handle into AX
    mov byte [si+3], 2

.fop_install:
    mov [si], ax                  ; handle
    mov byte [si+2], 1            ; in_use
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


; ----------------------------------------------------------------------
; int getc(FILE *)  — buffered byte read; returns EOF (-1) at end.
; ----------------------------------------------------------------------
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

    test byte [si+3], 1           ; eof already?
    jnz .gc_eof

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
    xor dx, dx                    ; high 16 bits of 32-bit return = 0
    jmp .gc_done

.gc_set_eof:
    or byte [si+3], 1
.gc_eof:
    mov ax, -1
    mov dx, -1                    ; high 16 bits = -1 so 32-bit return = 0xFFFFFFFF
                                  ; (stevie compares `c == EOF` as 32-bit)

.gc_done:
    pop si
    pop bx
    pop bp
    retf


; ----------------------------------------------------------------------
; int fclose(FILE *)
; Unbuffered write mode means there's nothing to flush.  Closes the
; underlying DOS handle and frees the slot.  Safe on stdin/out/err
; sentinels (they have in_use=0, so the slot-clear is a no-op, and
; closing handle 0/1/2 is something we never want to do — so we only
; act when the pointer is inside the _file_slots pool).
; ----------------------------------------------------------------------
global _fclose
_fclose:
    push bp
    mov bp, sp
    push bx
    push si

    mov si, [bp+6]                ; FILE *
    test si, si
    jz .fc_done

    ; Only close if this is a real slot from our pool.
    cmp si, _file_slots
    jb .fc_done
    mov ax, _file_slots
    add ax, (_NUM_FILES * _FILE_SZ)
    cmp si, ax
    jae .fc_done

    cmp byte [si+2], 0            ; in_use?
    je .fc_done

    mov bx, [si]                  ; handle
    mov byte [si+2], 0            ; free slot
    mov ah, 0x3E                  ; close handle
    int 0x21

.fc_done:
    xor ax, ax
    pop si
    pop bx
    pop bp
    retf


; ----------------------------------------------------------------------
; int fflush(FILE *)  — no-op since writes are unbuffered.
; ----------------------------------------------------------------------
global _fflush
_fflush:
    xor ax, ax
    retf


; ----------------------------------------------------------------------
; size_t fwrite(const void *ptr, size_t sz, size_t n, FILE *)
; Writes sz*n bytes via INT 21h AH=40.  Returns n on success, 0 on
; error or zero size.
; ----------------------------------------------------------------------
global _fwrite
_fwrite:
    push bp
    mov bp, sp
    push bx
    push si

    mov ax, [bp+8]                ; sz
    mov cx, [bp+10]               ; n
    mul cx                        ; AX = sz*n (DX overflow ignored)
    test ax, ax
    jz .fw_done
    mov cx, ax                    ; byte count
    mov dx, [bp+6]                ; ptr
    mov si, [bp+12]               ; FILE*
    mov bx, [si]                  ; handle (works for slot or sentinel)
    mov ah, 0x40
    int 0x21
    jc .fw_err

    ; AX = bytes actually written; convert back to "items written".
    xor dx, dx
    div word [bp+8]               ; AX = bytes / sz
    jmp .fw_done
.fw_err:
    xor ax, ax
.fw_done:
    pop si
    pop bx
    pop bp
    retf


; ----------------------------------------------------------------------
; size_t fread(void *ptr, size_t size, size_t nmemb, FILE *)
;
; Direct INT 21h AH=3F read of size*nmemb bytes into ptr.  Returns the
; number of *items* read (= bytes / size).  Bypasses the FILE slot's
; line buffer used by _getc; mixing fread + getc on the same handle is
; not supported (the standard makes no such guarantee either).
; ----------------------------------------------------------------------
global _fread
_fread:
    push bp
    mov bp, sp
    push bx
    push si

    mov ax, [bp+8]                ; size
    mul word [bp+10]              ; size*nmemb -> DX:AX
    test dx, dx
    jnz .fr_err                   ; byte count > 64KB — unsupported
    mov cx, ax                    ; total bytes
    test cx, cx
    jz .fr_zero
    mov dx, [bp+6]                ; DS:DX -> ptr
    mov si, [bp+12]               ; FILE *
    test si, si
    jz .fr_err
    mov bx, [si]                  ; DOS handle
    mov ah, 0x3F
    int 0x21
    jc .fr_err

    ; AX = bytes actually read.  Convert to item count.
    mov cx, [bp+8]                ; size
    test cx, cx
    jz .fr_done                   ; size=0: leave AX as bytes (UB anyway)
    xor dx, dx
    div cx                        ; AX = bytes / size
    jmp .fr_done
.fr_zero:
    xor ax, ax
    jmp .fr_done
.fr_err:
    xor ax, ax
.fr_done:
    pop si
    pop bx
    pop bp
    retf


; ----------------------------------------------------------------------
; int fputc(int c, FILE *)  — write one byte via INT 21h AH=40.
; ----------------------------------------------------------------------
global _fputc
_fputc:
    push bp
    mov bp, sp
    push bx
    push si

    mov si, [bp+8]                ; FILE*
    mov bx, [si]                  ; handle
    mov ax, [bp+6]                ; c (low byte)
    push ax                       ; scratch 1-byte buffer on stack
    mov dx, sp                    ; DS:DX -> our byte
    mov cx, 1
    mov ah, 0x40
    int 0x21
    pop ax                        ; discard scratch
    jc .fpc_err

    mov ax, [bp+6]                ; return the char written
    and ax, 0xFF
    jmp .fpc_done
.fpc_err:
    mov ax, -1
.fpc_done:
    pop si
    pop bx
    pop bp
    retf


; ----------------------------------------------------------------------
; int fputs(const char *s, FILE *)  — write a null-terminated string.
; ----------------------------------------------------------------------
global _fputs
_fputs:
    push bp
    mov bp, sp
    push bx
    push si
    push di

    mov si, [bp+6]                ; s
    test si, si
    jz .fps_err
    mov di, si
.fps_len:
    cmp byte [di], 0
    je .fps_len_done
    inc di
    jmp .fps_len
.fps_len_done:
    mov cx, di
    sub cx, si                    ; CX = strlen
    jz .fps_ok                    ; empty string — still success

    mov dx, si                    ; DS:DX -> s
    mov si, [bp+8]                ; FILE*
    mov bx, [si]                  ; handle
    mov ah, 0x40
    int 0x21
    jc .fps_err

.fps_ok:
    xor ax, ax                    ; success (non-negative)
    jmp .fps_done
.fps_err:
    mov ax, -1
.fps_done:
    pop di
    pop si
    pop bx
    pop bp
    retf


; ----------------------------------------------------------------------
; int fprintf(FILE *, const char *fmt, ...)
;
; Allocates a 512-byte stack scratch, far-calls _sprintf with the same
; (up to 16-word) variadic args we received, then writes the resulting
; string to the FILE's DOS handle via INT 21h AH=40.  _sprintf is in
; the same LIBSTUB_TEXT segment but uses retf in the EXE build, so the
; far-call ABI lines up.
; ----------------------------------------------------------------------
global _fprintf
_fprintf:
    push bp
    mov bp, sp
    sub sp, _FP_BUFSZ             ; scratch at [bp - _FP_BUFSZ]
    push si
    push di                       ; callee-save in cdecl
    push bx                       ; callee-save in cdecl

    ; Copy 16 words of varargs (caller frame [bp+10 .. bp+40]) onto our
    ; stack in cdecl right-to-left order so _sprintf reads the same
    ; values we received.  Reading past the caller's real varargs is
    ; harmless: _sprintf only consumes what %fmt directs.
    mov cx, 16
.fpr_pusharg:
    mov si, bp
    mov ax, cx
    shl ax, 1
    add si, ax                    ; bp + cx*2
    add si, 8                     ; first vararg at bp+10 = bp + 1*2 + 8
    push word [si]
    loop .fpr_pusharg

    push word [bp+8]              ; fmt
    lea ax, [bp - _FP_BUFSZ]
    push ax                       ; dest buffer
    call far _sprintf
    add sp, 4 + 32                ; pop dest + fmt + 16 vararg words

    ; Measure the formatted string.
    lea si, [bp - _FP_BUFSZ]
    mov dx, si                    ; DS:DX -> buf
    xor cx, cx
.fpr_strlen:
    cmp byte [si], 0
    je .fpr_strlen_done
    inc si
    inc cx
    jmp .fpr_strlen
.fpr_strlen_done:
    test cx, cx
    jz .fpr_ok                    ; empty output -> nothing to write

    ; Write CX bytes from DS:DX to FILE's handle.
    mov si, [bp+6]                ; FILE*
    mov bx, [si]                  ; handle
    mov ah, 0x40
    int 0x21
    jc .fpr_err

.fpr_ok:
    mov ax, cx                    ; return bytes written
    jmp .fpr_done
.fpr_err:
    mov ax, -1
.fpr_done:
    pop bx
    pop di
    pop si
    mov sp, bp
    pop bp
    retf


; ----------------------------------------------------------------------
; int printf(const char *fmt, ...)
;
; Same as fprintf(stdout, fmt, ...).  Caller frame layout differs by
; one slot (no leading FILE* arg) so the vararg base and fmt offsets
; shift by 2.
; ----------------------------------------------------------------------
global _printf
_printf:
    push bp
    mov bp, sp
    sub sp, _FP_BUFSZ
    push si
    push di                       ; callee-save in cdecl
    push bx                       ; callee-save in cdecl

    mov cx, 16
.pr_pusharg:
    mov si, bp
    mov ax, cx
    shl ax, 1
    add si, ax
    add si, 6                     ; first vararg at bp+8 = bp + 1*2 + 6
    push word [si]
    loop .pr_pusharg

    push word [bp+6]              ; fmt
    lea ax, [bp - _FP_BUFSZ]
    push ax
    call far _sprintf
    add sp, 4 + 32

    lea si, [bp - _FP_BUFSZ]
    mov dx, si
    xor cx, cx
.pr_strlen:
    cmp byte [si], 0
    je .pr_strlen_done
    inc si
    inc cx
    jmp .pr_strlen
.pr_strlen_done:
    test cx, cx
    jz .pr_ok

    mov bx, 1                     ; stdout
    mov ah, 0x40
    int 0x21
    jc .pr_err

.pr_ok:
    mov ax, cx
    jmp .pr_done
.pr_err:
    mov ax, -1
.pr_done:
    pop bx
    pop di
    pop si
    mov sp, bp
    pop bp
    retf


; ----------------------------------------------------------------------
; int far_printf(const char __far *fmt, ...)  — compact/large/huge entry.
; int far_fprintf(FILE *stream, const char __far *fmt, ...)
;
; In far-data memory models a pointer occupies 4 stack bytes (offset
; then segment).  minic mangles `printf` -> `far_printf` (asm symbol
; `_far_printf`) when memmodel ∈ {compact, large, huge}.  Both
; functions delegate to `_far_sprintf` (defined below), which writes
; into a DGROUP-local scratch buffer and correctly consumes 4-byte far
; %s / %p arguments.
; ----------------------------------------------------------------------
%define _FMT_BUFSZ 128                ; legacy reserve (unused after _far_sprintf)

global _far_fprintf
_far_fprintf:
    push bp
    mov bp, sp
    sub sp, _FP_BUFSZ + _FMT_BUFSZ
    push si
    push di
    push bx
    push es

    ; FILE* at [bp+6]:[bp+8] (4 bytes — caller pushes 4 bytes for a
    ; FILE* under far-data even though only the .off is meaningful, since
    ; FILE.seg is always SS=DGROUP).  Far fmt at [bp+10]:[bp+12], first
    ; vararg at [bp+14].  Push 16 words of varargs (right-to-left), then
    ; call _far_sprintf with the original far fmt (it does its own DGROUP
    ; scratch copy) and a DGROUP-local far dest (seg=SS, off=local
    ; output buffer).
    mov cx, 16
.ffpr_pusharg:
    mov si, bp
    mov ax, cx
    shl ax, 1
    add si, ax
    add si, 12                    ; first vararg at bp+14 = bp + 1*2 + 12
    push word [ss:si]             ; caller stack (ss: — split-stack safe)
    loop .ffpr_pusharg

    ; Push fmt (far, as-is from caller)
    push word [bp+12]             ; fmt.seg
    push word [bp+10]             ; fmt.off
    ; Push dest as far ptr: seg=SS, off=local output buffer.  The buffer
    ; is [bp-N] stack storage, so SS IS its segment (also correct under
    ; --separate-stack, where SS != DGROUP).
    mov ax, ss
    push ax                       ; dest.seg
    lea ax, [bp - _FP_BUFSZ]
    push ax                       ; dest.off
    call far _far_sprintf
    add sp, 8 + 32                ; 4 (dest far) + 4 (fmt far) + 32 (varargs)

    lea si, [bp - _FP_BUFSZ]
    mov dx, si
    xor cx, cx
.ffpr_strlen:
    cmp byte [ss:si], 0           ; output buffer is stack-resident
    je .ffpr_strlen_done
    inc si
    inc cx
    jmp .ffpr_strlen
.ffpr_strlen_done:
    test cx, cx
    jz .ffpr_ok

    mov si, [bp+6]                ; FILE*.off (slot lives in DGROUP)
    mov bx, [si]                  ; handle (DS=DGROUP)
    push ds
    push ss
    pop ds                        ; DS=SS: AH=40h buf is stack-resident
    mov ah, 0x40
    int 0x21
    pop ds                        ; (pop does not touch CF)
    jc .ffpr_err

.ffpr_ok:
    mov ax, cx
    jmp .ffpr_done
.ffpr_err:
    mov ax, -1
.ffpr_done:
    pop es
    pop bx
    pop di
    pop si
    mov sp, bp
    pop bp
    retf


global _far_printf
_far_printf:
    push bp
    mov bp, sp
    sub sp, _FP_BUFSZ + _FMT_BUFSZ
    push si
    push di
    push bx
    push es

    ; Far fmt at [bp+6]:[bp+8], first vararg at [bp+10].
    mov cx, 16
.fpr_pusharg:
    mov si, bp
    mov ax, cx
    shl ax, 1
    add si, ax
    add si, 8                     ; first vararg at bp+10 = bp + 1*2 + 8
    push word [ss:si]             ; caller stack (ss: — split-stack safe)
    loop .fpr_pusharg

    ; Push fmt (far, as-is from caller)
    push word [bp+8]              ; fmt.seg
    push word [bp+6]              ; fmt.off
    ; Push dest as far ptr: seg=SS, off=local output buffer
    mov ax, ss
    push ax
    lea ax, [bp - _FP_BUFSZ]
    push ax
    call far _far_sprintf
    add sp, 8 + 32

    lea si, [bp - _FP_BUFSZ]
    mov dx, si
    xor cx, cx
.fpr_strlen:
    cmp byte [ss:si], 0           ; output buffer is stack-resident
    je .fpr_strlen_done
    inc si
    inc cx
    jmp .fpr_strlen
.fpr_strlen_done:
    test cx, cx
    jz .fpr_ok

    mov bx, 1                     ; stdout
    push ds
    push ss
    pop ds                        ; DS=SS: AH=40h buf is stack-resident
    mov ah, 0x40
    int 0x21
    pop ds                        ; (pop does not touch CF)
    jc .fpr_err

.fpr_ok:
    mov ax, cx
    jmp .fpr_done
.fpr_err:
    mov ax, -1
.fpr_done:
    pop es
    pop bx
    pop di
    pop si
    mov sp, bp
    pop bp
    retf


; ----------------------------------------------------------------------
; int rename(const char *old, const char *new)  — INT 21h AH=56.
; Both names must be in DGROUP (DS=ES).
; ----------------------------------------------------------------------
global _rename
_rename:
    push bp
    mov bp, sp
    push di
    push es

    push ds
    pop es                        ; ES = DS = DGROUP
    mov dx, [bp+6]                ; DS:DX = old
    mov di, [bp+8]                ; ES:DI = new
    mov ah, 0x56
    int 0x21
    jc .rn_err
    xor ax, ax
    jmp .rn_done
.rn_err:
    mov ax, -1
.rn_done:
    pop es
    pop di
    pop bp
    retf


; ----------------------------------------------------------------------
; int remove(const char *name)  — INT 21h AH=41.
; ----------------------------------------------------------------------
global _remove
_remove:
    push bp
    mov bp, sp
    mov dx, [bp+6]
    mov ah, 0x41
    int 0x21
    jc .rm_err
    xor ax, ax
    pop bp
    retf
.rm_err:
    mov ax, -1
    pop bp
    retf


segment _BSS
_file_slots: resb (_NUM_FILES * _FILE_SZ)
"""


# ----------------------------------------------------------------------------
# _far_sprintf — sprintf with far dest, far fmt, and far %s/%p source pointers.
#
# Far-data memory models (compact/large/huge) have minic mangle `sprintf` to
# `_far_sprintf` and pass every char-pointer argument as a 4-byte far ptr.
# _far_sprintf is structurally identical to _sprintf's format engine but:
#   - dest is far: output via `mov [es:di], al` (we set ES = dest.seg)
#   - fmt is far: copied to a DGROUP scratch buffer up front, then walked
#     with DS=DGROUP (so [_spr_*] state variables remain reachable)
#   - %s consumes 4 stack bytes (off:seg); during the string copy we swap
#     DS to src.seg and access [_spr_width]/[_spr_flags] via `ss:` override
#   - %p consumes 4 stack bytes and prints as 32-bit hex (always 'l')
#   - calls into shared `_spr_emit_int` for numeric specifiers — that helper
#     emits via stosb (ES:DI) which is exactly what we need
#
# Numeric specifier args (%d/%u/%x/%o) remain 2-byte; %ld/%lu/%lx/%lo remain
# 4-byte. %c stays 2-byte.
# ----------------------------------------------------------------------------
FAR_SPRINTF_EXE = """

segment _BSS
; Static DGROUP scratch for the far fmt copy.  Was a [bp-N] stack buffer,
; but the engine reads it with `lodsb` (DS:SI) — DS-relative — which only
; reached the stack while SS==DS.  A DGROUP static keeps the DS-relative
; engine correct under --separate-stack.  printf state (_spr_*) is global
; already, so libstub printf was never reentrant; this changes nothing.
_fsp_fmtbuf: resb 256

segment LIBSTUB_TEXT

%define _FSP_FMTBUF_SZ 256

global _far_sprintf
_far_sprintf:
    push bp
    mov bp, sp
    push si
    push di
    push bx
    push es
    push ds

    ; Stack: [bp+6..9] dest (far), [bp+10..13] fmt (far), [bp+14] varargs.

    ; --- Copy far fmt into the DGROUP scratch buffer ---
    mov si, [bp+10]                 ; fmt.off
    mov ax, [bp+12]                 ; fmt.seg
    mov es, ax
    mov di, _fsp_fmtbuf
.fsp_cpfmt:
    mov al, [es:si]
    mov [di], al                    ; DS=DGROUP, scratch is DGROUP _BSS
    inc si
    inc di
    test al, al
    jnz .fsp_cpfmt

    ; --- Engine register setup ---
    mov di, [bp+6]                  ; DI = dest.off
    mov ax, [bp+8]
    mov es, ax                      ; ES = dest.seg  (stosb → ES:DI = far dest)
    mov si, _fsp_fmtbuf             ; SI = fmt scratch (DGROUP)
    lea bx, [bp+14]                 ; BX = vararg ptr (caller stack — every
                                    ; [bx] read below uses ss:)

.fsp_top:
    lodsb
    test al, al
    jz .fsp_done
    cmp al, '%'
    je .fsp_pct
    stosb
    jmp .fsp_top

.fsp_pct:
    mov word [_spr_flags], 0
    mov word [_spr_width], 0
    mov word [_spr_prec], 0

.fsp_pf:
    lodsb
    cmp al, '-'
    jne .fsp_pf_nminus
    or word [_spr_flags], 1
    jmp .fsp_pf
.fsp_pf_nminus:
    cmp al, '0'
    jne .fsp_pf_nzero
    or word [_spr_flags], 2
    jmp .fsp_pf
.fsp_pf_nzero:
    cmp al, '+'
    je .fsp_pf
    cmp al, ' '
    je .fsp_pf
    cmp al, '#'
    je .fsp_pf

.fsp_pw:
    cmp al, '0'
    jb .fsp_pw_done
    cmp al, '9'
    ja .fsp_pw_done
    push ax
    mov ax, [_spr_width]
    mov cx, 10
    mul cx
    mov cx, ax
    pop ax
    sub al, '0'
    xor ah, ah
    add cx, ax
    mov [_spr_width], cx
    lodsb
    jmp .fsp_pw
.fsp_pw_done:

    cmp al, '.'
    jne .fsp_pp_done
    or word [_spr_flags], 8
    lodsb
.fsp_pp:
    cmp al, '0'
    jb .fsp_pp_done
    cmp al, '9'
    ja .fsp_pp_done
    push ax
    mov ax, [_spr_prec]
    mov cx, 10
    mul cx
    mov cx, ax
    pop ax
    sub al, '0'
    xor ah, ah
    add cx, ax
    mov [_spr_prec], cx
    lodsb
    jmp .fsp_pp
.fsp_pp_done:

    cmp al, 'l'
    jne .fsp_lm_h
    or word [_spr_flags], 4
    lodsb
    cmp al, 'l'
    jne .fsp_lm_done
    lodsb
    jmp .fsp_lm_done
.fsp_lm_h:
    cmp al, 'h'
    jne .fsp_lm_done
    lodsb
    cmp al, 'h'
    jne .fsp_lm_done
    lodsb
.fsp_lm_done:

    test al, al
    jz .fsp_done
    cmp al, '%'
    je .fsp_emit_pct
    cmp al, 'c'
    je .fsp_do_chr
    cmp al, 's'
    je .fsp_do_str
    cmp al, 'd'
    je .fsp_do_signed
    cmp al, 'i'
    je .fsp_do_signed
    cmp al, 'u'
    je .fsp_do_unsigned
    cmp al, 'x'
    je .fsp_do_hex_lo
    cmp al, 'X'
    je .fsp_do_hex_up
    cmp al, 'o'
    je .fsp_do_octal
    cmp al, 'p'
    je .fsp_do_ptr
    stosb
    jmp .fsp_top

.fsp_emit_pct:
    stosb
    jmp .fsp_top

.fsp_done:
    mov byte [es:di], 0
    xor ax, ax
    pop ds
    pop es
    pop bx
    pop di
    pop si
    mov sp, bp
    pop bp
    retf

    ; ---- %c ----
.fsp_do_chr:
    mov ax, [ss:bx]
    add bx, 2
    push ax
    mov cx, [_spr_width]
    cmp cx, 1
    jbe .fsp_chr_emit
    dec cx
    test word [_spr_flags], 1
    jnz .fsp_chr_left
    mov al, ' '
    rep stosb
    pop ax
    stosb
    jmp .fsp_top
.fsp_chr_left:
    pop ax
    stosb
    mov al, ' '
    rep stosb
    jmp .fsp_top
.fsp_chr_emit:
    pop ax
    stosb
    jmp .fsp_top

    ; ---- %s (4-byte far ptr arg) ----
.fsp_do_str:
    push si                         ; save fmt scratch position

    ; Snapshot precision cap into CX while DS=DGROUP
    mov cx, 0x7FFF
    test word [_spr_flags], 8
    jz .fsp_str_have_cap
    mov cx, [_spr_prec]
.fsp_str_have_cap:

    mov si, [ss:bx]                 ; src.off (vararg on caller stack)
    mov ax, [ss:bx+2]               ; src.seg
    add bx, 4

    push ds
    mov ds, ax                      ; DS = src.seg (subsequent [si] reads from far)

    ; Scan length up to NUL or cap
    push si                         ; save src start
    xor dx, dx                      ; length
.fsp_str_scan:
    test cx, cx
    jz .fsp_str_scan_done
    cmp byte [si], 0
    je .fsp_str_scan_done
    inc si
    inc dx
    dec cx
    jmp .fsp_str_scan
.fsp_str_scan_done:
    pop si                          ; SI = src.off (start), DX = length

    ; Read DGROUP state vars through a brief DS=DGROUP window (DS =
    ; src.seg right now).  The old `[ss:label]` override only reached
    ; DGROUP while SS==DS — wrong under --separate-stack.
    push ds
    mov ds, [cs:_dgroup_para]
    mov cx, [_spr_width]
    mov ax, [_spr_flags]
    pop ds                          ; DS = src.seg again
    cmp cx, dx
    jbe .fsp_str_no_pad
    sub cx, dx
    test ax, 1
    jnz .fsp_str_pad_after

    ; pad before
    push si
    push dx
    mov al, ' '
    rep stosb
    pop cx
    pop si
    rep movsb                       ; ES:DI ← DS:SI
    pop ds
    pop si
    jmp .fsp_top

.fsp_str_pad_after:
    push cx
    mov cx, dx
    rep movsb
    pop cx
    mov al, ' '
    rep stosb
    pop ds
    pop si
    jmp .fsp_top

.fsp_str_no_pad:
    mov cx, dx
    rep movsb
    pop ds
    pop si
    jmp .fsp_top

    ; ---- %d / %i ----
.fsp_do_signed:
    test word [_spr_flags], 4
    jnz .fsp_sgn_long
    mov ax, [ss:bx]
    add bx, 2
    cwd
    jmp .fsp_sgn_common
.fsp_sgn_long:
    mov ax, [ss:bx]
    mov dx, [ss:bx+2]
    add bx, 4
.fsp_sgn_common:
    mov byte [_spr_signc], 0
    test dx, dx
    jns .fsp_sgn_pos
    mov byte [_spr_signc], '-'
    not dx
    neg ax
    sbb dx, -1
.fsp_sgn_pos:
    mov cx, 10
    call _spr_emit_int
    jmp .fsp_top

    ; ---- %u ----
.fsp_do_unsigned:
    test word [_spr_flags], 4
    jnz .fsp_uns_long
    mov ax, [ss:bx]
    add bx, 2
    xor dx, dx
    jmp .fsp_uns_common
.fsp_uns_long:
    mov ax, [ss:bx]
    mov dx, [ss:bx+2]
    add bx, 4
.fsp_uns_common:
    mov byte [_spr_signc], 0
    mov cx, 10
    call _spr_emit_int
    jmp .fsp_top

    ; ---- %x ----
.fsp_do_hex_lo:
    and word [_spr_flags], 0xFFEF
    jmp .fsp_hex_dispatch
.fsp_do_hex_up:
    or word [_spr_flags], 16
.fsp_hex_dispatch:
    test word [_spr_flags], 4
    jnz .fsp_hex_long
    mov ax, [ss:bx]
    add bx, 2
    xor dx, dx
    jmp .fsp_hex_common
.fsp_hex_long:
    mov ax, [ss:bx]
    mov dx, [ss:bx+2]
    add bx, 4
.fsp_hex_common:
    mov byte [_spr_signc], 0
    mov cx, 16
    call _spr_emit_int
    jmp .fsp_top

    ; ---- %p (always 4-byte far ptr, printed as 32-bit hex) ----
.fsp_do_ptr:
    or word [_spr_flags], 4         ; force 'l' (long) path
    or word [_spr_flags], 2         ; zero-fill
    and word [_spr_flags], 0xFFEF   ; lowercase hex
    mov word [_spr_width], 8        ; pad to 8 hex digits
    mov ax, [ss:bx]
    mov dx, [ss:bx+2]
    add bx, 4
    mov byte [_spr_signc], 0
    mov cx, 16
    call _spr_emit_int
    jmp .fsp_top

    ; ---- %o ----
.fsp_do_octal:
    test word [_spr_flags], 4
    jnz .fsp_oct_long
    mov ax, [ss:bx]
    add bx, 2
    xor dx, dx
    jmp .fsp_oct_common
.fsp_oct_long:
    mov ax, [ss:bx]
    mov dx, [ss:bx+2]
    add bx, 4
.fsp_oct_common:
    mov byte [_spr_signc], 0
    mov cx, 8
    call _spr_emit_int
    jmp .fsp_top
"""

# ----------------------------------------------------------------------------
# Far-data DOS API + stdio helpers.
#
# Under compact/large/huge, all data pointers are 4 bytes (off:seg).  The
# unmangled _intdos/_int86/_segread in libstub.asm consume *near* pointers
# (2 bytes), so the caller pushes 4 bytes per ptr and the helpers read
# garbage.  Same shape for _fputs/_fputc/_fgets and _puts (the latter has
# no near impl yet — it's added here).
#
# The frontend mangles intdos/int86/segread/puts/fputs/fputc/fgets to
# _far_X in far-data models via the far_stdlib[] list in minic.y.  Each
# far helper saves the caller's ES, points ES:BX into the relevant struct,
# performs the operation, then restores ES.  When two distinct far args
# are involved (e.g. intdos's in/out) we swap ES between them via push/pop.
#
# Stack-arg offsets are written for a far call (4-byte return address),
# so the first arg lives at [bp+6].
# ----------------------------------------------------------------------------
FAR_DOSIO_EXE = """

segment LIBSTUB_TEXT

; ----------------------------------------------------------------------
; int far_intdos(union REGS __far *in, union REGS __far *out)
;
; Stack: [bp+6] in.off, [bp+8] in.seg, [bp+10] out.off, [bp+12] out.seg.
; ----------------------------------------------------------------------
global _far_intdos
_far_intdos:
    push bp
    mov bp, sp
    push si
    push di
    push bx
    push es

    mov ax, [bp+8]                ; in.seg
    mov es, ax
    mov bx, [bp+6]                ; ES:BX -> in
    mov ax, [es:bx+0]
    mov cx, [es:bx+4]
    mov dx, [es:bx+6]
    mov si, [es:bx+8]
    mov di, [es:bx+10]
    mov bx, [es:bx+2]             ; BX = in.bx (in ptr discarded)
    int 0x21

    push bx                       ; save call-result BX
    mov bx, [bp+12]               ; out.seg
    mov es, bx
    mov bx, [bp+10]               ; ES:BX -> out
    mov [es:bx+0], ax
    pop ax
    mov [es:bx+2], ax
    mov [es:bx+4], cx
    mov [es:bx+6], dx
    mov [es:bx+8], si
    mov [es:bx+10], di
    pushf
    pop ax
    mov [es:bx+14], ax
    and ax, 1
    mov [es:bx+12], ax
    mov ax, [es:bx+0]             ; return = call AX

    pop es
    pop bx
    pop di
    pop si
    pop bp
    retf


; ----------------------------------------------------------------------
; int far_int86(int intno, union REGS __far *in, union REGS __far *out)
;
; Stack: [bp+6] intno, [bp+8] in.off, [bp+10] in.seg,
;        [bp+12] out.off, [bp+14] out.seg.
;
; Patches the INT immediate via self-modifying code (same idiom as
; _int86 in libstub.asm).
; ----------------------------------------------------------------------
global _far_int86
_far_int86:
    push bp
    mov bp, sp
    push si
    push di
    push bx
    push es

    mov ax, [bp+6]
    mov [cs:.fi86_op+1], al

    mov ax, [bp+10]               ; in.seg
    mov es, ax
    mov bx, [bp+8]                ; ES:BX -> in
    mov ax, [es:bx+0]
    mov cx, [es:bx+4]
    mov dx, [es:bx+6]
    mov si, [es:bx+8]
    mov di, [es:bx+10]
    mov bx, [es:bx+2]
.fi86_op:
    int 0x21

    push bx
    mov bx, [bp+14]               ; out.seg
    mov es, bx
    mov bx, [bp+12]               ; ES:BX -> out
    mov [es:bx+0], ax
    pop ax
    mov [es:bx+2], ax
    mov [es:bx+4], cx
    mov [es:bx+6], dx
    mov [es:bx+8], si
    mov [es:bx+10], di
    pushf
    pop ax
    mov [es:bx+14], ax
    and ax, 1
    mov [es:bx+12], ax
    mov ax, [es:bx+0]

    pop es
    pop bx
    pop di
    pop si
    pop bp
    retf


; ----------------------------------------------------------------------
; int far_puts(const char __far *s)
;
; Writes s followed by CR+LF to stdout (DOS handle 1).  Returns 0 on
; success, -1 on error.  Mirrors a near _puts but there is no near
; _puts in libstub today; this far variant is the only impl, so any
; caller of puts() under any model needs it.  Reads s via ES:SI; swaps
; DS to s.seg briefly for the AH=40h call, restores DS=DGROUP after.
;
; Stack: [bp+6] s.off, [bp+8] s.seg.
; ----------------------------------------------------------------------
global _far_puts
_far_puts:
    push bp
    mov bp, sp
    push si
    push di
    push bx
    push es
    push ds

    mov ax, [bp+8]                ; s.seg
    mov es, ax
    mov si, [bp+6]                ; ES:SI -> s
    mov di, si
.fps_len:
    cmp byte [es:di], 0
    je .fps_len_done
    inc di
    jmp .fps_len
.fps_len_done:
    mov cx, di
    sub cx, si                    ; CX = strlen
    jz .fps_nl                    ; empty body, just emit newline

    ; AH=40h needs DS:DX -> buf.  Swap DS to ES (= s.seg) temporarily.
    mov ax, es
    mov ds, ax
    mov dx, si                    ; DS:DX = (s.seg):(s.off)
    mov bx, 1                     ; stdout handle
    mov ah, 0x40
    int 0x21
    mov ds, [cs:_dgroup_para]     ; restore DS = DGROUP (split-stack safe)
    jc .fps_err

.fps_nl:
    ; Emit CR LF via a stack-resident 2-byte buffer.  The buffer lives on
    ; the stack, so AH=40h's DS:DX needs DS = SS (NOT DGROUP — distinct
    ; under --separate-stack).  Caller DS is restored by the exit pops.
    ; 8086 has no `push imm16`, stage via AX.
    mov ax, 0x0A0D                ; little-endian: byte 0x0D (CR), byte 0x0A (LF)
    push ax
    mov dx, sp                    ; SS:DX -> the pushed word
    push ss
    pop ds                        ; DS = SS for the stack-resident buffer
    mov bx, 1
    mov cx, 2
    mov ah, 0x40
    int 0x21
    pop ax                        ; discard scratch word (flags preserved)
    jc .fps_err

    xor ax, ax                    ; success
    jmp .fps_done
.fps_err:
    mov ax, -1
.fps_done:
    pop ds
    pop es
    pop bx
    pop di
    pop si
    pop bp
    retf


; ----------------------------------------------------------------------
; void far_segread(struct SREGS __far *segs)
;
; Stack: [bp+6] segs.off, [bp+8] segs.seg.
;
; We need to read ES BEFORE overwriting it to point at segs, so we stash
; the caller's ES in SI (callee-save) first.
; ----------------------------------------------------------------------
global _far_segread
_far_segread:
    push bp
    mov bp, sp
    push bx
    push si
    push es

    push es
    pop si                        ; SI = caller's ES (pre-overwrite)

    mov ax, [bp+8]                ; segs.seg
    mov es, ax
    mov bx, [bp+6]                ; ES:BX -> segs

    mov [es:bx+0], si             ; segs.es = caller's ES
    mov [es:bx+2], cs
    mov [es:bx+4], ss
    mov [es:bx+6], ds

    pop es
    pop si
    pop bx
    pop bp
    retf
"""


# ----------------------------------------------------------------------------
# Far-data stdio: 4-byte FILE* sentinels + _far_fopen/_far_fclose/
# _far_fputs/_far_fputc/_far_fgets.
#
# Under compact/large/huge, the C variable `FILE *` is 4 bytes (off:seg).
# All FILE slots live in DGROUP (the _file_slots pool from FILEIO_EXE), so
# FILE*.seg always equals SS (= DGROUP at runtime in our EXE layout).  The
# helpers ignore FILE*.seg and read the handle via FILE*.off from the
# current DS=DGROUP.
#
# stdin/stdout/stderr replace the 2-byte sentinels in libstub.asm; the
# corresponding 1-word `_stdin_file`/`_stdout_file`/`_stderr_file` storage
# is re-emitted here (the libstub.asm versions are eaten by the SKIP_GLOBALS
# region that starts at `global _stdin, _stdout, _stderr`).
#
# _far_fopen returns DX:AX where AX = slot offset in DGROUP, DX = SS.  The
# helpers below mirror the FILEIO_EXE near-data versions byte-for-byte
# except for: 4-byte arg layout, ES:BX-loaded reads of name/mode/string,
# DS-swap for the AH=40h/AH=3C/AH=3D calls (whose DS:DX must point at the
# far source), and DX:AX returns.
# ----------------------------------------------------------------------------
FAR_STDIO_EXE = """

; -------- far-data stdio (compact/large/huge) --------

segment _DATA

; 4-byte stdio sentinels: off:seg pointing at the matching _stdin_file etc.
; `seg X` emits a 16-bit segment fixup that omf_link resolves to a runtime
; relocation in the MZ header.
global _stdin, _stdout, _stderr
_stdin:  dw _stdin_file,  seg _stdin_file
_stdout: dw _stdout_file, seg _stdout_file
_stderr: dw _stderr_file, seg _stderr_file
_stdin_file:  dw 0
_stdout_file: dw 1
_stderr_file: dw 2

segment LIBSTUB_TEXT

; ----------------------------------------------------------------------
; FILE *far_fopen(const char __far *name, const char __far *mode)
;
; Stack: [bp+6] name.off, [bp+8] name.seg,
;        [bp+10] mode.off, [bp+12] mode.seg.
;
; Returns DX:AX where AX = slot offset in DGROUP, DX = SS (= DGROUP).
; Returns 0:0 on failure.
; ----------------------------------------------------------------------
global _far_fopen
_far_fopen:
    push bp
    mov bp, sp
    push si
    push di
    push bx
    push es
    push ds

    ; Find a free FILE slot (DS=DGROUP still).
    mov si, _file_slots
    mov di, _NUM_FILES
.ffop_find:
    cmp byte [si + 2], 0          ; in_use?
    je .ffop_found
    add si, _FILE_SZ
    dec di
    jnz .ffop_find
    xor ax, ax                    ; no free slot -> NULL
    xor dx, dx
    jmp .ffop_done

.ffop_found:
    ; Inspect mode[0] via ES:BX -> mode.
    mov ax, [bp+12]               ; mode.seg
    mov es, ax
    mov bx, [bp+10]               ; mode.off
    mov al, [es:bx]
    cmp al, 'r'
    je .ffop_read
    cmp al, 'w'
    je .ffop_write
    cmp al, 'a'
    je .ffop_append
    xor ax, ax                    ; unknown mode -> NULL
    xor dx, dx
    jmp .ffop_done

.ffop_read:
    ; AH=3D AL=00: open existing, read-only.  DS:DX -> name (far).
    mov ax, [bp+8]                ; name.seg
    mov ds, ax
    mov dx, [bp+6]                ; name.off
    mov ax, 0x3D00
    int 0x21
    mov ds, [cs:_dgroup_para]     ; restore DS=DGROUP (split-stack safe)
    jc .ffop_fail
    mov byte [si+3], 0            ; flags: read mode
    jmp .ffop_install

.ffop_write:
    mov ax, [bp+8]
    mov ds, ax
    mov dx, [bp+6]
    xor cx, cx                    ; attribute = normal
    mov ah, 0x3C                  ; create / truncate
    int 0x21
    mov ds, [cs:_dgroup_para]     ; restore DS=DGROUP (split-stack safe)
    jc .ffop_fail
    mov byte [si+3], 2            ; flags: bit1 = writing
    jmp .ffop_install

.ffop_append:
    ; Try to open existing for read+write; if missing, create.
    mov ax, [bp+8]
    mov ds, ax
    mov dx, [bp+6]
    mov ax, 0x3D02
    int 0x21
    mov ds, [cs:_dgroup_para]     ; restore DS=DGROUP (split-stack safe)
    jnc .ffop_seek_end
    ; Create new (file didn't exist).
    mov ax, [bp+8]
    mov ds, ax
    mov dx, [bp+6]
    xor cx, cx
    mov ah, 0x3C
    int 0x21
    mov ds, [cs:_dgroup_para]     ; restore DS=DGROUP (split-stack safe)
    jc .ffop_fail
    mov byte [si+3], 2
    jmp .ffop_install

.ffop_seek_end:
    ; AX = handle.  Seek to EOF: AH=42 AL=2, CX:DX = 0.
    mov bx, ax
    xor cx, cx
    xor dx, dx
    mov ax, 0x4202
    int 0x21
    mov ax, bx                    ; restore handle into AX
    mov byte [si+3], 2

.ffop_install:
    mov [si], ax                  ; handle
    mov byte [si+2], 1            ; in_use
    mov word [si+4], 0            ; buf_pos
    mov word [si+6], 0            ; buf_len
    mov ax, si                    ; AX = slot offset in DGROUP
    mov dx, [cs:_dgroup_para]     ; DX = DGROUP segment (slot lives in
                                  ; DGROUP; NOT ss — split-stack safe)
    jmp .ffop_done

.ffop_fail:
    xor ax, ax
    xor dx, dx

.ffop_done:
    pop ds
    pop es
    pop bx
    pop di
    pop si
    pop bp
    retf


; ----------------------------------------------------------------------
; int far_fclose(FILE __far *fp)
;
; Stack: [bp+6] fp.off, [bp+8] fp.seg (ignored — slots are in DGROUP).
; ----------------------------------------------------------------------
global _far_fclose
_far_fclose:
    push bp
    mov bp, sp
    push bx
    push si

    mov si, [bp+6]                ; fp.off
    test si, si
    jz .ffc_done

    ; Only close if this is a real slot from our pool.
    cmp si, _file_slots
    jb .ffc_done
    mov ax, _file_slots
    add ax, (_NUM_FILES * _FILE_SZ)
    cmp si, ax
    jae .ffc_done

    cmp byte [si+2], 0            ; in_use?
    je .ffc_done

    mov bx, [si]                  ; handle
    mov byte [si+2], 0            ; free slot
    mov ah, 0x3E                  ; close handle
    int 0x21

.ffc_done:
    xor ax, ax
    pop si
    pop bx
    pop bp
    retf


; ----------------------------------------------------------------------
; int far_fputc(int c, FILE __far *fp)
;
; Stack: [bp+6] c, [bp+8] fp.off, [bp+10] fp.seg.
; ----------------------------------------------------------------------
global _far_fputc
_far_fputc:
    push bp
    mov bp, sp
    push bx
    push si

    mov si, [bp+8]                ; fp.off (FILE slot or stdio sentinel)
    mov bx, [si]                  ; handle (DS=DGROUP at entry)
    mov ax, [bp+6]                ; c (low byte)
    push ax                       ; scratch 1-byte buffer on stack
    mov dx, sp                    ; SS:DX -> our byte
    push ds
    push ss
    pop ds                        ; DS = SS: buffer is stack-resident
                                  ; (≠ DGROUP under --separate-stack)
    mov cx, 1
    mov ah, 0x40
    int 0x21
    pop ds                        ; (pop does not touch CF)
    pop ax                        ; discard scratch
    jc .ffpc_err

    mov ax, [bp+6]                ; return the char written
    and ax, 0xFF
    jmp .ffpc_done
.ffpc_err:
    mov ax, -1
.ffpc_done:
    pop si
    pop bx
    pop bp
    retf


; ----------------------------------------------------------------------
; int far_fputs(const char __far *s, FILE __far *fp)
;
; Stack: [bp+6] s.off, [bp+8] s.seg,
;        [bp+10] fp.off, [bp+12] fp.seg (ignored).
;
; Reads s via ES:DI to measure length, then swaps DS to s.seg for the
; AH=40h call (whose DS:DX must point at the source string).
; ----------------------------------------------------------------------
global _far_fputs
_far_fputs:
    push bp
    mov bp, sp
    push si
    push di
    push bx
    push es
    push ds

    ; Read handle from FILE slot (in DGROUP) while DS=DGROUP.
    mov si, [bp+10]               ; fp.off
    mov bx, [si]                  ; handle

    ; Measure strlen via ES:DI on s (far).
    mov ax, [bp+8]                ; s.seg
    mov es, ax
    mov si, [bp+6]                ; ES:SI -> s
    mov di, si
.ffps_len:
    cmp byte [es:di], 0
    je .ffps_len_done
    inc di
    jmp .ffps_len
.ffps_len_done:
    mov cx, di
    sub cx, si                    ; CX = strlen
    jz .ffps_ok                   ; empty string still succeeds

    ; AH=40h needs DS:DX -> buf.  Swap DS to s.seg (= ES) temporarily.
    mov ax, es
    mov ds, ax
    mov dx, si                    ; DS:DX = (s.seg):(s.off)
    mov ah, 0x40
    int 0x21
    mov ds, [cs:_dgroup_para]     ; restore DS=DGROUP (split-stack safe)
    jc .ffps_err

.ffps_ok:
    xor ax, ax                    ; success (non-negative)
    jmp .ffps_done
.ffps_err:
    mov ax, -1
.ffps_done:
    pop ds
    pop es
    pop bx
    pop di
    pop si
    pop bp
    retf


; ----------------------------------------------------------------------
; char *far_fgets(char __far *buf, int n, FILE __far *fp)
;
; Stack: [bp+6] buf.off, [bp+8] buf.seg,
;        [bp+10] n,
;        [bp+12] fp.off, [bp+14] fp.seg.
;
; Returns DX:AX = buf (success) or 0:0 (EOF, no bytes read).
;
; Inlined slot-buffered reader mirroring `_getc` in FILEIO_EXE.  Writes
; output bytes via ES:DI on the caller's far buf.
; ----------------------------------------------------------------------
global _far_fgets
_far_fgets:
    push bp
    mov bp, sp
    push bx
    push si
    push di
    push es

    ; Validate FILE slot belongs to our pool (stdin handle 0 would be a
    ; sentinel pointer, which can't refill via the slot buffer; we treat
    ; it as EOF — fgets on stdin isn't supported by this implementation).
    mov si, [bp+12]               ; fp.off
    test si, si
    jz .ffg_null
    cmp si, _file_slots
    jb .ffg_null
    mov ax, _file_slots
    add ax, (_NUM_FILES * _FILE_SZ)
    cmp si, ax
    jae .ffg_null

    mov cx, [bp+10]               ; n
    cmp cx, 2
    jl .ffg_null                  ; n < 2 -> no room for char + NUL
    dec cx                        ; reserve byte for terminator

    mov ax, [bp+8]                ; buf.seg
    mov es, ax
    mov di, [bp+6]                ; ES:DI -> buf

.ffg_loop:
    test cx, cx
    jz .ffg_end_ok                ; filled n-1 bytes
    mov ax, [si+4]                ; buf_pos
    cmp ax, [si+6]                ; buf_len
    jb .ffg_byte

    test byte [si+3], 1           ; eof already?
    jnz .ffg_end_check

    ; Refill: INT 21h AH=3F, BX=handle, CX=count, DS:DX=buf.
    push cx
    push di
    mov bx, [si]
    mov cx, _FBUF_SZ
    mov dx, si
    add dx, 8                     ; buf at slot+8
    mov ah, 0x3F
    int 0x21
    pop di
    pop cx
    jc .ffg_set_eof
    test ax, ax
    jz .ffg_set_eof
    mov [si+6], ax                ; buf_len
    mov word [si+4], 0            ; buf_pos
    jmp .ffg_loop

.ffg_set_eof:
    or byte [si+3], 1
    jmp .ffg_end_check

.ffg_byte:
    mov bx, [si+4]                ; buf_pos
    mov al, [si + bx + 8]
    mov [es:di], al
    inc di
    inc word [si+4]
    dec cx
    cmp al, 10                    ; '\\n' -> stop after copying it
    je .ffg_end_ok
    jmp .ffg_loop

.ffg_end_check:
    ; If we've read at least one byte, return buf; else return NULL.
    cmp di, [bp+6]
    je .ffg_null

.ffg_end_ok:
    mov byte [es:di], 0           ; NUL-terminate
    mov ax, [bp+6]                ; return buf
    mov dx, [bp+8]
    jmp .ffg_done

.ffg_null:
    xor ax, ax
    xor dx, dx

.ffg_done:
    pop es
    pop di
    pop si
    pop bx
    pop bp
    retf


; ----------------------------------------------------------------------
; size_t far_fread(void __far *buf, size_t sz, size_t n, FILE __far *fp)
;
; Stack: [bp+6]  buf.off, [bp+8]  buf.seg,
;        [bp+10] sz,      [bp+12] n,
;        [bp+14] fp.off,  [bp+16] fp.seg (ignored — slot in DGROUP).
;
; Direct INT 21h AH=3F read of sz*n bytes into far buf.  Returns item
; count (= bytes / sz).  Bypasses the FILE slot's line buffer — mixing
; with _far_fgets on the same handle is UB, same as the near version.
; ----------------------------------------------------------------------
global _far_fread
_far_fread:
    push bp
    mov bp, sp
    push bx
    push si
    push ds

    mov ax, [bp+10]               ; sz
    mul word [bp+12]              ; sz*n -> DX:AX
    test dx, dx
    jnz .ffr_err                  ; > 64KB unsupported
    mov cx, ax                    ; total bytes
    test cx, cx
    jz .ffr_zero

    mov si, [bp+14]               ; fp.off
    test si, si
    jz .ffr_err
    mov bx, [si]                  ; DOS handle (DS=DGROUP still)

    ; AH=3F needs DS:DX -> buf.  Swap DS to buf.seg.
    mov ax, [bp+8]                ; buf.seg
    mov ds, ax
    mov dx, [bp+6]                ; buf.off
    mov ah, 0x3F
    int 0x21
    mov ds, [cs:_dgroup_para]     ; restore DS=DGROUP (split-stack safe)
    jc .ffr_err

    ; AX = bytes actually read.  Convert to item count.
    mov cx, [bp+10]               ; sz
    test cx, cx
    jz .ffr_done                  ; sz=0: leave AX as bytes (UB)
    xor dx, dx
    div cx                        ; AX = bytes / sz
    jmp .ffr_done
.ffr_zero:
    xor ax, ax
    jmp .ffr_done
.ffr_err:
    xor ax, ax
.ffr_done:
    pop ds
    pop si
    pop bx
    pop bp
    retf


; ----------------------------------------------------------------------
; size_t far_fwrite(const void __far *buf, size_t sz, size_t n,
;                   FILE __far *fp)
;
; Same arg layout as _far_fread.  AH=40 instead of AH=3F.
; ----------------------------------------------------------------------
global _far_fwrite
_far_fwrite:
    push bp
    mov bp, sp
    push bx
    push si
    push ds

    mov ax, [bp+10]               ; sz
    mul word [bp+12]              ; sz*n -> DX:AX
    test dx, dx
    jnz .ffw_err
    mov cx, ax                    ; total bytes
    test cx, cx
    jz .ffw_done_zero

    mov si, [bp+14]               ; fp.off
    test si, si
    jz .ffw_err
    mov bx, [si]                  ; DOS handle (DS=DGROUP still)

    ; AH=40 needs DS:DX -> buf.  Swap DS to buf.seg.
    mov ax, [bp+8]                ; buf.seg
    mov ds, ax
    mov dx, [bp+6]                ; buf.off
    mov ah, 0x40
    int 0x21
    mov ds, [cs:_dgroup_para]     ; restore DS=DGROUP (split-stack safe)
    jc .ffw_err

    ; AX = bytes actually written.  Convert to item count.
    xor dx, dx
    div word [bp+10]              ; AX = bytes / sz
    jmp .ffw_done
.ffw_done_zero:
    xor ax, ax
    jmp .ffw_done
.ffw_err:
    xor ax, ax
.ffw_done:
    pop ds
    pop si
    pop bx
    pop bp
    retf


; ----------------------------------------------------------------------
; int far_fflush(FILE __far *fp)  — no-op (writes are unbuffered).
;
; Stack: [bp+6] fp.off, [bp+8] fp.seg.
; ----------------------------------------------------------------------
global _far_fflush
_far_fflush:
    xor ax, ax
    retf
"""


# ----------------------------------------------------------------------
# setjmp / longjmp — the NLR keystone (py/nlrsetjmp.c: nlr_push uses
# setjmp((buf)->jmpbuf); nlr_jump uses longjmp(top->jmpbuf, 1)).
#
# Medium model: near data, far code.  jmp_buf is `int[8]` (16 bytes) and
# decays to a 2-byte near pointer (DGROUP/SS offset; DS==SS so a near
# pointer reaches a stack-allocated nlr_buf via DS).  This is the FAR
# call/return form (4-byte CS:IP return address, retf) and CANNOT be
# produced by the [bp+N]->[bp+N+2] / ret->retf rewrite that converts the
# near libstub.asm stubs — longjmp must restore the caller's stack and
# `jmp far` (synthesized via retf) to the saved CS:IP.
#
# Far-call frame at setjmp entry (after `push bp; mov bp,sp`):
#   [bp+0] saved BP   [bp+2] ret IP   [bp+4] ret CS   [bp+6] env
# The i8086 ABI passes args in caller-reserved slots (not push/pop) and
# does NOT clean them after the call, so the SP the caller resumes with
# is exactly its SP just before the `call far` == [bp+6] inside setjmp.
#
# jmp_buf layout (7 words; jmp_buf[7] unused):
#   [0] caller BP   [2] resume SP (=bp+6)   [4] SI   [6] DI
#   [8] caller BX   [10] ret IP            [12] ret CS
# BX/SI/DI/BP are the i8086 callee-saved regs (qbe allocates locals into
# BX/SI/DI), so restoring them makes longjmp's resume look like a normal
# return from the setjmp call site.
# ----------------------------------------------------------------------
SETJMP_EXE = """

segment LIBSTUB_TEXT

global _setjmp
_setjmp:
    push bp
    mov bp, sp
    mov dx, bx                   ; dx = caller's BX (scratch-save before clobber)
    mov bx, [bp+6]               ; bx = env (near ptr)
    mov ax, [bp+0]               ; caller BP
    mov [bx+0], ax
    lea ax, [bp+6]               ; resume SP (caller SP after the far ret)
    mov [bx+2], ax
    mov [bx+4], si
    mov [bx+6], di
    mov [bx+8], dx               ; caller BX
    mov ax, [bp+2]               ; ret IP
    mov [bx+10], ax
    mov ax, [bp+4]               ; ret CS
    mov [bx+12], ax
    xor ax, ax                   ; setjmp returns 0 on the direct call
    mov bx, dx                   ; restore caller's BX (callee-saved; we
                                 ; clobbered it as the env pointer above)
    pop bp
    retf

global _longjmp
_longjmp:
    push bp
    mov bp, sp
    mov bx, [bp+6]               ; bx = env (kept live until the final retf)
    mov ax, [bp+8]               ; val
    test ax, ax
    jnz .nz
    mov ax, 1                    ; longjmp(env,0) must surface as 1
.nz:
    mov si, [bx+4]               ; restore SI
    mov di, [bx+6]               ; restore DI
    mov cx, [bx+12]              ; ret CS
    mov dx, [bx+10]              ; ret IP
    mov sp, [bx+2]               ; restore caller SP (SS==DS; offset only)
    push cx                      ; CS \\ pushed below restored SP, reclaimed
    push dx                      ; IP / by the retf below
    mov bp, [bx+0]               ; restore caller BP
    mov bx, [bx+8]               ; restore caller BX (final use of env ptr)
    retf                         ; far-jump to ret CS:IP with AX = val
"""


# Far-data (compact/large/huge) setjmp/longjmp.  Identical stack arithmetic
# to SETJMP_EXE -- the resume SP is still `lea [bp+6]` (args sit ABOVE the
# 4-byte CS:IP return address regardless of their size, since the i8086 ABI
# reserves arg slots in the caller frame and does not clean them).  The only
# differences from the medium form:
#   * `env` is a 4-byte FAR pointer (off at [bp+6], seg at [bp+8]) reached via
#     ES:BX, not a 2-byte near ptr via DS:BX.
#   * longjmp's `val` is at [bp+10] (after the 4-byte env), not [bp+8].
# minic mangles setjmp/longjmp -> far_setjmp/far_longjmp (asm _far_setjmp /
# _far_longjmp) under far-data models via call_target_name's far_stdlib[].
# The jmp_buf contents are still 7 16-bit words; only the POINTER to it is far.
# ES is left clobbered by longjmp (it jumps back to the caller's post-setjmp
# code, bypassing _far_setjmp's `pop es`); under far-data codegen ES is always
# reloaded before a far access, so it is never assumed live across a call.
FAR_SETJMP_EXE = """

segment LIBSTUB_TEXT

global _far_setjmp
_far_setjmp:
    push bp
    mov bp, sp
    push es                      ; save caller ES (restored on the direct path)
    mov dx, bx                   ; dx = caller's BX (scratch-save before clobber)
    mov es, [bp+8]               ; es = env.seg
    mov bx, [bp+6]               ; bx = env.off
    mov ax, [bp+0]               ; caller BP
    mov [es:bx+0], ax
    lea ax, [bp+6]               ; resume SP (caller SP just before the far call)
    mov [es:bx+2], ax
    mov [es:bx+4], si
    mov [es:bx+6], di
    mov [es:bx+8], dx            ; caller BX
    mov ax, [bp+2]               ; ret IP
    mov [es:bx+10], ax
    mov ax, [bp+4]               ; ret CS
    mov [es:bx+12], ax
    xor ax, ax                   ; setjmp returns 0 on the direct call
    mov bx, dx                   ; restore caller's BX (callee-saved here)
    pop es                       ; restore caller ES
    pop bp
    retf

global _far_longjmp
_far_longjmp:
    push bp
    mov bp, sp
    mov es, [bp+8]               ; es = env.seg (SS-relative read, before SP switch)
    mov bx, [bp+6]               ; bx = env.off (kept live until the final retf)
    mov ax, [bp+10]              ; val
    test ax, ax
    jnz .nz
    mov ax, 1                    ; longjmp(env,0) must surface as 1
.nz:
    mov si, [es:bx+4]            ; restore SI
    mov di, [es:bx+6]            ; restore DI
    mov cx, [es:bx+12]           ; ret CS
    mov dx, [es:bx+10]           ; ret IP
    mov sp, [es:bx+2]            ; restore caller SP (env still reachable via ES:BX)
    push cx                      ; CS \\ pushed below restored SP, reclaimed
    push dx                      ; IP / by the retf below
    mov bp, [es:bx+0]            ; restore caller BP
    mov bx, [es:bx+8]            ; restore caller BX (final use of env ptr)
    retf                         ; far-jump to ret CS:IP with AX = val
"""


def far_data_model(model):
    """Compact/large/huge models use 4-byte data pointers; medium does not."""
    return model in ('compact', 'large', 'huge')


def near_code_model(model):
    """Tiny/small keep libstub's native near-call ABI: 2-byte return
    address, args from [bp+4], plain `ret`.  The body passes through
    UNTRANSFORMED and the EXE epilogue blocks (authored in far-call ABI)
    are reverse-transformed by unfar_epilogue()."""
    return model in ('tiny', 'small')


def unfar_epilogue(text):
    """Reverse the far-call ABI the EXE epilogue blocks are authored in,
    for near-code models: `retf` -> `ret`, `call far X` -> `call X`, and
    every positive [bp+N] arg offset (N >= 6, the first far-ABI arg) drops
    2 because a near call pushes a 2-byte return address.  The printf
    engines also compute their vararg base arithmetically
    (`add si, N ; first vararg at bp+M = ...`) — shift those the same way."""
    out = []
    for line in text.splitlines():
        line = re.sub(r'\bretf\b', 'ret', line)
        line = re.sub(r'\bcall\s+far\s+', 'call ', line)
        # The blocks switch segments internally; route their code into the
        # shared near-code _TEXT segment.
        line = re.sub(r'\bLIBSTUB_TEXT\b', '_TEXT', line)

        def repl(m):
            n = int(m.group(1))
            if n >= 6:
                return '[bp+{}]'.format(n - 2)
            return m.group(0)
        line = re.sub(r'\[bp\+(\d+)\]', repl, line)
        m = re.match(r'^(\s*add\s+si,\s*)(\d+)(\s*; first vararg at bp\+)'
                     r'(\d+)(.*)$', line)
        if m:
            line = '{}{}{}{}{}'.format(m.group(1), int(m.group(2)) - 2,
                                       m.group(3), int(m.group(4)) - 2,
                                       m.group(5))
        out.append(line)
    return '\n'.join(out)


def build_epilogue(model, no_stdio=False):
    """Assemble the EPILOGUE; the 4-byte stdio sentinels + _far_fX helpers
    are appended only under far-data models."""
    if no_stdio:
        # --no-stdio (newlibc links, §6b): the C library under test
        # provides the whole stdio surface (printf family via its own
        # formatter + _write -> VFS), so every libstub stdio block is
        # suppressed to avoid duplicate publics.  Only malloc/free
        # survive from the EXE epilogue.  libstub.asm's near
        # _stdin/_stdout/_stderr sentinels are KEPT: `FILE { int _file }`
        # with _file first makes the one-word sentinel structs directly
        # compatible with newlib-shaped stream_fd().  Near-code only —
        # far models route printf through minic's far_stdlib mangling,
        # which a newlibc-provided printf doesn't answer to yet.
        return ''.join(unfar_epilogue(p) for p in [MALLOC_EXE])
    if near_code_model(model):
        # Small/tiny .EXE: keep the EXE-specific malloc/fileio replacements
        # (libstub.asm's versions reference .COM-only labels) but in near
        # ABI.  FAR_SPRINTF stays because FILEIO's _far_printf/_far_fprintf
        # call it (unreachable under near-code — minic's far_stdlib
        # mangling is off — but nasm needs the symbol defined).
        # FAR_DOSIO/FAR_STDIO/FAR_SETJMP are likewise unreachable and
        # standalone, so they are dropped.  SETJMP_EXE is dropped too:
        # its jmp_buf layout is structurally far (saves the 4-byte CS:IP
        # return, longjmp exits via retf) — a near setjmp needs its own
        # implementation; no small-model consumer today.
        return ''.join(unfar_epilogue(p) for p in
                       [MALLOC_EXE, FILEIO_EXE, FAR_SPRINTF_EXE])
    parts = [MALLOC_EXE, FILEIO_EXE, FAR_SPRINTF_EXE, FAR_DOSIO_EXE, SETJMP_EXE]
    if far_data_model(model):
        parts.append(FAR_STDIO_EXE)
        parts.append(FAR_SETJMP_EXE)
    return ''.join(parts)


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
    args = sys.argv[1:]
    model = 'medium'
    no_stdio = False
    while args and args[0].startswith('--'):
        a = args.pop(0)
        if a.startswith('--model='):
            model = a[len('--model='):]
        elif a == '--no-stdio':
            no_stdio = True
        else:
            print('libstub_to_exe: unknown option: ' + a, file=sys.stderr)
            sys.exit(2)
    if len(args) != 2:
        print(__doc__, file=sys.stderr)
        sys.exit(2)
    in_path, out_path = args[0], args[1]
    # Compact is currently routed through the medium-model far-call ABI
    # (see i8086 `uses_far_code()`): the only model-specific difference
    # is far-pointer data on the libstub side via the _far_X variants.
    # Near-code models (small) keep the native near ABI and emit into the
    # shared `_TEXT` segment so the linker coalesces ALL code into one
    # 64KB CS frame (near calls + 2-byte code pointers need one frame).
    near_code = near_code_model(model)
    if near_code:
        code_seg = '_TEXT'
        model_label = 'Small/near-code model (model=%s)' % model
        abi_label = ('All functions use the native near-call ABI: '
                     'ret, args at [bp+4] and up.')
    else:
        code_seg = 'LIBSTUB_TEXT'
        model_label = 'Medium/far-code model (model=%s)' % model
        abi_label = ('All functions use far-call ABI: '
                     'retf, args at [bp+6] and up.')
    prologue = PROLOGUE_TEMPLATE.format(code_seg=code_seg,
                                        model_label=model_label,
                                        abi_label=abi_label)
    with open(in_path) as f:
        src = f.read()

    out_lines = [prologue, '']

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
            out_lines.append('segment ' + code_seg)
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
    SKIP_GLOBALS = {
        '_malloc', '_free',
        # All file I/O is reimplemented in FILEIO_EXE for write support.
        '_fopen', '_fclose', '_getc',
        '_fwrite', '_fread', '_fputc', '_fputs', '_fprintf', '_printf', '_fflush',
        '_rename', '_remove',
    }
    if far_data_model(model):
        # FAR_STDIO_EXE re-emits 4-byte _stdin/_stdout/_stderr sentinels (+
        # the matching _stdin_file/_stdout_file/_stderr_file storage); skip
        # the 2-byte libstub.asm versions.  `global _stdin, _stdout, _stderr`
        # is a single line whose first symbol triggers the skip, and the
        # skip region runs until the next `global` (= `_updatetabstoptable`),
        # which is exactly the data block we want to suppress.
        SKIP_GLOBALS = set(SKIP_GLOBALS)
        SKIP_GLOBALS.add('_stdin')
    if no_stdio:
        if not near_code_model(model):
            print('libstub_to_exe: --no-stdio requires a near-code model '
                  '(small/tiny); far models route stdio through far_stdlib '
                  'mangling', file=sys.stderr)
            sys.exit(2)
        # §6b: the libstub.asm stdio symbols that the newlibc subset (or
        # the dos_shim.c bottom layer: `stat`) defines itself and that
        # are NOT already EXE-replaced (those are in the base
        # SKIP_GLOBALS and their FILEIO_EXE replacements are dropped by
        # build_epilogue).  No kept libstub.asm code calls any of these
        # (verified: no `call _sprintf/_putchar/_fgets/_abort/_stat`).
        SKIP_GLOBALS = set(SKIP_GLOBALS)
        SKIP_GLOBALS |= {'_sprintf', '_fgets', '_putchar', '_abort', '_stat'}
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

        # Otherwise it's code: apply transform (ret→retf, bp+ shift).
        # Near-code models keep the body's native near ABI untouched.
        if in_data_block:
            in_data_block = False
            open_code()
        out_lines.append(raw if near_code else transform(raw))
        i += 1

    out_lines.append(build_epilogue(model, no_stdio))

    with open(out_path, 'w') as f:
        f.write('\n'.join(out_lines) + '\n')


if __name__ == '__main__':
    main()
