/*
 * STevie - ST editor for VI enthusiasts.    ...Tim Thompson...twitch!tjt...
 *
 * Extensive modifications by:  Tony Andrews       onecom!wldrdg!tony
 *
 * DOS port for MiniC/QBE 8086 compiler
 *
 * System-dependent routines for DOS.
 */

#include "stevie.h"

/*
 * DOS-specific I/O functions
 * These would typically be implemented using BIOS interrupts:
 * - INT 10h for video services
 * - INT 16h for keyboard services
 * - INT 21h for DOS services
 *
 * For now, we declare them as external functions to be linked
 * from a runtime library or assembly stubs.
 */

/*
 * External DOS/BIOS interface functions - these will be linked from
 * assembly stubs or a runtime library.
 * MiniC doesn't support extern function declarations, so we rely on
 * implicit declaration. The functions must be linked at link time.
 */

/*
 * Input buffer for type-ahead
 */
#define IBUFSZ 128
int dos_inbuf_count;
int *dos_inbuf_data;

void init_dos(void)
{
	dos_inbuf_count = 0;
	dos_inbuf_data = (int *)alloc(IBUFSZ * 4);
}

/*
 * inchar() - get a character from the keyboard
 *
 * Certain special keys are mapped to values above 0x80.
 */
int inchar(void)
{
	int c;
	int scan;

	while (1)
	{
		/* Check input buffer first */
		if (dos_inbuf_count > 0)
		{
			c = dos_inbuf_data[0];
			dos_inbuf_count = dos_inbuf_count - 1;
			/* Shift buffer contents down */
			scan = 0;
			while (scan < dos_inbuf_count)
			{
				dos_inbuf_data[scan] = dos_inbuf_data[scan + 1];
				scan = scan + 1;
			}
		}
		else
		{
			c = dos_getch();
		}

		/* Check for extended key (scan code) */
		if ((c & 0xFF) == 0)
		{
			scan = (c >> 8) & 0xFF;
			/* Map extended keys */
			if (scan == 0x3B) return K_HELP;    /* F1 = Help */
			if (scan == 0x48) return K_UARROW;  /* Up arrow */
			if (scan == 0x50) return K_DARROW;  /* Down arrow */
			if (scan == 0x4B) return K_LARROW;  /* Left arrow */
			if (scan == 0x4D) return K_RARROW;  /* Right arrow */
			if (scan == 0x47) return K_HOME;    /* Home */
			if (scan == 0x52) return K_INSERT;  /* Insert */
			/* Unknown extended key */
			beep();
		}
		else
		{
			return c & 0xFF;
		}
	}
}

/*
 * get_inchars - buffer any pending keyboard input
 */
void get_inchars(void)
{
	while (dos_kbhit())
	{
		if (dos_inbuf_count >= IBUFSZ)
		{
			dos_getch();  /* Discard */
			beep();
		}
		else
		{
			dos_inbuf_data[dos_inbuf_count] = dos_getch();
			dos_inbuf_count = dos_inbuf_count + 1;
		}
	}
}

void outchar(int c)
{
	get_inchars();
	dos_putch(c);
}

void outstr(char *s)
{
	get_inchars();
	while (*s)
	{
		dos_putch(*s);
		s = s + 1;
	}
}

void beep(void)
{
	if (P(P_VB))
	{
		/* Visual bell - flash screen */
		/* For now, just do nothing */
	}
	else
	{
		outchar(7);  /* Bell character - MiniC doesn't support octal escapes */
	}
}

void windinit(void)
{
	int rows;
	int cols;

	dos_getvidmode(&rows, &cols);
	Rows = rows;
	Columns = cols;
	P(P_LI) = rows;
}

void windexit(int r)
{
	dos_exit(r);
}

void windgoto(int r, int c)
{
	dos_gotoxy(c, r);
}

void sleep(int n)
{
	/* Simple busy-wait delay */
	/* n is in seconds - not accurate but functional */
	int i;
	int j;

	i = 0;
	while (i < n)
	{
		j = 0;
		while (j < 10000)
		{
			j = j + 1;
		}
		i = i + 1;
	}
}

void delay(void)
{
	int n;

	n = 0;
	while (n < 8000)
	{
		n = n + 1;
	}
}

FILE *fopenb(char *fname, char *mode)
{
	/* In DOS, binary mode is often the default or uses "rb"/"wb" */
	return (FILE *)fopen(fname, mode);
}

/*
 * screenclear - clear the screen
 */
void screenclear(void)
{
	dos_cls();
}

/*
 * getout - exit the editor
 */
void getout(void)
{
	windgoto(Rows - 1, 0);
	outchar('\n');
	windexit(0);
}
