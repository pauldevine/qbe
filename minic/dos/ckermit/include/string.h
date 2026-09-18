/* string.h -- C-Kermit header set. */
#ifndef _STRING_H
#define _STRING_H
#include <stddef.h>
void *memchr(const void *s, int c, size_t n);
int   memcmp(const void *a, const void *b, size_t n);
void *memcpy(void *dst, const void *src, size_t n);
void *memmove(void *dst, const void *src, size_t n);
void *memset(void *s, int c, size_t n);
char *strcat(char *dst, const char *src);
char *strchr(const char *s, int c);
int   strcmp(const char *a, const char *b);
int   strcoll(const char *a, const char *b);
char *strcpy(char *dst, const char *src);
size_t strcspn(const char *s, const char *reject);
char *strdup(const char *s);
char *strerror(int errnum);
size_t strlen(const char *s);
char *strncat(char *dst, const char *src, size_t n);
int   strncmp(const char *a, const char *b, size_t n);
char *strncpy(char *dst, const char *src, size_t n);
char *strpbrk(const char *s, const char *accept);
char *strrchr(const char *s, int c);
size_t strspn(const char *s, const char *accept);
char *strstr(const char *hay, const char *needle);
char *strtok(char *s, const char *delim);
int   stricmp(const char *a, const char *b);
int   strnicmp(const char *a, const char *b, size_t n);
#endif
