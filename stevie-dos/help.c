/*
 * STevie - ST editor for VI enthusiasts.    ...Tim Thompson...twitch!tjt...
 *
 * Extensive modifications by:  Tony Andrews       onecom!wldrdg!tony
 *
 * DOS port for MiniC/QBE 8086 compiler
 */

#include "stevie.h"

char *Version;
int help_row;

void init_help(void)
{
	Version = "STEVIE - Version 3.10";
	help_row = 0;
}

void longline(char *p)
{
	char *s;

	s = p;
	while (*s)
	{
		if (*s == '\n')
		{
			help_row = help_row + 1;
			windgoto(help_row, 0);
		}
		else
			outchar(*s);
		s = s + 1;
	}
}

#ifdef HELP

bool_t help(void)
{
	/*
	 * First Screen: Positioning within file, Adjusting the Screen
	 */

	outstr(T_ED);
	help_row = 0;
	windgoto(help_row, 0);

	longline("   Positioning within file\n");
	longline("   =======================\n");
	longline("      ^F             Forward screenfull\n");
	longline("      ^B             Backward screenfull\n");
	longline("      ^D             scroll down half screen\n");
	longline("      ^U             scroll up half screen\n");
	longline("      G              Goto line (end default)\n");
	longline("      ]]             next function\n");
	longline("      [[             previous function\n");
	longline("      /re            next occurence of regular expression 're'\n");
	longline("      ?re            prior occurence of regular expression 're'\n");
	longline("      n              repeat last / or ?\n");
	longline("      N              reverse last / or ?\n");
	longline("      %              find matching (, ), {, }, [, or ]\n");
	longline("\n");
	longline("   Adjusting the screen\n");
	longline("   ====================\n");
	longline("      ^L             Redraw the screen\n");
	longline("      ^E             scroll window down 1 line\n");
	longline("      ^Y             scroll window up 1 line\n");
	longline("      z<RETURN>      redraw, current line at top\n");
	longline("      z-             ... at bottom\n");
	longline("      z.             ... at center\n");

	windgoto(0, 52);
	longline(Version);

	help_row = Rows - 2;
	windgoto(help_row, 47);
	longline("<Press space bar to continue>\n");
	help_row = Rows - 1;
	windgoto(help_row, 47);
	longline("<Any other key will quit>");

	if (vgetc() != ' ')
		return TRUE;

	/*
	 * Second Screen: Character positioning
	 */

	outstr(T_ED);
	help_row = 0;
	windgoto(help_row, 0);

	longline("   Character Positioning\n");
	longline("   =====================\n");
	longline("      ^              first non-white\n");
	longline("      0              beginning of line\n");
	longline("      $              end of line\n");
	longline("      h              backward\n");
	longline("      l              forward\n");
	longline("      ^H             same as h\n");
	longline("      space          same as l\n");
	longline("      fx             find 'x' forward\n");
	longline("      Fx             find 'x' backward\n");
	longline("      tx             upto 'x' forward\n");
	longline("      Tx             upto 'x' backward\n");
	longline("      ;              Repeat last f, F, t, or T\n");
	longline("      ,              inverse of ;\n");
	longline("      |              to specified column\n");
	longline("      %              find matching (, ), {, }, [, or ]\n");

	help_row = Rows - 2;
	windgoto(help_row, 47);
	longline("<Press space bar to continue>\n");
	help_row = Rows - 1;
	windgoto(help_row, 47);
	longline("<Any other key will quit>");

	if (vgetc() != ' ')
		return TRUE;

	/*
	 * Third Screen: Line Positioning, Marking and Returning
	 */

	outstr(T_ED);
	help_row = 0;
	windgoto(help_row, 0);

	longline("    Line Positioning\n");
	longline("    =====================\n");
	longline("    H           home window line\n");
	longline("    L           last window line\n");
	longline("    M           middle window line\n");
	longline("    +           next line, at first non-white\n");
	longline("    -           previous line, at first non-white\n");
	longline("    CR          return, same as +\n");
	longline("    j           next line, same column\n");
	longline("    k           previous line, same column\n");
	longline("\n");
	longline("    Marking and Returning\n");
	longline("    =====================\n");
	longline("    ``          previous context\n");
	longline("    ''          ... at first non-white in line\n");
	longline("    mx          mark position with letter 'x'\n");
	longline("    `x          to mark 'x'\n");
	longline("    'x          ... at first non-white in line\n");

	help_row = Rows - 2;
	windgoto(help_row, 47);
	longline("<Press space bar to continue>\n");
	help_row = Rows - 1;
	windgoto(help_row, 47);
	longline("<Any other key will quit>");

	if (vgetc() != ' ')
		return TRUE;

	/*
	 * Fourth Screen: Insert & Replace
	 */

	outstr(T_ED);
	help_row = 0;
	windgoto(help_row, 0);

	longline("    Insert and Replace\n");
	longline("    ==================\n");
	longline("    a           append after cursor\n");
	longline("    i           insert before cursor\n");
	longline("    A           append at end of line\n");
	longline("    I           insert before first non-blank\n");
	longline("    o           open line below\n");
	longline("    O           open line above\n");
	longline("    rx          replace single char with 'x'\n");
	longline("    R           replace characters (not yet)\n");
	longline("\n");
	longline("    Words, sentences, paragraphs\n");
	longline("    ============================\n");
	longline("    w           word forward\n");
	longline("    b           back word\n");
	longline("    e           end of word\n");
	longline("    )           to next sentence (not yet)\n");
	longline("    }           to next paragraph (not yet)\n");
	longline("    (           back sentence (not yet)\n");
	longline("    {           back paragraph (not yet)\n");
	longline("    W           blank delimited word\n");
	longline("    B           back W\n");
	longline("    E           to end of W\n");

	help_row = Rows - 2;
	windgoto(help_row, 47);
	longline("<Press space bar to continue>\n");
	help_row = Rows - 1;
	windgoto(help_row, 47);
	longline("<Any other key will quit>");

	if (vgetc() != ' ')
		return TRUE;

	/*
	 * Fifth Screen: Misc. operations
	 */

	outstr(T_ED);
	help_row = 0;
	windgoto(help_row, 0);

	longline("    Undo  &  Redo\n");
	longline("    =============\n");
	longline("    u           undo last change (partially done)\n");
	longline("    U           restore current line (not yet)\n");
	longline("    .           repeat last change\n");
	longline("\n");
	longline("    File manipulation\n");
	longline("    =================\n");
	longline("    :w          write back changes\n");
	longline("    :wq         write and quit\n");
	longline("    :x          write if modified, and quit\n");
	longline("    :q          quit\n");
	longline("    :q!         quit, discard changes\n");
	longline("    :e name     edit file 'name'\n");
	longline("    :e!         reedit, discard changes\n");
	longline("    :e #        edit alternate file\n");
	longline("    :w name     write file 'name'\n");
	longline("    :n          edit next file in arglist\n");
	longline("    :rew        rewind arglist\n");
	longline("    :f          show current file and lines\n");
	longline("    :f file     change current file name\n");
	longline("    :ta tag     to tag file entry 'tag'\n");
	longline("    ^]          :ta, current word is tag\n");

	help_row = Rows - 2;
	windgoto(help_row, 47);
	longline("<Press space bar to continue>\n");
	help_row = Rows - 1;
	windgoto(help_row, 47);
	longline("<Any other key will quit>");

	if (vgetc() != ' ')
		return TRUE;

	/*
	 * Sixth Screen: Operators, Misc. operations, Yank & Put
	 */

	outstr(T_ED);
	help_row = 0;
	windgoto(help_row, 0);

	longline("    Operators (double to affect lines)\n");
	longline("    ==================================\n");
	longline("    d           delete\n");
	longline("    c           change\n");
	longline("    <           left shift\n");
	longline("    >           right shift\n");
	longline("    y           yank to buffer\n");
	longline("\n");
	longline("    Miscellaneous operations\n");
	longline("    ========================\n");
	longline("    C           change rest of line\n");
	longline("    D           delete rest of line\n");
	longline("    s           substitute chars\n");
	longline("    S           substitute lines (not yet)\n");
	longline("    J           join lines\n");
	longline("    x           delete characters\n");
	longline("    X           ... before cursor\n");
	longline("\n");
	longline("    Yank and Put\n");
	longline("    ============\n");
	longline("    p           put back text\n");
	longline("    P           put before\n");
	longline("    Y           yank lines");

	help_row = Rows - 1;
	windgoto(help_row, 47);
	longline("<Press any key>");

	vgetc();

	return TRUE;
}

#else

bool_t help(void)
{
	msg("Sorry, help not configured");
	return FALSE;
}

#endif
