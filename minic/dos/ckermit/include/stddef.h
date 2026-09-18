/* stddef.h -- C-Kermit header set (see README.md). */
#ifndef _STDDEF_H
#define _STDDEF_H
typedef unsigned int size_t;
typedef long ptrdiff_t;         /* far-data models: pointer differences are 32-bit */
typedef unsigned short wchar_t;
#ifndef NULL
#define NULL ((void *)0)
#endif
#define offsetof(type, member) ((size_t)&((type *)0)->member)
#endif
