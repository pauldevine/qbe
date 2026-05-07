#ifndef _SIGNAL_H
#define _SIGNAL_H

/* Minimal <signal.h> stub for the QBE/i8086 toolchain.  Stevie uses
 * SIGINT to catch user interrupts; everything else is no-ops. */

typedef int sig_atomic_t;

#define SIG_DFL  ((void (*)(int))0)
#define SIG_IGN  ((void (*)(int))1)
#define SIG_ERR  ((void (*)(int))-1)

#define SIGABRT  6
#define SIGFPE   8
#define SIGILL   4
#define SIGINT   2
#define SIGSEGV 11
#define SIGTERM 15

extern int signal();
extern int raise();

#endif /* _SIGNAL_H */
