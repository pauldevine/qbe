/* stdio.h -- C-Kermit header set.  FILE has the layout the newlibc stdio
   (dos_printf.c / newlibc libgloss) and dos_libc.c's std streams use:
   struct __sFILE { int _file; }.  Programs treat it as opaque. */
#ifndef _STDIO_H
#define _STDIO_H
#include <stddef.h>
#include <stdarg.h>
struct __sFILE {
	int _file;
};
typedef struct __sFILE FILE;
typedef long fpos_t;
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;
#define EOF (-1)
#define BUFSIZ 512
#define FILENAME_MAX 144
#ifndef PATH_MAX
#define PATH_MAX 143          /* Watcom defines it here too */
#endif
#define FOPEN_MAX 20
#define L_tmpnam 13
#define P_tmpdir "\\"
#define TMP_MAX 26
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2
#ifndef SEEK_SET
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#endif
int   printf(const char *fmt, ...);
int   fprintf(FILE *fp, const char *fmt, ...);
int   sprintf(char *buf, const char *fmt, ...);
int   snprintf(char *buf, size_t n, const char *fmt, ...);
int   vprintf(const char *fmt, va_list ap);
int   vfprintf(FILE *fp, const char *fmt, va_list ap);
int   vsprintf(char *buf, const char *fmt, va_list ap);
int   vsnprintf(char *buf, size_t n, const char *fmt, va_list ap);
int   scanf(const char *fmt, ...);
int   fscanf(FILE *fp, const char *fmt, ...);
int   sscanf(const char *buf, const char *fmt, ...);
FILE *fopen(const char *path, const char *mode);
FILE *fdopen(int fd, const char *mode);
FILE *freopen(const char *path, const char *mode, FILE *fp);
int   fclose(FILE *fp);
int   fflush(FILE *fp);
size_t fread(void *buf, size_t size, size_t n, FILE *fp);
size_t fwrite(const void *buf, size_t size, size_t n, FILE *fp);
int   fgetc(FILE *fp);
int   getc(FILE *fp);
int   getchar(void);
char *fgets(char *buf, int n, FILE *fp);
char *gets(char *buf);
int   fputc(int c, FILE *fp);
int   putc(int c, FILE *fp);
int   putchar(int c);
int   fputs(const char *s, FILE *fp);
int   puts(const char *s);
int   ungetc(int c, FILE *fp);
int   fseek(FILE *fp, long off, int whence);
long  ftell(FILE *fp);
void  rewind(FILE *fp);
int   feof(FILE *fp);
int   ferror(FILE *fp);
void  clearerr(FILE *fp);
int   fileno(FILE *fp);
void  setbuf(FILE *fp, char *buf);
int   setvbuf(FILE *fp, char *buf, int mode, size_t size);
void  perror(const char *s);
int   remove(const char *path);
int   rename(const char *oldp, const char *newp);
FILE *tmpfile(void);
char *tmpnam(char *buf);
#endif
