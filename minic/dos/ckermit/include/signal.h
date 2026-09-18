/* signal.h -- C-Kermit header set.  Signal numbers are Open Watcom's (the
   port's reference; ckvictor.h adds SIGHUP=20, SIGQUIT=21 and maps SIGALRM
   onto SIGUSR3). */
#ifndef _SIGNAL_H
#define _SIGNAL_H
typedef int sig_atomic_t;
typedef void (*__sig_func)(int);
#define SIG_IGN ((__sig_func)1)
#define SIG_DFL ((__sig_func)2)
#define SIG_ERR ((__sig_func)3)
#define SIGABRT  1
#define SIGFPE   2
#define SIGILL   3
#define SIGINT   4
#define SIGSEGV  5
#define SIGTERM  6
#define SIGBREAK 7
#define SIGUSR1  8
#define SIGUSR2  9
#define SIGUSR3  10
#define SIGIDIVZ 11
#define SIGIOVFL 12
void (*signal(int sig, void (*func)(int)))(int);
int raise(int sig);
#endif
