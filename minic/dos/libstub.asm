; Minimal libc stubs for stevie-dos prototype build.
;
; These are placeholder implementations that allow the binary to link.
; A real port needs proper printf/fopen/malloc on top of the doslib DOS
; INT 21h wrappers.  For now we just want a successful link.

; void *malloc(size_t sz) — pointer arg is l (32-bit), but size_t is w.
; Stack: [bp+4] = sz (w).  Returns DX:AX where AX=offset, DX=0 (DS-rel).
;
; Bump-pointer allocator.  The heap starts at _heap_end_of_image (a
; label placed at the absolute end of the linked .COM, defined by the
; build script) and ends at SP minus a stack reserve, computed at first
; call.  No bytes are added to the .COM file by this code — the heap
; lives in the ~64KB-_heap_end_of_image region of the loaded segment.
;
; ⚠ This is dead code while .COM is over 64KB: DOSBox truncates the
; image at 0xFFFE so _heap_end_of_image points past the truncation and
; into garbage.  Once the .COM size is fixed (Path A or B), this lights
; up automatically.
global _malloc
_malloc:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    add ax, 1
    and ax, 0xFFFE              ; word-align the size
    cmp word [_heap_initialized], 0
    jne .post_init
    ; First-call init: heap_ptr = end-of-image (rounded up to word),
    ; heap_top = SP - 1024 (leave 1KB stack).
    mov bx, _heap_end_of_image
    add bx, 1
    and bx, 0xFFFE
    mov [_heap_ptr], bx
    mov bx, sp
    sub bx, 1024
    mov [_heap_top], bx
    mov word [_heap_initialized], 1
.post_init:
    mov bx, [_heap_ptr]
    mov cx, bx
    add cx, ax
    cmp cx, [_heap_top]
    ja .fail
    mov [_heap_ptr], cx
    mov ax, bx                  ; offset into DGROUP
    xor dx, dx                  ; segment relative to DS = 0
    pop bp
    ret
.fail:
    xor ax, ax
    xor dx, dx
    pop bp
    ret

global _free
_free:                          ; bump-allocator can't free
    ret

_heap_initialized:  dw 0
_heap_ptr:          dw 0
_heap_top:          dw 0

global _strlen
_strlen:
    push bp
    mov bp, sp
    push si
    mov si, [bp+4]
    xor ax, ax
.l:
    cmp byte [si], 0
    je .d
    inc ax
    inc si
    jmp .l
.d:
    pop si
    pop bp
    ret

; char *strcpy(char *dest, char *src) — near pointers, 2 bytes each.
global _strcpy
_strcpy:
    push bp
    mov bp, sp
    push si
    push di
    mov di, [bp+4]      ; dest
    mov si, [bp+6]      ; src
.l:
    lodsb
    stosb
    cmp al, 0
    jne .l
    mov ax, [bp+4]
    pop di
    pop si
    pop bp
    ret

; int strcmp(const char *s1, const char *s2)
global _strcmp
_strcmp:
    push bp
    mov bp, sp
    push si
    push di
    push bx
    mov si, [bp+4]
    mov di, [bp+6]
    xor ax, ax
.loop:
    mov bl, [si]
    mov bh, [di]
    cmp bl, bh
    jne .diff
    cmp bl, 0
    je .done
    inc si
    inc di
    jmp .loop
.diff:
    xor ax, ax
    xor dx, dx
    mov al, bl
    mov dl, bh
    sub ax, dx
.done:
    pop bx
    pop di
    pop si
    pop bp
    ret

; int strncmp(const char *s1, const char *s2, size_t n)
global _strncmp
_strncmp:
    push bp
    mov bp, sp
    push si
    push di
    push bx
    mov si, [bp+4]
    mov di, [bp+6]
    mov cx, [bp+8]
    xor ax, ax
    jcxz .done
.loop:
    mov bl, [si]
    mov bh, [di]
    cmp bl, bh
    jne .diff
    cmp bl, 0
    je .done
    inc si
    inc di
    dec cx
    jnz .loop
    jmp .done
.diff:
    xor ax, ax
    xor dx, dx
    mov al, bl
    mov dl, bh
    sub ax, dx
.done:
    pop bx
    pop di
    pop si
    pop bp
    ret

; char *strchr(const char *s, int c)
global _strchr
_strchr:
    push bp
    mov bp, sp
    push si
    mov si, [bp+4]      ; s
    mov ax, [bp+6]      ; c (low byte significant)
.lsc:
    mov ah, [si]
    cmp ah, al
    je .fsc             ; found
    test ah, ah
    je .nfsc            ; end of string
    inc si
    jmp .lsc
.fsc:
    mov ax, si
    pop si
    pop bp
    ret
.nfsc:
    xor ax, ax
    pop si
    pop bp
    ret

; char *strcat(char *dest, const char *src)
global _strcat
_strcat:
    push bp
    mov bp, sp
    push si
    push di
    mov di, [bp+4]      ; dest
.lac:
    mov al, [di]
    test al, al
    je .acdn
    inc di
    jmp .lac
.acdn:
    mov si, [bp+6]      ; src
.lacc:
    lodsb
    stosb
    test al, al
    jne .lacc
    mov ax, [bp+4]
    pop di
    pop si
    pop bp
    ret

; size_t strcspn(const char *s, const char *reject)
global _strcspn
_strcspn:
    push bp
    mov bp, sp
    push si
    push di
    push bx
    mov si, [bp+4]      ; s
    xor cx, cx          ; count
.lcsp:
    mov al, [si]
    test al, al
    je .csdn
    mov di, [bp+6]      ; reject
.lcsr:
    mov bl, [di]
    test bl, bl
    je .csno            ; not in reject
    cmp al, bl
    je .csdn            ; in reject -> stop
    inc di
    jmp .lcsr
.csno:
    inc si
    inc cx
    jmp .lcsp
.csdn:
    mov ax, cx
    pop bx
    pop di
    pop si
    pop bp
    ret

; char *strncpy(char *dest, const char *src, size_t n)
global _strncpy
_strncpy:
    push bp
    mov bp, sp
    push si
    push di
    mov di, [bp+4]      ; dest
    mov si, [bp+6]      ; src
    mov cx, [bp+8]      ; n
    jcxz .nclend
.lnc1:
    lodsb               ; al = *src++
    stosb               ; *dest++ = al
    test al, al
    je .lnc2            ; src ended; pad rest with NUL
    loop .lnc1
    jmp .nclend
.lnc2:
    dec cx
    jcxz .nclend
.lncp:
    xor al, al
    stosb
    loop .lncp
.nclend:
    mov ax, [bp+4]
    pop di
    pop si
    pop bp
    ret

global _atoi
_atoi:
    mov ax, 0
    ret

; ============================================================================
; sprintf(char *dest, const char *fmt, ...) — full implementation
; ============================================================================
;
; Conversion specifiers:
;   %d %i      signed decimal       (16-bit; 32-bit with `l`)
;   %u         unsigned decimal     (16-bit; 32-bit with `l`)
;   %x %X      hex (lower/upper)    (16-bit; 32-bit with `l`)
;   %o         octal                (16-bit; 32-bit with `l`)
;   %s         string
;   %c         character
;   %p         pointer (treated as %x with `l`)
;   %%         literal '%'
;
; Flags / modifiers:
;   -          left-align within field
;   0          zero-pad on left (integer only; ignored if precision set)
;   +, ' ', #  parsed but ignored
;   <width>    minimum field width (decimal digits, no '*')
;   .<prec>    string max length / integer min digits
;   l          long argument (32-bit on i8086)
;   ll         parsed; treated as `l` (no long long support)
;   h, hh      parsed but ignored
;
; Not supported: floating point (%f %e %g %a), %n, %*<width|prec>.
;
; Internal helpers (_spr_emit_int) use `retn` so libstub_to_exe.py's
; ret→retf rewrite doesn't break the near-call ABI used inside this file.
; ============================================================================

global _sprintf
_sprintf:
    push bp
    mov bp, sp
    push si
    push di
    push bx
    mov di, [bp+4]              ; dest
    mov si, [bp+6]              ; fmt
    lea bx, [bp+8]              ; ptr to first variadic arg

.spr_top:
    lodsb
    test al, al
    jz .spr_done
    cmp al, '%'
    je .spr_pct
    stosb
    jmp .spr_top

.spr_pct:
    mov word [_spr_flags], 0
    mov word [_spr_width], 0
    mov word [_spr_prec], 0

    ; -- Flags --
.spr_pf:
    lodsb
    cmp al, '-'
    jne .pf_nminus
    or word [_spr_flags], 1
    jmp .spr_pf
.pf_nminus:
    cmp al, '0'
    jne .pf_nzero
    or word [_spr_flags], 2
    jmp .spr_pf
.pf_nzero:
    cmp al, '+'
    je .spr_pf
    cmp al, ' '
    je .spr_pf
    cmp al, '#'
    je .spr_pf

    ; -- Width (AL has lookahead) --
.spr_pw:
    cmp al, '0'
    jb .pw_done
    cmp al, '9'
    ja .pw_done
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
    jmp .spr_pw
.pw_done:

    ; -- Precision --
    cmp al, '.'
    jne .pp_done
    or word [_spr_flags], 8     ; bit 3: precision is set
    lodsb
.spr_pp:
    cmp al, '0'
    jb .pp_done
    cmp al, '9'
    ja .pp_done
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
    jmp .spr_pp
.pp_done:

    ; -- Length modifier --
    cmp al, 'l'
    jne .lm_h
    or word [_spr_flags], 4
    lodsb
    cmp al, 'l'                 ; 'll' → consume, still treat as `l`
    jne .lm_done
    lodsb
    jmp .lm_done
.lm_h:
    cmp al, 'h'                 ; 'h' / 'hh' → ignored (promoted to int)
    jne .lm_done
    lodsb
    cmp al, 'h'
    jne .lm_done
    lodsb
.lm_done:

    ; -- Dispatch on conversion specifier --
    test al, al
    jz .spr_done
    cmp al, '%'
    je .spr_emit_pct
    cmp al, 'c'
    je .do_chr
    cmp al, 's'
    je .do_str
    cmp al, 'd'
    je .do_signed
    cmp al, 'i'
    je .do_signed
    cmp al, 'u'
    je .do_unsigned
    cmp al, 'x'
    je .do_hex_lo
    cmp al, 'X'
    je .do_hex_up
    cmp al, 'o'
    je .do_octal
    cmp al, 'p'
    je .do_hex_lo
    ; Unknown: emit literally so the bad spec is visible.
    stosb
    jmp .spr_top

.spr_emit_pct:
    stosb
    jmp .spr_top

.spr_done:
    mov byte [di], 0
    xor ax, ax
    pop bx
    pop di
    pop si
    pop bp
    ret

    ; ---- %c ----
.do_chr:
    mov ax, [bx]
    add bx, 2
    push ax                     ; save char (AL)
    mov cx, [_spr_width]
    cmp cx, 1
    jbe .chr_emit
    dec cx                      ; CX = pad count
    test word [_spr_flags], 1
    jnz .chr_left
    ; right-align: spaces, then char
    mov al, ' '
    rep stosb
    pop ax
    stosb
    jmp .spr_top
.chr_left:
    pop ax
    stosb
    mov al, ' '
    rep stosb
    jmp .spr_top
.chr_emit:
    pop ax
    stosb
    jmp .spr_top

    ; ---- %s ----
.do_str:
    push si                     ; save fmt pointer
    mov si, [bx]
    add bx, 2

    ; Cap scan length by precision if set
    mov cx, 0x7FFF
    test word [_spr_flags], 8
    jz .str_no_prec
    mov cx, [_spr_prec]
.str_no_prec:
    push si                     ; save src start
    xor dx, dx                  ; DX = length
.str_scan:
    test cx, cx
    jz .str_scan_done
    cmp byte [si], 0
    je .str_scan_done
    inc si
    inc dx
    dec cx
    jmp .str_scan
.str_scan_done:
    pop si                      ; SI = src, DX = length

    mov cx, [_spr_width]
    cmp cx, dx
    jbe .str_no_pad
    sub cx, dx                  ; CX = pad amount
    test word [_spr_flags], 1
    jnz .str_pad_after

    ; pad before
    push si
    push dx
    mov al, ' '
    rep stosb
    pop cx                      ; original length
    pop si
    rep movsb
    pop si                      ; restore fmt
    jmp .spr_top

.str_pad_after:
    push cx                     ; save pad count
    mov cx, dx
    rep movsb
    pop cx
    mov al, ' '
    rep stosb
    pop si                      ; restore fmt
    jmp .spr_top

.str_no_pad:
    mov cx, dx
    rep movsb
    pop si                      ; restore fmt
    jmp .spr_top

    ; ---- %d / %i ----
.do_signed:
    test word [_spr_flags], 4
    jnz .sgn_long
    mov ax, [bx]
    add bx, 2
    cwd                         ; sign-extend AX → DX:AX
    jmp .sgn_common
.sgn_long:
    mov ax, [bx]
    mov dx, [bx+2]
    add bx, 4
.sgn_common:
    mov byte [_spr_signc], 0
    test dx, dx
    jns .sgn_pos
    mov byte [_spr_signc], '-'
    not dx
    neg ax
    sbb dx, -1                  ; finish two's-complement negate of DX:AX
.sgn_pos:
    mov cx, 10
    call _spr_emit_int
    jmp .spr_top

    ; ---- %u ----
.do_unsigned:
    test word [_spr_flags], 4
    jnz .uns_long
    mov ax, [bx]
    add bx, 2
    xor dx, dx
    jmp .uns_common
.uns_long:
    mov ax, [bx]
    mov dx, [bx+2]
    add bx, 4
.uns_common:
    mov byte [_spr_signc], 0
    mov cx, 10
    call _spr_emit_int
    jmp .spr_top

    ; ---- %x ----
.do_hex_lo:
    and word [_spr_flags], 0xFFEF       ; clear uppercase
    jmp .hex_dispatch
.do_hex_up:
    or word [_spr_flags], 16
.hex_dispatch:
    test word [_spr_flags], 4
    jnz .hex_long
    mov ax, [bx]
    add bx, 2
    xor dx, dx
    jmp .hex_common
.hex_long:
    mov ax, [bx]
    mov dx, [bx+2]
    add bx, 4
.hex_common:
    mov byte [_spr_signc], 0
    mov cx, 16
    call _spr_emit_int
    jmp .spr_top

    ; ---- %o ----
.do_octal:
    test word [_spr_flags], 4
    jnz .oct_long
    mov ax, [bx]
    add bx, 2
    xor dx, dx
    jmp .oct_common
.oct_long:
    mov ax, [bx]
    mov dx, [bx+2]
    add bx, 4
.oct_common:
    mov byte [_spr_signc], 0
    mov cx, 8
    call _spr_emit_int
    jmp .spr_top


; ----------------------------------------------------------------------------
; _spr_emit_int — write an unsigned 32-bit integer to ES:DI with formatting.
;
; In:
;   DX:AX = unsigned value
;   CX    = base (8, 10, 16)
;   [_spr_flags] = bit0='-' bit1='0' bit2='l' bit3=prec-set bit4=uppercase-hex
;   [_spr_width] = minimum field width
;   [_spr_prec]  = minimum digit count (only when bit3 set)
;   [_spr_signc] = sign character ('-') or 0
;
; Out: DI advanced past emitted bytes.  SI preserved.  BX preserved.
; Trashes: AX, CX, DX.
;
; Uses `retn` so libstub_to_exe.py doesn't rewrite the return to far.
; ----------------------------------------------------------------------------
_spr_emit_int:
    push bx
    push si                     ; SI is sprintf's fmt pointer — preserve
    mov [_spr_base], cx

    push di                     ; save output ptr; reuse DI for digit gen
    mov di, _spr_digbuf + 12
    xor cx, cx                  ; CX = digit count

    ; Special case: value = 0 with precision = 0 → no digits at all
    mov bx, ax
    or bx, dx
    jnz .gd_loop
    test word [_spr_flags], 8
    jz .gd_emit_zero            ; no precision → emit single '0'
    cmp word [_spr_prec], 0
    jne .gd_emit_zero
    jmp .gd_after

.gd_emit_zero:
    dec di
    mov byte [di], '0'
    inc cx
    jmp .gd_after

.gd_loop:
    ; 32-bit divmod: DX:AX / base → DX:AX (quotient), BX = remainder
    push cx                     ; save digit count
    mov cx, [_spr_base]
    mov bx, ax                  ; stash low word
    mov ax, dx
    xor dx, dx
    div cx                      ; AX = q_hi, DX = r_hi
    mov [_spr_qhi], ax
    mov ax, bx                  ; restore low word into AX
    div cx                      ; AX = q_lo, DX = remainder
    mov bx, dx                  ; BX = remainder (digit value)
    mov dx, [_spr_qhi]          ; DX = q_hi
    pop cx                      ; restore digit count

    push ax                     ; save low quotient across digit conversion
    mov al, bl
    add al, '0'
    cmp bl, 10
    jb .gd_dig_ok
    add al, 'a' - '0' - 10      ; bump 10-15 to 'a'-'f' (+39)
    test word [_spr_flags], 16
    jz .gd_dig_ok
    sub al, 'a' - 'A'           ; uppercase: lower→upper (-32)
.gd_dig_ok:
    dec di
    mov [di], al
    inc cx
    pop ax

    mov bx, ax
    or bx, dx
    jnz .gd_loop

.gd_after:
    ; CX = actual digit count.  DI = start of digits in _spr_digbuf.
    ; SI was preserved above; swap roles now.
    mov si, di                  ; SI = digit pointer
    pop di                      ; restore output ptr (matches push di above)

    ; Effective digit count = max(actual, precision-if-set)
    mov ax, cx
    test word [_spr_flags], 8
    jz .ei_eff_ok
    cmp ax, [_spr_prec]
    jae .ei_eff_ok
    mov ax, [_spr_prec]
.ei_eff_ok:
    ; Content length = effective digit count + (sign present ? 1 : 0)
    mov bx, ax
    cmp byte [_spr_signc], 0
    je .ei_clen_ok
    inc bx
.ei_clen_ok:

    ; Compute width padding: DX = max(0, width - content_len)
    mov dx, [_spr_width]
    cmp dx, bx
    jbe .ei_no_wpad
    sub dx, bx
    jmp .ei_dispatch
.ei_no_wpad:
    xor dx, dx
.ei_dispatch:

    ; Branch on alignment / padding char
    test word [_spr_flags], 1
    jnz .ei_left

    ; right-align: choose zero vs space pad
    test word [_spr_flags], 2
    jz .ei_right_space
    test word [_spr_flags], 8
    jnz .ei_right_space         ; precision present → zero flag suppressed

    ; right-align, zero-pad: sign, DX zeros, precision-zeros, digits
    cmp byte [_spr_signc], 0
    je .rz_nosign
    push ax
    mov al, [_spr_signc]
    stosb
    pop ax
.rz_nosign:
    push ax
    push cx
    mov cx, dx
    mov al, '0'
    rep stosb
    pop cx
    pop ax
    sub ax, cx                  ; AX = precision-driven extra zeros
    jz .rz_pdone
    push cx
    mov cx, ax
    mov al, '0'
    rep stosb
    pop cx
.rz_pdone:
    rep movsb
    pop si
    pop bx
    retn

.ei_right_space:
    ; right-align, space-pad: DX spaces, sign, precision-zeros, digits
    push ax
    push cx
    mov cx, dx
    mov al, ' '
    rep stosb
    pop cx
    pop ax
    cmp byte [_spr_signc], 0
    je .rs_nosign
    push ax
    push cx
    mov al, [_spr_signc]
    stosb
    pop cx
    pop ax
.rs_nosign:
    sub ax, cx
    jz .rs_pdone
    push cx
    mov cx, ax
    mov al, '0'
    rep stosb
    pop cx
.rs_pdone:
    rep movsb
    pop si
    pop bx
    retn

.ei_left:
    ; left-align: sign, precision-zeros, digits, DX trailing spaces
    push dx
    cmp byte [_spr_signc], 0
    je .lt_nosign
    push ax
    push cx
    mov al, [_spr_signc]
    stosb
    pop cx
    pop ax
.lt_nosign:
    sub ax, cx
    jz .lt_pdone
    push cx
    mov cx, ax
    mov al, '0'
    rep stosb
    pop cx
.lt_pdone:
    rep movsb
    pop cx
    mov al, ' '
    rep stosb
    pop si
    pop bx
    retn


; sprintf state (DGROUP).  Single-threaded DOS — safe as statics.
_spr_flags:  dw 0
_spr_width:  dw 0
_spr_prec:   dw 0
_spr_base:   dw 0
_spr_qhi:    dw 0
_spr_signc:  db 0
_spr_pad0:   db 0
_spr_digbuf: db 0,0,0,0,0,0,0,0,0,0,0,0


global _fprintf
_fprintf:
    mov ax, 0
    ret

global _printf
_printf:
    mov ax, 0
    ret

global _fputs
_fputs:
    mov ax, 0
    ret

global _fputc
_fputc:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    pop bp
    ret

global _fread
_fread:
    mov ax, 0
    ret

global _fwrite
_fwrite:
    mov ax, 0
    ret

global _fseek
_fseek:
    mov ax, 0
    ret

global _ftell
_ftell:
    mov ax, 0
    ret

global _fflush
_fflush:
    ret

global _abort
_abort:
    mov ah, 4Ch
    mov al, 1
    int 21h

global _fopen
_fopen:
    mov ax, 0
    ret

; fopenb defined in dos.c
global _fclose
_fclose:
    mov ax, 0
    ret

global _getc
_getc:
    mov ax, -1
    ret

global _rename
_rename:
    ret

global _putc
_putc:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    pop bp
    ret

global _putchar
_putchar:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    push bx
    mov ah, 0x0E
    xor bx, bx
    int 0x10
    pop bx
    mov ax, [bp+4]
    pop bp
    ret

global _isalpha
_isalpha:
    mov ax, 0
    ret

global _isdigit
_isdigit:
    mov ax, 0
    ret

global _toupper
_toupper:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    pop bp
    ret

global _tolower
_tolower:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    pop bp
    ret

global _exit
_exit:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    mov ah, 4Ch
    int 21h

global _fgets
_fgets:
    mov ax, 0
    ret

global _isspace
_isspace:
    mov ax, 0
    ret

global _getenv
_getenv:
    mov ax, 0
    ret

global _system
_system:
    mov ax, 0
    ret

; DOS API helpers (stubs — real impl belongs in doslib.asm).
global _dos_cls
_dos_cls:
    ret
global _dos_gotoxy
_dos_gotoxy:
    ret
global _dos_kbhit
_dos_kbhit:
    mov ax, 0
    ret
global _dos_putch
_dos_putch:
    push bp
    mov bp, sp
    mov dx, [bp+4]
    mov ah, 02h
    int 21h
    pop bp
    ret
global _dos_getvidmode
_dos_getvidmode:
    mov ax, 0
    ret

; Stubs for stevie globals that don't have a clear single-file home.
; _params now lives in param.c (compiles via struct-array initializers).
; _gchar and _inc live in ptrfunc.c.

; stdio sentinels.
;
; The libstub I/O routines (_fwrite/_fputc/_fputs/_fprintf/_printf)
; treat a FILE* as a pointer to a struct whose FIRST WORD is the DOS
; file handle, so they all do `mov bx, [filep]` to extract the handle.
;
; To make `fprintf(stderr, ...)` work, the C variables stdin/stdout/
; stderr must each point at a one-word "FILE" struct holding the
; corresponding DOS handle.  That is, the C value of `stderr` is the
; address of a word whose contents are 2.
;
; In asm: `_stderr` is the C variable's storage (a word holding the
; FILE* value).  `_stderr_file` is the one-word struct that
; `_stderr` points at.
global _stdin, _stdout, _stderr
_stdin:  dw _stdin_file    ; FILE *stdin  = &_stdin_file
_stdout: dw _stdout_file   ; FILE *stdout = &_stdout_file
_stderr: dw _stderr_file   ; FILE *stderr = &_stderr_file
_stdin_file:  dw 0         ; FILE struct: handle = 0 (DOS stdin)
_stdout_file: dw 1         ; FILE struct: handle = 1 (DOS stdout)
_stderr_file: dw 2         ; FILE struct: handle = 2 (DOS stderr)

global _updatetabstoptable
_updatetabstoptable:
    ret
; Turbo C delay()/disable()/enable() stubs — stevie compiles with
; __TURBOC__ defined so it expects these.
global _delay
_delay:
    ret
global _disable
_disable:
    cli
    ret
global _enable
_enable:
    sti
    ret
; NOTE: filemess, readfile, writeit, renum live in fileio.c.
; updatescreen, updateline, cursupdate, s_ins, s_del live in screen.c.
; All compile to QBE asm now, so we don't stub them.

; regerror is now in search.c which compiles.

; Standard C / MS-DOS / curses functions stevie expects.

; int getch(void) — read keystroke from BIOS, no echo.
; Uses INT 16h AH=00h to also capture function/arrow keys via the
; extended scancode pattern (returns scancode<<8 | 0 for function keys,
; the way Microsoft C's getch did it).
global _getch
_getch:
    push bp
    mov bp, sp
    xor ah, ah
    int 16h          ; AH=scancode, AL=ASCII
    cmp al, 0
    jne .ascii
    ; Function key: return 0 first, save scancode for next call
    mov [cs:.fn_pending], ah
    mov byte [cs:.fn_flag], 1
    xor ax, ax
    pop bp
    ret
.ascii:
    xor ah, ah       ; return only the ASCII char in AX
    pop bp
    ret
.fn_pending: db 0
.fn_flag:    db 0

; int int86(int intno, union REGS *in, union REGS *out)
; REGS layout: ax(0), bx(2), cx(4), dx(6), si(8), di(10), cflag(12), flags(14).
;
; minic emits near pointers as 16-bit (`w`), so each REGS* arg occupies
; 2 bytes on the stack.
;
; Stack layout (after push bp / mov bp, sp, near-call return addr):
;   [bp+4]  intno (int = 16-bit)
;   [bp+6]  inregs offset
;   [bp+8]  outregs offset
;
; Dispatches the requested INT via self-modifying code.
global _int86
_int86:
    push bp
    mov bp, sp
    push si
    push di
    push bx                     ; BX is callee-save (cdecl/8086)
    mov ax, [bp+4]
    mov [cs:.int_op+1], al
    mov bx, [bp+6]
    mov ax, [bx+0]
    mov cx, [bx+4]
    mov dx, [bx+6]
    mov si, [bx+8]
    mov di, [bx+10]
    mov bx, [bx+2]
.int_op:
    int 21h
    push bx
    mov bx, [bp+8]
    mov [bx+0], ax
    pop ax
    mov [bx+2], ax
    mov [bx+4], cx
    mov [bx+6], dx
    mov [bx+8], si
    mov [bx+10], di
    pushf
    pop ax
    mov [bx+14], ax
    and ax, 1
    mov [bx+12], ax
    mov ax, [bx+0]
    pop bx
    pop di
    pop si
    pop bp
    ret

; int intdos(union REGS *in, union REGS *out) — int86(0x21, in, out).
; Stack (near pointers, 2 bytes each): [bp+4] inregs, [bp+6] outregs.
global _intdos
_intdos:
    push bp
    mov bp, sp
    push si
    push di
    push bx                     ; BX is callee-save (cdecl/8086)
    mov bx, [bp+4]
    mov ax, [bx+0]
    mov cx, [bx+4]
    mov dx, [bx+6]
    mov si, [bx+8]
    mov di, [bx+10]
    mov bx, [bx+2]
    int 21h
    push bx
    mov bx, [bp+6]
    mov [bx+0], ax
    pop ax
    mov [bx+2], ax
    mov [bx+4], cx
    mov [bx+6], dx
    mov [bx+8], si
    mov [bx+10], di
    pushf
    pop ax
    mov [bx+14], ax
    and ax, 1
    mov [bx+12], ax
    mov ax, [bx+0]
    pop bx
    pop di
    pop si
    pop bp
    ret
global _signal
_signal:
    mov ax, 0
    ret
global _sleep
_sleep:
    ret
global _stat
_stat:
    mov ax, -1
    ret
global _chmod
_chmod:
    mov ax, 0
    ret
global _mktemp
_mktemp:
    ; mktemp(template) — return template unchanged
    push bp
    mov bp, sp
    mov ax, [bp+4]
    pop bp
    ret
global _islower
_islower:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    cmp al, 'a'
    jb .no
    cmp al, 'z'
    ja .no
    mov ax, 1
    pop bp
    ret
.no:
    xor ax, ax
    pop bp
    ret
global _isupper
_isupper:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    cmp al, 'A'
    jb .no
    cmp al, 'Z'
    ja .no
    mov ax, 1
    pop bp
    ret
.no:
    xor ax, ax
    pop bp
    ret
; char *strrchr(char *s, int c) — near pointer (2 bytes), c is w (2 bytes).
global _strrchr
_strrchr:
    push bp
    mov bp, sp
    push si
    push bx
    mov si, [bp+4]   ; s
    mov bl, [bp+6]   ; c (low byte)
    xor ax, ax
.loop:
    mov dl, [si]
    cmp dl, 0
    je .done
    cmp dl, bl
    jne .skip
    mov ax, si
.skip:
    inc si
    jmp .loop
.done:
    pop bx
    pop si
    pop bp
    ret

; ============================================================================
; 32-bit divide / remainder soft helpers (referenced by qbe i8086 codegen)
; ============================================================================
;
; The 8086 has no 32-bit DIV/IDIV instruction.  When the QBE i8086 backend
; sees `Odiv`/`Oudiv`/`Orem`/`Ourem` on a Kl (32-bit) operand it emits a
; cdecl call to one of the helpers below:
;
;     _qbe_div32u   unsigned long quotient
;     _qbe_rem32u   unsigned long remainder
;     _qbe_div32s   signed   long quotient
;     _qbe_rem32s   signed   long remainder
;
; ABI:
;     args:  (long num, long denom)   — pushed cdecl right-to-left, so:
;            [bp+4..5]  = num   low word
;            [bp+6..7]  = num   high word
;            [bp+8..9]  = denom low word
;            [bp+10..11] = denom high word
;     return: DX:AX   (DX = high word, AX = low word)
;     clobbers: AX, CX, DX (caller-save in libstub cdecl)
;     preserves: BX, SI, DI (callee-save in libstub cdecl,
;                see [[libstub-cdecl-callee-save]])
;
; Division by zero is not trapped — quotient is implementation-defined
; (this implementation returns 0xFFFFFFFF, matching what x86 idiv would
; produce in the limit case).  Real CRT div-by-zero handling is out of
; scope; minic doesn't currently emit any check.
;
; Signed semantics: trunc-toward-zero quotient (C99 §6.5.5/6); remainder
; has the sign of the dividend (C99 §6.5.5/6).
; ============================================================================

; Shared shift-subtract body — args at [bp+4..11], expects BX/SI saved
; by the caller's prologue.  On exit:  DX:AX = quotient, CX:BX = remainder.
; Implemented as a NASM macro (not a `call`) so the [bp+N] offsets
; expand inside each public function's own frame.  That matters for the
; .EXE build: libstub_to_exe.py bumps every `[bp+N]` by +2 to account for
; the far-call return frame, and a `retn`-based internal helper would
; pick up those bumped offsets even though it was reached via near call.
%macro UDIVMOD32_BODY 0
    mov ax, [bp+4]           ; Q lo (numerator)
    mov dx, [bp+6]           ; Q hi
    xor bx, bx               ; R lo
    xor cx, cx               ; R hi
    mov si, 32
%%loop:
    shl ax, 1
    rcl dx, 1
    rcl bx, 1
    rcl cx, 1
    cmp cx, [bp+10]
    jb  %%skip
    ja  %%sub
    cmp bx, [bp+8]
    jb  %%skip
%%sub:
    sub bx, [bp+8]
    sbb cx, [bp+10]
    or  ax, 1
%%skip:
    dec si
    jnz %%loop
%endmacro

global _qbe_div32u
_qbe_div32u:
    push bp
    mov bp, sp
    push bx
    push si
    UDIVMOD32_BODY
    pop si
    pop bx
    pop bp
    ret

global _qbe_rem32u
_qbe_rem32u:
    push bp
    mov bp, sp
    push bx
    push si
    UDIVMOD32_BODY
    mov ax, bx
    mov dx, cx
    pop si
    pop bx
    pop bp
    ret

global _qbe_div32s
_qbe_div32s:
    push bp
    mov bp, sp
    push bx
    push si
    push di
    xor di, di               ; bit 0 = negate quotient
    test word [bp+6], 0x8000
    jz  .qs_npos
    inc di
    not word [bp+4]
    not word [bp+6]
    add word [bp+4], 1
    adc word [bp+6], 0
.qs_npos:
    test word [bp+10], 0x8000
    jz  .qs_dpos
    xor di, 1
    not word [bp+8]
    not word [bp+10]
    add word [bp+8], 1
    adc word [bp+10], 0
.qs_dpos:
    UDIVMOD32_BODY           ; DX:AX = |num| / |denom|
    test di, 1
    jz  .qs_done
    not ax
    not dx
    add ax, 1
    adc dx, 0
.qs_done:
    pop di
    pop si
    pop bx
    pop bp
    ret

global _qbe_rem32s
_qbe_rem32s:
    push bp
    mov bp, sp
    push bx
    push si
    push di
    xor di, di               ; bit 0 = negate remainder (sign follows num)
    test word [bp+6], 0x8000
    jz  .rs_npos
    inc di
    not word [bp+4]
    not word [bp+6]
    add word [bp+4], 1
    adc word [bp+6], 0
.rs_npos:
    test word [bp+10], 0x8000
    jz  .rs_dpos
    not word [bp+8]
    not word [bp+10]
    add word [bp+8], 1
    adc word [bp+10], 0
.rs_dpos:
    UDIVMOD32_BODY           ; CX:BX = |num| mod |denom|
    mov ax, bx
    mov dx, cx
    test di, 1
    jz  .rs_done
    not ax
    not dx
    add ax, 1
    adc dx, 0
.rs_done:
    pop di
    pop si
    pop bx
    pop bp
    ret

; ============================================================================
; int86x / intdosx / segread — the segment-aware members of the DOS API trio.
;
; These three MUST live before `_remove` in this file: tools/libstub_to_exe.py
; enters a SKIP region at `_remove` for the medium-model EXE rewrite (see
; [[libstub-to-exe-skip-region]]).
;
; ABI follows [[libstub-cdecl-callee-save]]: cdecl, BX/SI/DI/BP callee-save,
; pointers are 16-bit near (small/medium model code segment).
;
; Strategy for DS handling in int86x/intdosx:
;   1. Snapshot the desired DS from segregs->ds into CS-relative scratch
;      while DS still points at DGROUP.
;   2. Set ES from segregs->es early (ES isn't used for stack/data here).
;   3. Read inregs via DS, then mov ds, [cs:scratch] just before INT.
;   4. After INT, snapshot callee's ES/DS to CS-rel scratch, then
;      restore DS via `push ss; pop ds` (small/medium model: DS == SS).
;   5. Write outregs and segregs->{es,ds} via the now-restored DS.
;
; SS, CS are NOT loaded into the CPU — int86x docs say only DS/ES are loaded
; from segregs.  segregs->{cs,ss} are also NOT modified on return for the same
; reason; Microsoft C int86x only updates es and ds on exit.
; ============================================================================

; void segread(struct SREGS *segs)
; Snapshot ES/CS/SS/DS into the SREGS struct.  Trivial.
global _segread
_segread:
    push bp
    mov bp, sp
    push bx
    mov bx, [bp+4]
    mov [bx+0], es
    mov [bx+2], cs
    mov [bx+4], ss
    mov [bx+6], ds
    pop bx
    pop bp
    ret


; int int86x(int intno, union REGS *in, union REGS *out, struct SREGS *segs)
; Stack:  [bp+4] intno, [bp+6] in, [bp+8] out, [bp+10] segs.
global _int86x
_int86x:
    push bp
    mov bp, sp
    push si
    push di
    push bx
    push es
    push ds                      ; callee-save (we will overwrite DS)

    mov ax, [bp+4]
    mov [cs:.x_int_op+1], al     ; patch INT immediate

    ; Read segregs->ds (stash in CS-rel scratch) and segregs->es (load now).
    mov bx, [bp+10]
    mov ax, [bx+6]
    mov [cs:.x_desired_ds], ax
    mov ax, [bx+0]
    mov es, ax

    ; Load GPRs from inregs (DS still ours).
    mov bx, [bp+6]
    mov ax, [bx+0]
    mov cx, [bx+4]
    mov dx, [bx+6]
    mov si, [bx+8]
    mov di, [bx+10]
    mov bx, [bx+2]

    ; Last step before INT: swap DS to the caller-supplied value.
    push ax
    mov ax, [cs:.x_desired_ds]
    mov ds, ax
    pop ax
.x_int_op:
    int 21h

    ; Snapshot callee's ES/DS to CS-rel scratch, then restore our DS = SS.
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

    ; Write outregs via our DS.
    push bx
    mov bx, [bp+8]
    mov [bx+0], ax
    pop ax
    mov [bx+2], ax
    mov [bx+4], cx
    mov [bx+6], dx
    mov [bx+8], si
    mov [bx+10], di
    pushf
    pop ax
    mov [bx+14], ax
    and ax, 1
    mov [bx+12], ax
    mov ax, [bx+0]               ; return = result AX

    ; Write callee's ES/DS back into segregs->{es,ds} (cs/ss untouched).
    push ax
    mov bx, [bp+10]
    mov ax, [cs:.x_callee_es]
    mov [bx+0], ax
    mov ax, [cs:.x_callee_ds]
    mov [bx+6], ax
    pop ax

    pop ds
    pop es
    pop bx
    pop di
    pop si
    pop bp
    ret

.x_desired_ds: dw 0
.x_callee_ds:  dw 0
.x_callee_es:  dw 0


; int intdosx(union REGS *in, union REGS *out, struct SREGS *segs)
; Stack:  [bp+4] in, [bp+6] out, [bp+8] segs.
; Behaves as int86x(0x21, in, out, segs).  Body duplicated rather than
; tail-called because calling _int86x has different near/far ABI in COM
; vs. EXE builds (see comment on _intdos's split from _int86).
global _intdosx
_intdosx:
    push bp
    mov bp, sp
    push si
    push di
    push bx
    push es
    push ds

    mov bx, [bp+8]
    mov ax, [bx+6]
    mov [cs:.dx_desired_ds], ax
    mov ax, [bx+0]
    mov es, ax

    mov bx, [bp+4]
    mov ax, [bx+0]
    mov cx, [bx+4]
    mov dx, [bx+6]
    mov si, [bx+8]
    mov di, [bx+10]
    mov bx, [bx+2]

    push ax
    mov ax, [cs:.dx_desired_ds]
    mov ds, ax
    pop ax
    int 0x21

    push ax
    push ds
    pop ax
    mov [cs:.dx_callee_ds], ax
    push es
    pop ax
    mov [cs:.dx_callee_es], ax
    push ss
    pop ds
    pop ax

    push bx
    mov bx, [bp+6]
    mov [bx+0], ax
    pop ax
    mov [bx+2], ax
    mov [bx+4], cx
    mov [bx+6], dx
    mov [bx+8], si
    mov [bx+10], di
    pushf
    pop ax
    mov [bx+14], ax
    and ax, 1
    mov [bx+12], ax
    mov ax, [bx+0]

    push ax
    mov bx, [bp+8]
    mov ax, [cs:.dx_callee_es]
    mov [bx+0], ax
    mov ax, [cs:.dx_callee_ds]
    mov [bx+6], ax
    pop ax

    pop ds
    pop es
    pop bx
    pop di
    pop si
    pop bp
    ret

.dx_desired_ds: dw 0
.dx_callee_ds:  dw 0
.dx_callee_es:  dw 0


; ============================================================================
; High-level DOS API wrappers (Microsoft C / Turbo C names).
;
; Each is a thin shim over INT 10h / 16h / 21h.  Kept in asm so they can be
; called from C without minic having to build any extra translation units.
; ============================================================================

; void set_video_mode(int mode) — INT 10h AH=00h.
global _set_video_mode
_set_video_mode:
    push bp
    mov bp, sp
    mov ax, [bp+4]
    mov ah, 0
    int 10h
    pop bp
    ret


; void putpixel(int x, int y, unsigned char color) — VGA mode 13h direct write.
; Far-pokes 0xA000:y*320+x.  Caller is responsible for being in mode 13h.
; Args: [bp+4] x (w), [bp+6] y (w), [bp+8] color (w; low byte used).
global _putpixel
_putpixel:
    push bp
    mov bp, sp
    push bx
    push di
    push es
    mov ax, 0xA000
    mov es, ax
    mov ax, [bp+6]               ; y
    mov bx, 320
    mul bx                       ; DX:AX = y*320 (DX discarded; <64K for y<205)
    mov di, ax
    add di, [bp+4]               ; di = y*320 + x
    mov ax, [bp+8]
    mov [es:di], al
    pop es
    pop di
    pop bx
    pop bp
    ret


; int kbhit(void) — INT 16h AH=01h. ZF=0 means a key is waiting.
global _kbhit
_kbhit:
    mov ah, 1
    int 16h
    jz .no_key
    mov ax, 1
    ret
.no_key:
    xor ax, ax
    ret


; int getche(void) — INT 16h AH=00h, then echo via INT 21h AH=02h.
; Function/arrow keys return 0 (no echo); ASCII keys are echoed and returned.
global _getche
_getche:
    push bp
    mov bp, sp
    push bx
    xor ah, ah
    int 16h                      ; AH=scancode, AL=ASCII
    cmp al, 0
    je .ge_func
    mov bl, al
    mov dl, al
    mov ah, 2
    int 21h                      ; echo
    xor ax, ax
    mov al, bl
    pop bx
    pop bp
    ret
.ge_func:
    xor ax, ax
    pop bx
    pop bp
    ret


; int bdos(int func, int dx, int al) — Microsoft C compat.
; Calls INT 21h with AH=func, DX=dx, AL=al; returns AX.
; Stack: [bp+4] func, [bp+6] dx, [bp+8] al.
global _bdos
_bdos:
    push bp
    mov bp, sp
    push bx
    mov ax, [bp+4]               ; func (low byte)
    mov ah, al                   ; AH = func
    mov bx, [bp+8]
    mov al, bl                   ; AL = al
    mov dx, [bp+6]
    int 21h
    pop bx
    pop bp
    ret


; ============================================================================
; INT 33h mouse wrappers.  Returns 0/innocuous defaults when no driver loaded.
; ============================================================================

; int mouse_reset(void) — INT 33h AX=00h.  Returns AX (0xFFFF if installed).
global _mouse_reset
_mouse_reset:
    push bx
    xor ax, ax
    int 33h
    pop bx
    ret

; void mouse_show(void) — INT 33h AX=01h.
global _mouse_show
_mouse_show:
    push bx
    mov ax, 1
    int 33h
    pop bx
    ret

; void mouse_hide(void) — INT 33h AX=02h.
global _mouse_hide
_mouse_hide:
    push bx
    mov ax, 2
    int 33h
    pop bx
    ret

; void mouse_get_pos(int *x, int *y, int *buttons) — INT 33h AX=03h.
; Stack: [bp+4] x, [bp+6] y, [bp+8] buttons.
global _mouse_get_pos
_mouse_get_pos:
    push bp
    mov bp, sp
    push bx
    push si
    mov ax, 3
    int 33h
    ; AX=event flags (ignored), BX=buttons, CX=x, DX=y
    mov si, [bp+4]
    mov [si], cx
    mov si, [bp+6]
    mov [si], dx
    mov si, [bp+8]
    mov [si], bx
    pop si
    pop bx
    pop bp
    ret


; remove() is referenced but not in any source file we compile.
global _remove
_remove:
    mov ax, 0
    ret
