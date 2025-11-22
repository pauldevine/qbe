/* stdlib.h - Minimal stdlib.h stub for MiniC
 * This is a stub - actual functions must be provided by the runtime
 */

#ifndef _STDLIB_H
#define _STDLIB_H

typedef unsigned long size_t;

/* Memory allocation - must be provided by runtime */
void *malloc(size_t size);
void free(void *ptr);

/* Other standard functions */
void exit(int status);
int abs(int x);

#endif /* _STDLIB_H */
