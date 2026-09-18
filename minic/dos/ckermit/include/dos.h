/* dos.h -- C-Kermit header set.  union REGS / struct SREGS match the word
   layout the int86 runtime (minic/dos/dos_syscall*.asm) reads: ax bx cx dx
   si di cflag flags.  `w` is Watcom's name for the word view, `x` Turbo C's. */
#ifndef _DOS_H
#define _DOS_H
struct WORDREGS {
	unsigned short ax, bx, cx, dx, si, di, cflag, flags;
};
struct BYTEREGS {
	unsigned char al, ah, bl, bh, cl, ch, dl, dh;
};
union REGS {
	struct WORDREGS x;
	struct WORDREGS w;
	struct BYTEREGS h;
};
struct SREGS {
	unsigned short es, cs, ss, ds;
};
struct dostime_t {
	unsigned char hour, minute, second, hsecond;
};
struct dosdate_t {
	unsigned char day, month;
	unsigned short year;
	unsigned char dayofweek;
};
#define _A_NORMAL 0x00
#define _A_RDONLY 0x01
#define _A_HIDDEN 0x02
#define _A_SYSTEM 0x04
#define _A_VOLID  0x08
#define _A_SUBDIR 0x10
#define _A_ARCH   0x20
int  int86(int intno, union REGS *in, union REGS *out);
int  int86x(int intno, union REGS *in, union REGS *out, struct SREGS *s);
int  intdos(union REGS *in, union REGS *out);
int  intdosx(union REGS *in, union REGS *out, struct SREGS *s);
void segread(struct SREGS *s);
void _disable(void);
void _enable(void);
void _dos_gettime(struct dostime_t *t);
void _dos_getdate(struct dosdate_t *d);
unsigned _dos_getfileattr(const char *path, unsigned *attr);
unsigned _dos_setfileattr(const char *path, unsigned attr);
void __far *_dos_getvect(unsigned intno);
void _dos_setvect(unsigned intno, void __far *handler);
#define FP_SEG(fp) ((unsigned short)(((unsigned long)(void __far *)(fp)) >> 16))
#define FP_OFF(fp) ((unsigned short)((unsigned long)(void __far *)(fp)))
#define MK_FP(seg, ofs) ((void __far *)(((unsigned long)(seg) << 16) | (unsigned short)(ofs)))
#endif
