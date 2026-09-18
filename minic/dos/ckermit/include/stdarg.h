/* stdarg.h -- C-Kermit header set.  Same scheme as minic/include/stdarg.h:
   arguments are 2-byte-aligned slots above the last named parameter. */
#ifndef _STDARG_H
#define _STDARG_H
typedef char *va_list;
#define _VA_SLOT(type) ((sizeof(type) + 1) & ~1)
#define va_start(ap, last) ((ap) = (va_list)__builtin_va_argptr())
#define va_arg(ap, type) \
	(*(type *)(((ap) += _VA_SLOT(type)) - _VA_SLOT(type)))
#define va_end(ap) ((void)(ap))
#define va_copy(dst, src) ((dst) = (src))
#endif
