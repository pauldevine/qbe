/* string.h - Minimal string.h for MiniC/DOS */
#ifndef _STRING_H
#define _STRING_H

#ifndef NULL
#define NULL 0
#endif

/* All functions declared without parameters for MiniC compatibility */
int strlen();
int strcpy();
int strncpy();
int strcat();
int strncat();
int strcmp();
int strncmp();
int strchr();
int strrchr();
int strstr();
int memcpy();
int memmove();
int memset();
int memcmp();
int strdup();

#endif
