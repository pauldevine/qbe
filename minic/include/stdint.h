/* stdint.h - Minimal stdint.h for MiniC / i8086 DOS target
 *
 * Integer widths on this target: char=8, short=16, int=16, long=32,
 * long long=32 (no true 64-bit type).  The exact-width 32-bit types are
 * therefore `long`, NOT `int`.
 *
 * Pointer-sized types depend on the memory model: near data (medium/small/
 * tiny) has 16-bit data pointers; far data (compact/large/huge) has 32-bit
 * data pointers.  Build scripts pass -DFAR_DATA for the far-data models.
 */

#ifndef _STDINT_H
#define _STDINT_H

/* Exact-width integer types */
typedef char int8_t;
typedef unsigned char uint8_t;

typedef short int16_t;
typedef unsigned short uint16_t;

typedef long int32_t;
typedef unsigned long uint32_t;

/* No native 64-bit type; alias to 32-bit `long` (do not rely on 64-bit range) */
typedef long int64_t;
typedef unsigned long uint64_t;

/* Largest integer type */
typedef long intmax_t;
typedef unsigned long uintmax_t;

/* Pointer-holding integer types (model-dependent width) */
#ifdef FAR_DATA
typedef long intptr_t;
typedef unsigned long uintptr_t;
#else
typedef int intptr_t;
typedef unsigned int uintptr_t;
#endif

/* Limits of exact-width types */
#define INT8_MAX    127
#define INT8_MIN    (-128)
#define UINT8_MAX   255
#define INT16_MAX   32767
#define INT16_MIN   (-32768)
#define UINT16_MAX  65535U
#define INT32_MAX   2147483647L
#define INT32_MIN   (-2147483647L-1)
#define UINT32_MAX  4294967295UL
#define INT64_MAX   2147483647L
#define INT64_MIN   (-2147483647L-1)
#define UINT64_MAX  4294967295UL
#define INTMAX_MAX  2147483647L
#define INTMAX_MIN  (-2147483647L-1)
#define UINTMAX_MAX 4294967295UL

/* Limits of pointer-holding types (model-dependent) */
#ifdef FAR_DATA
#define INTPTR_MAX  2147483647L
#define INTPTR_MIN  (-2147483647L-1)
#define UINTPTR_MAX 4294967295UL
#define SIZE_MAX    4294967295UL
#else
#define INTPTR_MAX  32767
#define INTPTR_MIN  (-32768)
#define UINTPTR_MAX 65535U
#define SIZE_MAX    65535U
#endif

/* MicroPython references INTPTR_UMAX (non-standard) for MP_UINT_MAX */
#define INTPTR_UMAX UINTPTR_MAX

#endif /* _STDINT_H */
