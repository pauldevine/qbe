/* stdint.h - Minimal stdint.h for MiniC
 * Compatible with MiniC grammar limitations
 */

#ifndef _STDINT_H
#define _STDINT_H

/* Exact-width integer types */
typedef char int8_t;
typedef unsigned char uint8_t;

typedef short int16_t;
typedef unsigned short uint16_t;

typedef int int32_t;
typedef unsigned int uint32_t;

typedef long int64_t;
typedef unsigned long uint64_t;

/* Other common types */
typedef unsigned long size_t;

#endif /* _STDINT_H */
