; setjmp_rt.asm -- setjmp/longjmp for the libstub-free DOS-hosted runtime
; (§7x, Phase-6 libstub retirement).
;
; The libstub-free runtime (qbe_rt.asm + dos_syscall*.asm + heap.asm + the
; minic-compiled dos_libc.c fill) had NO setjmp/longjmp, so any --no-libstub
; program that called them failed to link -- the §7w gate PINNED the five
; setjmp probes (setjmp_probe, setjmp_clobber_probe, arr_jmpbuf_probe,
; aoa_extended_probe, split_stack_probe) to --libstub for want of an impl.
; This standalone TU supplies it, so those probes can take the libstub-free
; default and the retirement is complete for the setjmp surface.
;
; ⚠ SOURCE-OF-TRUTH DUPLICATION: the three forms below are COPIED VERBATIM from
; tools/libstub_to_exe.py -- the near form from NEAR_SETJMP_EXE, the medium
; (far code / near data) form from SETJMP_EXE, and the far-data form from
; FAR_SETJMP_EXE.  libstub_to_exe.py is left untouched so MicroPython / stevie
; / every existing gate cannot regress.  A future fix to any save/restore or
; jmp_buf-offset logic MUST be applied in BOTH files.
;
; ⚠ NOT routed through near_to_far_rt.py: the near<->far transform there shifts
; every [bp+N], which would silently corrupt the jmp_buf INTERNAL [bx+10] /
; [bx+12] ret-IP/CS offsets along with the frame offsets (the same reason
; libstub_to_exe.py hand-writes NEAR_SETJMP_EXE rather than reverse-transforming
; SETJMP_EXE).  So each ABI form is authored explicitly and selected by a nasm
; -d flag from the build:
;   (none)       small  -- near code, near data; _setjmp/_longjmp, near ret.
;   -dSJ_FAR_CODE medium -- far code, near data; _setjmp/_longjmp, env via
;                           DS:BX, retf, jmp_buf carries the ret CS word.
;   -dSJ_FAR_DATA compact/large/huge -- far code AND far data; minic mangles
;                           setjmp/longjmp -> _far_setjmp/_far_longjmp
;                           (far_stdlib[]); env is a 4-byte far ptr via ES:BX.
;
; jmp_buf is the C int[8] = 16 bytes; the near form uses 6 words ([12]/[14]
; spare), the far forms 7 words ([14] spare).

bits 16
cpu 8086

%ifdef SJ_FAR_DATA
; ----------------------------------------------------------------------
; Far-data (compact/large/huge): far code + far data.  env is a 4-byte FAR
; pointer (off at [bp+6], seg at [bp+8]) reached via ES:BX.  longjmp's `val`
; is at [bp+10] (after the 4-byte env).  Verbatim from FAR_SETJMP_EXE.
; ES is left clobbered by longjmp (it jumps back to the caller's post-setjmp
; code, bypassing _far_setjmp's `pop es`); under far-data codegen ES is always
; reloaded before a far access, so it is never assumed live across a call.
; A unique far-code segment (SETJMP_RT_TEXT) keeps this in its own paragraph
; so omf_link's `call far _far_setjmp` fixup resolves, mirroring QBE_RT_TEXT.
; ----------------------------------------------------------------------
segment SETJMP_RT_TEXT class=CODE align=2 use16

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
    push cx                      ; CS \ pushed below restored SP, reclaimed
    push dx                      ; IP / by the retf below
    mov bp, [es:bx+0]            ; restore caller BP
    mov bx, [es:bx+8]            ; restore caller BX (final use of env ptr)
    retf                         ; far-jump to ret CS:IP with AX = val

%elifdef SJ_FAR_CODE
; ----------------------------------------------------------------------
; Medium: far code, NEAR data.  env is a 2-byte near ptr at [bp+6] reached via
; DS:BX (DS==SS reaches a stack env).  jmp_buf carries the ret CS word.
; Verbatim from SETJMP_EXE.  Unique far-code segment so `call far _setjmp`
; resolves (mirrors QBE_RT_TEXT).
; ----------------------------------------------------------------------
segment SETJMP_RT_TEXT class=CODE align=2 use16

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
    push cx                      ; CS \ pushed below restored SP, reclaimed
    push dx                      ; IP / by the retf below
    mov bp, [bx+0]               ; restore caller BP
    mov bx, [bx+8]               ; restore caller BX (final use of env ptr)
    retf                         ; far-jump to ret CS:IP with AX = val

%else
; ----------------------------------------------------------------------
; Near (tiny/small): near code, near data.  A near `call` pushes only a 2-byte
; return IP, so env (first arg) sits at [bp+4] -- one word lower than the far
; form's [bp+6].  No CS word in the jmp_buf; near `ret` restores IP only.
; Verbatim from NEAR_SETJMP_EXE; shares the single small-model _TEXT frame.
; ----------------------------------------------------------------------
segment _TEXT class=CODE align=2 use16

global _setjmp
_setjmp:
    push bp
    mov bp, sp
    mov dx, bx                   ; dx = caller's BX (scratch-save before clobber)
    mov bx, [bp+4]               ; bx = env (near ptr)
    mov ax, [bp+0]               ; caller BP
    mov [bx+0], ax
    lea ax, [bp+4]               ; resume SP (caller SP after the near ret)
    mov [bx+2], ax
    mov [bx+4], si
    mov [bx+6], di
    mov [bx+8], dx               ; caller BX
    mov ax, [bp+2]               ; ret IP
    mov [bx+10], ax
    xor ax, ax                   ; setjmp returns 0 on the direct call
    mov bx, dx                   ; restore caller's BX (callee-saved; we
                                 ; clobbered it as the env pointer above)
    pop bp
    ret

global _longjmp
_longjmp:
    push bp
    mov bp, sp
    mov bx, [bp+4]               ; bx = env (kept live until the final ret)
    mov ax, [bp+6]               ; val
    test ax, ax
    jnz .nz
    mov ax, 1                    ; longjmp(env,0) must surface as 1
.nz:
    mov si, [bx+4]               ; restore SI
    mov di, [bx+6]               ; restore DI
    mov dx, [bx+10]              ; ret IP
    mov sp, [bx+2]               ; restore caller SP (SS==DS; offset only)
    push dx                      ; IP pushed below restored SP, reclaimed by ret
    mov bp, [bx+0]               ; restore caller BP
    mov bx, [bx+8]               ; restore caller BX (final use of env ptr)
    ret                          ; near-jump to ret IP with AX = val

%endif
