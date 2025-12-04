/*
 * STevie - ST editor for VI enthusiasts.    ...Tim Thompson...twitch!tjt...
 *
 * Extensive modifications by:  Tony Andrews       onecom!wldrdg!tony
 *
 * DOS port for MiniC/QBE 8086 compiler
 */

#include "stevie.h"

/*
 * This file shows how to display characters on the screen. This is
 * approach is something of an overkill. It's a remnant from the
 * original code that isn't worth messing with for now. TABS are
 * special-cased depending on the value of the "list" parameter.
 *
 * MiniC doesn't support global struct array initializers, so we
 * initialize the chars array dynamically in init_chars().
 */

void init_char(int i, char sz, char *str)
{
	chars[i].ch_size = sz;
	chars[i].ch_str = str;
}

void init_chars(void)
{
	int i;

	/* Initialize all printable chars (32-126) to size 1, NULL string */
	i = 32;
	while (i < 127)
	{
		init_char(i, 1, (char *)0);
		i = i + 1;
	}

	/* NUL character */
	init_char(0, 1, (char *)0);

	/* Control characters 1-31 */
	init_char(1, 2, "^A");
	init_char(2, 2, "^B");
	init_char(3, 2, "^C");
	init_char(4, 2, "^D");
	init_char(5, 2, "^E");
	init_char(6, 2, "^F");
	init_char(7, 2, "^G");
	init_char(8, 2, "^H");
	init_char(9, 2, "^I");
	init_char(10, 7, "[ERROR]");	/* newline shouldn't occur */
	init_char(11, 2, "^K");
	init_char(12, 2, "^L");
	init_char(13, 2, "^M");
	init_char(14, 2, "^N");
	init_char(15, 2, "^O");
	init_char(16, 2, "^P");
	init_char(17, 2, "^Q");
	init_char(18, 2, "^R");
	init_char(19, 2, "^S");
	init_char(20, 2, "^T");
	init_char(21, 2, "^U");
	init_char(22, 2, "^V");
	init_char(23, 2, "^W");
	init_char(24, 2, "^X");
	init_char(25, 2, "^Y");
	init_char(26, 2, "^Z");
	init_char(27, 2, "^[");
	init_char(28, 3, "^BS");	/* backslash - MiniC can't do \\ */
	init_char(29, 2, "^]");
	init_char(30, 2, "^^");
	init_char(31, 2, "^_");

	/* DEL character */
	init_char(127, 5, "[DEL]");
}
