/* dirent.h - triage shim for the §6a newlibc sweep.
 * Matches libgloss/dirent.c: DIR is struct __msdos_DIR (defined there);
 * the DOS/Watcom-shaped dirent fields it fills, d_name sized for VFS
 * 8.3 names (vfs.h name[13]). */
#ifndef _DIRENT_H
#define _DIRENT_H

struct dirent {
    char d_dta[21];
    char d_attr;
    unsigned short d_time;
    unsigned short d_date;
    long d_size;
    char d_name[13];
};

typedef struct __msdos_DIR DIR;

DIR *opendir(const char *path);
struct dirent *readdir(DIR *dir);
int closedir(DIR *dir);

#endif /* _DIRENT_H */
