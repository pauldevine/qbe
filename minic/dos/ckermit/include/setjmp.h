/* setjmp.h -- C-Kermit header set.  Same jmp_buf as minic/include/setjmp.h
   (the libstub-free setjmp_rt.asm layout). */
#ifndef _SETJMP_H
#define _SETJMP_H
typedef int jmp_buf[8];
int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);
#endif
