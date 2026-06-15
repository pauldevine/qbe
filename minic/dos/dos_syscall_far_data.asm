; dos_syscall_far_data.asm -- the far-DATA-model INT 21h primitives for the
; libstub-free DOS-hosted link (§7t, Phase-6 libstub retirement).
;
; Under far-DATA models (compact/large/huge in this backend -- all FAR CODE +
; FAR DATA) a `union REGS *` / `struct SREGS *` argument is a 4-byte far
; pointer (offset then segment).  minic's call_target_name() mangles
; int86/intdos/segread to _far_int86/_far_intdos/_far_segread in those models
; (far_stdlib[] in minic.y), so the libstub-free runtime must supply far
; helpers that read the register struct through ES:BX (the caller's segment)
; rather than DS-relative near pointers.  This is the far-data counterpart of
; dos_syscall.asm (which serves small near-data raw and medium near-data via
; near_to_far_rt.py); that file's near-pointer _int86/_intdos/_segread are the
; wrong ABI here.
;
; ⚠ SOURCE-OF-TRUTH DUPLICATION: each routine is COPIED VERBATIM from
; tools/libstub_to_exe.py's FAR_DOSIO_EXE block (the python EXE epilogue that
; this campaign is retiring).  libstub_to_exe.py is left untouched so every
; existing libstub gate cannot regress; a future fix to the far-syscall logic
; MUST be applied in BOTH places.  _far_puts is intentionally NOT ported (it
; needs libstub's _dgroup_para; newlibc's puts -> _write covers it instead).
;
; Far form: far code (retf), 4-byte far return address, so the first stack
; argument lives at [bp+6].

bits 16
cpu 8086

; Pure code TU: no DGROUP data of its own.  A unique CODE segment name keeps
; this in its own paragraph/CS (omf_link coalesces CODE by NAME) so the far
; CALLs into it resolve, matching asm_to_omf.py / near_to_far_rt.py naming.
segment DOS_SYSCALL_FAR_TEXT class=CODE align=2 use16

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
    mov bx, [es:bx+2]            ; BX = in.bx (in ptr discarded)
    int 0x21

    push bx                      ; save call-result BX
    mov bx, [bp+12]              ; out.seg
    mov es, bx
    mov bx, [bp+10]             ; ES:BX -> out
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
    mov ax, [es:bx+0]           ; return = call AX

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
; Patches the INT immediate via self-modifying code (same idiom as _int86).
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

    mov ax, [bp+10]              ; in.seg
    mov es, ax
    mov bx, [bp+8]               ; ES:BX -> in
    mov ax, [es:bx+0]
    mov cx, [es:bx+4]
    mov dx, [es:bx+6]
    mov si, [es:bx+8]
    mov di, [es:bx+10]
    mov bx, [es:bx+2]
.fi86_op:
    int 0x21

    push bx
    mov bx, [bp+14]             ; out.seg
    mov es, bx
    mov bx, [bp+12]            ; ES:BX -> out
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
; int int86x(int intno, union REGS __far *in, union REGS __far *out,
;            struct SREGS __far *segs)
;
; int86x is NOT in minic's far_stdlib[] list, so under far-DATA models its
; call site is the UNMANGLED `_int86x` (with 4-byte far-pointer args) -- the
; far-data counterpart of dos_syscall.asm's near `_int86x`.  Unlike _far_int86
; it loads ES and DS from the caller-supplied segs struct before the INT (used
; by AH=56h rename for the new-name ES:DI), then writes the callee ES/DS back
; into segs.  in/out/segs are each read/written through their OWN far segment.
;
; Stack: [bp+6] intno, [bp+8] in.off,   [bp+10] in.seg,
;        [bp+12] out.off, [bp+14] out.seg,
;        [bp+16] segs.off, [bp+18] segs.seg.
; ----------------------------------------------------------------------
global _int86x
_int86x:
    push bp
    mov bp, sp
    push si
    push di
    push bx
    push es
    push ds                      ; callee-save (we will overwrite DS)

    mov ax, [bp+6]
    mov [cs:.x_int_op+1], al      ; patch INT immediate

    ; Read segs->{es,ds} via segs' far segment, stash both in CS-rel scratch.
    mov ax, [bp+18]               ; segs.seg
    mov es, ax
    mov bx, [bp+16]               ; ES:BX -> segs
    mov ax, [es:bx+0]             ; segs.es (desired ES for INT)
    mov [cs:.x_desired_es], ax
    mov ax, [es:bx+6]             ; segs.ds (desired DS for INT)
    mov [cs:.x_desired_ds], ax

    ; Load GPRs from in via in's far segment (DS still ours).
    mov ax, [bp+10]               ; in.seg
    mov es, ax
    mov bx, [bp+8]                ; ES:BX -> in
    mov ax, [es:bx+0]
    mov cx, [es:bx+4]
    mov dx, [es:bx+6]
    mov si, [es:bx+8]
    mov di, [es:bx+10]
    mov bx, [es:bx+2]             ; BX = in.bx

    ; Last step before INT: set ES and DS to the caller-supplied values.
    push ax
    mov ax, [cs:.x_desired_es]
    mov es, ax
    mov ax, [cs:.x_desired_ds]
    mov ds, ax
    pop ax
.x_int_op:
    int 0x21

    ; Snapshot callee ES/DS to CS-rel scratch, restore our DS = SS.
    push ax
    push ds
    pop ax
    mov [cs:.x_callee_ds], ax
    push es
    pop ax
    mov [cs:.x_callee_es], ax
    push ss
    pop ds
    pop ax

    ; Write outregs via out's far segment.
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
    mov ax, [es:bx+0]             ; return = result AX

    ; Write callee ES/DS into segs->{es,ds} via segs' far segment.
    push ax
    mov ax, [bp+18]               ; segs.seg
    mov es, ax
    mov bx, [bp+16]               ; ES:BX -> segs
    mov ax, [cs:.x_callee_es]
    mov [es:bx+0], ax
    mov ax, [cs:.x_callee_ds]
    mov [es:bx+6], ax
    pop ax

    pop ds
    pop es
    pop bx
    pop di
    pop si
    pop bp
    retf

.x_desired_es: dw 0
.x_desired_ds: dw 0
.x_callee_ds:  dw 0
.x_callee_es:  dw 0


; ----------------------------------------------------------------------
; void far_segread(struct SREGS __far *segs)
;
; Stack: [bp+6] segs.off, [bp+8] segs.seg.
;
; Read ES BEFORE overwriting it to point at segs, so stash caller's ES in SI.
; ----------------------------------------------------------------------
global _far_segread
_far_segread:
    push bp
    mov bp, sp
    push bx
    push si
    push es

    push es
    pop si                       ; SI = caller's ES (pre-overwrite)

    mov ax, [bp+8]               ; segs.seg
    mov es, ax
    mov bx, [bp+6]               ; ES:BX -> segs

    mov [es:bx+0], si            ; segs.es = caller's ES
    mov [es:bx+2], cs
    mov [es:bx+4], ss
    mov [es:bx+6], ds

    pop es
    pop si
    pop bx
    pop bp
    retf
