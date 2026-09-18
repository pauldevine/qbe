/* dirent.h -- C-Kermit header set.  DOS find-first/next shape (Watcom's
   field names, so d_attr/d_size/d_date/d_time are available). */
#ifndef _DIRENT_H
#define _DIRENT_H
struct dirent {
	char d_dta[21];                 /* INT 21h find-first/next DTA state */
	char d_attr;
	unsigned short d_time;
	unsigned short d_date;
	long d_size;
	unsigned short d_ino;
	char d_first;
	char *d_openpath;
	char d_name[13];
};
typedef struct dirent DIR;
DIR *opendir(const char *path);
struct dirent *readdir(DIR *dir);
void rewinddir(DIR *dir);
int closedir(DIR *dir);
#endif
