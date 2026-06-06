/* string.h - Minimal string.h stub for MiniC / DOS target
 * Implementations live in libstub.asm / libc.c.
 */

#ifndef _STRING_H
#define _STRING_H

#include <stddef.h>

int strcmp(char *s1, char *s2);
int strncmp(char *s1, char *s2, size_t n);
size_t strlen(char *s);
char *strcpy(char *dst, char *src);
char *strncpy(char *dst, char *src, size_t n);
char *strcat(char *dst, char *src);
char *strncat(char *dst, char *src, size_t n);
char *strchr(char *s, int c);
char *strrchr(char *s, int c);
char *strstr(char *haystack, char *needle);
size_t strspn(char *s, char *accept);
size_t strcspn(char *s, char *reject);
char *strpbrk(char *s, char *accept);
char *strtok(char *s, char *delim);

void *memcpy(void *dst, void *src, size_t n);
void *memmove(void *dst, void *src, size_t n);
void *memset(void *s, int c, size_t n);
int memcmp(void *s1, void *s2, size_t n);
void *memchr(void *s, int c, size_t n);

#endif /* _STRING_H */
