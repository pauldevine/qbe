/* Simple getopt replacement for DOS/OpenWatcom */
#ifndef DOSGETOPT_H
#define DOSGETOPT_H

extern char *optarg;
extern int optind;
extern int opterr;
extern int optopt;

int getopt(int argc, char * const argv[], const char *optstring);

#endif
