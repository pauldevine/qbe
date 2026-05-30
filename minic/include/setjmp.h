#ifndef _SETJMP_H
#define _SETJMP_H

/*
 * jmp_buf for the i8086 medium model (near data, far code).
 *
 * setjmp/longjmp are implemented in the far form inside
 * tools/libstub_to_exe.py (SETJMP_EXE); the near libstub.asm .COM build
 * does not provide them.  Only 7 of these 8 ints are used (caller BP,
 * resume SP, SI, DI, caller BX, return IP, return CS); the 8th is slack.
 */
typedef int jmp_buf[8];

int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);

#endif /* _SETJMP_H */
