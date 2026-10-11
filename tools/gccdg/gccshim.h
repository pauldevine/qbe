/* gccshim.h -- GCC-isms the gcc.dg runtime tests lean on, mapped onto what
 * minic + the libstub-free runtime provide.  Included first by every wrapped
 * test (tools/gccdg/wrap.sh).  __attribute__, typeof, __asm__ and the C11
 * keywords are minic-native and need nothing here. */
#ifndef GCCDG_SHIM_H
#define GCCDG_SHIM_H
/* libm names: minic/include/math.h maps fabs()/sqrt()/... onto the
 * soft-float sf_* implementations in minic/dos/softfloat.c, which
 * tools/gccdg/run.sh links (--softfloat).  A test that declares
 * `extern double fabs(double)` itself still resolves through the macro. */
#include <math.h>
#define __builtin_abort abort
#define __builtin_exit exit
#define __builtin_memcpy memcpy
#define __builtin_memmove memmove
#define __builtin_memset memset
#define __builtin_memcmp memcmp
#define __builtin_strlen strlen
#define __builtin_strcpy strcpy
#define __builtin_strncpy strncpy
#define __builtin_strcmp strcmp
#define __builtin_strncmp strncmp
#define __builtin_strcat strcat
#define __builtin_strchr strchr
#define __builtin_printf printf
#define __builtin_sprintf sprintf
#define __builtin_malloc malloc
#define __builtin_free free
#define __builtin_expect(x, y) (x)
#define __builtin_unreachable() abort()
#define __builtin_trap() abort()
#define __builtin_va_list char *
#define __extension__
#define __restrict
#define __inline__ inline
#define __inline inline
#define __signed__ signed
#define __volatile__ volatile
#define __asm__ asm
#define __FUNCTION__ "fn"
#define __func__ "fn"
#endif
