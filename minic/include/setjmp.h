#ifndef _SETJMP_H
#define _SETJMP_H

/*
 * jmp_buf for the i8086 (far code; near OR far data).
 *
 * setjmp/longjmp are implemented in the far form inside
 * tools/libstub_to_exe.py: SETJMP_EXE (medium/near-data, 2-byte env ptr)
 * and FAR_SETJMP_EXE (compact/large/huge, 4-byte far env ptr via ES:BX).
 * minic mangles setjmp/longjmp -> _far_setjmp/_far_longjmp under far-data
 * models (call_target_name's far_stdlib[]).  The near libstub.asm .COM
 * build does not provide them.  Only 7 of these 8 ints are used (caller BP,
 * resume SP, SI, DI, caller BX, return IP, return CS); the 8th is slack.
 * The buffer contents are always 16-bit words; only the POINTER to the
 * buffer widens to 4 bytes under a far-data model.
 */
typedef int jmp_buf[8];

int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);

#endif /* _SETJMP_H */
