/* limits.h -- C-Kermit header set (16-bit int, 32-bit long). */
#ifndef _LIMITS_H
#define _LIMITS_H
#define CHAR_BIT 8
#define SCHAR_MIN (-128)
#define SCHAR_MAX 127
#define UCHAR_MAX 255
#define CHAR_MIN SCHAR_MIN
#define CHAR_MAX SCHAR_MAX
#define MB_LEN_MAX 1
#define SHRT_MIN (-32767-1)
#define SHRT_MAX 32767
#define USHRT_MAX 65535U
#define INT_MIN (-32767-1)
#define INT_MAX 32767
#define UINT_MAX 65535U
#define LONG_MIN (-2147483647L-1)
#define LONG_MAX 2147483647L
#define ULONG_MAX 4294967295UL
#ifndef PATH_MAX
#define PATH_MAX 143          /* Watcom: max pathname excluding the NUL */
#endif
#define NAME_MAX 12
#define _POSIX_ARG_MAX 4096
#define ARG_MAX 128
#define OPEN_MAX 20
#endif
