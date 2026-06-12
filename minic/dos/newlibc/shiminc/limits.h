/* limits.h - triage shim for the §6a newlibc sweep (16-bit int target). */
#ifndef _LIMITS_H
#define _LIMITS_H

#define CHAR_BIT 8
#define SCHAR_MIN (-128)
#define SCHAR_MAX 127
#define UCHAR_MAX 255
#define CHAR_MIN (-128)
#define CHAR_MAX 127
#define SHRT_MIN (-32767-1)
#define SHRT_MAX 32767
#define USHRT_MAX 65535U
#define INT_MIN (-32767-1)
#define INT_MAX 32767
#define UINT_MAX 65535U
#define LONG_MIN (-2147483647L-1)
#define LONG_MAX 2147483647L
#define ULONG_MAX 4294967295UL
#define PATH_MAX 64
#define NAME_MAX 12

#endif /* _LIMITS_H */
