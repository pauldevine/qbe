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

/* High-level DOS API wrappers (Microsoft C / Turbo C names).  These are
 * thin shims over INT 10h / 16h / 21h, implemented in asm in libstub. */
extern void set_video_mode();   /* INT 10h AH=00h */
extern void putpixel();         /* mode 13h far-poke */
extern int  kbhit();            /* INT 16h AH=01h: nonzero if key waiting */
extern int  getche();           /* INT 16h AH=00h + echo */
extern int  bdos();             /* bdos(func, dx, al) → INT 21h */

#define FP_SEG(fp) ((unsigned short)(((unsigned long)(fp)) >> 16))
#define FP_OFF(fp) ((unsigned short)((unsigned long)(fp)))
#define MK_FP(seg, ofs) ((void far *)(((unsigned long)(seg) << 16) | (unsigned short)(ofs)))

#endif /* _DOS_H */
