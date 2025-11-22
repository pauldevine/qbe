; DOS System Call Wrappers
; Phase 1 - Basic DOS INT 21h syscalls

BITS 16
CPU 8086

section .text

; void dos_exit(int code)
; Exit to DOS with return code
global _dos_exit
_dos_exit:
	push bp
	mov bp, sp
	mov ax, [bp+4]          ; Get exit code from first argument
	mov ah, 4Ch             ; DOS function 4Ch - terminate
	int 21h
	; Never returns

; int dos_putchar(int ch)
; Write a single character to stdout
; Returns: character written, or -1 on error
global _dos_putchar
_dos_putchar:
	push bp
	mov bp, sp
	mov dx, [bp+4]          ; Get character from first argument
	mov ah, 02h             ; DOS function 02h - write character
	int 21h
	xor ah, ah              ; Return value in AX (AL has character)
	pop bp
	ret

; void dos_write_string(const char *str)
; Write a '$'-terminated string to stdout
; (DOS style string - terminated with '$' not null)
global _dos_write_string
_dos_write_string:
	push bp
	mov bp, sp
	mov dx, [bp+4]          ; Get string pointer
	mov ah, 09h             ; DOS function 09h - write string
	int 21h
	pop bp
	ret
