; DOS Runtime Startup (crt0.asm)
; Phase 1 - Minimal DOS startup code for i8086
; This is the entry point for all DOS programs compiled with QBE

; DOS COM files start at 100h (after PSP)
; DOS EXE files have a relocatable entry point

BITS 16                 ; 16-bit code for 8086
CPU 8086                ; Target 8086 CPU

section .text

global _start
extern _main

_start:
	; DOS has already set up:
	; - DS, ES = PSP segment
	; - SS:SP = stack (for EXE files)
	; - CS:IP = this code

	; For .COM files, setup stack ourselves
	; For .EXE files, linker sets up stack

	; Call the C main() function
	call _main
	; main() return value is in AX

	; Exit to DOS with return code from main()
	; INT 21h, AH=4Ch - Terminate with return code
	; AL = return code (already in AX from main)
	mov ah, 4Ch
	int 21h

	; Should never reach here
	; But just in case, infinite loop
	jmp $
