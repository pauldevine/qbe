#ifndef _DOS_H
#define _DOS_H

/* Minimal MS-DOS-compatible <dos.h> stub for the QBE/i8086 toolchain.
 * Provides the REGS union and int86 / intdos prototypes that legacy
 * DOS C programs (Microsoft C, Turbo C, Lattice) expect. */

struct WORDREGS {
	unsigned short ax;
	unsigned short bx;
	unsigned short cx;
	unsigned short dx;
	unsigned short si;
	unsigned short di;
	unsigned short cflag;
	unsigned short flags;
};

struct BYTEREGS {
	unsigned char al;
	unsigned char ah;
	unsigned char bl;
	unsigned char bh;
	unsigned char cl;
	unsigned char ch;
	unsigned char dl;
	unsigned char dh;
};

union REGS {
	struct WORDREGS x;
	struct BYTEREGS h;
};

struct SREGS {
	unsigned short es;
	unsigned short cs;
	unsigned short ss;
	unsigned short ds;
};

extern int int86();
extern int int86x();
extern int intdos();
extern int intdosx();
extern void segread();

#define FP_SEG(fp) ((unsigned short)(((unsigned long)(fp)) >> 16))
#define FP_OFF(fp) ((unsigned short)((unsigned long)(fp)))

#endif /* _DOS_H */
