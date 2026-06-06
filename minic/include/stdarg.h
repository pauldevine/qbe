#ifndef _STDARG_H
#define _STDARG_H

/*
 * minic <stdarg.h> for the i8086 cdecl ABI.
 *
 * A va_list is a plain pointer that walks the variadic arguments the caller
 * pushed on the stack just past the named parameters.  Under a far-data model
 * (compact/large/huge) `char *` is a 4-byte far pointer whose segment is SS
 * (the stack); under a near-data model (tiny/small/medium) DS==SS so a 2-byte
 * near pointer suffices.  The backend's `vargp` op (emitted by minic for the
 * __builtin_va_argptr() call below) produces the correctly-segmented pointer to
 * the first vararg.
 *
 * Each argument occupies a whole number of 16-bit stack words (the cdecl
 * minimum), so va_arg advances by sizeof(type) rounded up to 2.  This matches
 * how minic's selcall pushes arguments (>=2 bytes each, 4 for long/far-ptr).
 */

typedef char *va_list;

/* __builtin_va_argptr() is recognised by name in minic's call() handler
 * (it emits the i8086 `vargp` op and returns a void*), so no prototype is
 * needed — and declaring one as `void *f(void)` trips a minic grammar gap
 * (pointer return + explicit `(void)` params in a prototype). */

#define _VA_SLOT(type) ((sizeof(type) + 1) & ~1)

#define va_start(ap, last) ((ap) = (va_list)__builtin_va_argptr())
#define va_arg(ap, type) \
	(*(type *)(((ap) += _VA_SLOT(type)) - _VA_SLOT(type)))
#define va_end(ap) ((void)(ap))
#define va_copy(dst, src) ((dst) = (src))

#endif /* _STDARG_H */
