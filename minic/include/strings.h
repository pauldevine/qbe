/* strings.h - BSD-style; mostly aliases to <string.h>.
 * Stevie only uses this when BSD is defined in env.h, so this is mostly
 * a placeholder for portability.
 */

#ifndef _STRINGS_H
#define _STRINGS_H

#include <string.h>

int bcmp(void *s1, void *s2, unsigned long n);
void bcopy(void *src, void *dst, unsigned long n);
void bzero(void *s, unsigned long n);
char *index(char *s, int c);
char *rindex(char *s, int c);

#endif /* _STRINGS_H */
