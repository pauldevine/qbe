/*
 * STevie - ST editor for VI enthusiasts.    ...Tim Thompson...twitch!tjt...
 *
 * Extensive modifications by:  Tony Andrews       onecom!wldrdg!tony
 *
 * DOS port for MiniC/QBE 8086 compiler
 */

#include "stevie.h"

/*
 * MiniC doesn't support static variables - use globals instead
 */
char *cmdl_altfile;		/* alternate file */
int cmdl_altline;		/* line # in alternate file */

char *cmdl_nowrtmsg;

extern char **files;		/* used for "n" and "rew" */
extern int numfiles;
extern int curfile;

/*
 * Range variables - MiniC doesn't support local struct variables
 * So we use global pointers
 */
LPTR *cmdl_l_pos;
LPTR *cmdl_u_pos;

bool_t cmdl_interactive;	/* TRUE if we're reading a real command line */

#define CMDSZ 100		/* size of the command buffer */
#define LSIZE 512		/* max. size of a line in the tags file */

/* Global buffers for functions that need local arrays */
char *cmdl_buff;		/* command buffer for readcmdline */
char *cmdl_lbuf;		/* line buffer for dotag */
LPTR *cmdl_getline_pos;		/* return value for get_line */

void init_cmdline(void)
{
	cmdl_altfile = (char *)0;
	cmdl_altline = 0;
	cmdl_nowrtmsg = "No write since last change (use ! to override)";
	cmdl_l_pos = (LPTR *)alloc(16);
	cmdl_u_pos = (LPTR *)alloc(16);
	cmdl_buff = (char *)alloc(CMDSZ);
	cmdl_lbuf = (char *)alloc(LSIZE);
	cmdl_getline_pos = (LPTR *)alloc(16);
}

void gotocmd(bool_t clr, bool_t fresh, int firstc)
{
	windgoto(Rows - 1, 0);
	if (clr)
		outstr(T_EL);		/* clear the bottom line */
	if (firstc)
		outchar(firstc);
}

/*
 * msg(s) - displays the string 's' on the status line
 */
void msg(char *s)
{
	gotocmd(TRUE, TRUE, 0);
	outstr(s);
}

void smsg(char *s, int a1, int a2, int a3, int a4, int a5, int a6, int a7, int a8, int a9)
{
	char *sbuf;

	sbuf = (char *)alloc(80);
	sprintf(sbuf, s, a1, a2, a3, a4, a5, a6, a7, a8, a9);
	msg(sbuf);
	free(sbuf);
}

/*
 * emsg() - display an error message
 *
 * Rings the bell, if appropriate, and calls message() to do the real work
 */
void emsg(char *s)
{
	if (P(P_EB))
		beep();
	msg(s);
}

void wait_return(void)
{
	char c;

	outstr("Press RETURN to continue");
	c = vgetc();
	while (c != '\r' && c != '\n')
	{
		c = vgetc();
	}

	screenclear();
	updatescreen();
}

void badcmd(void)
{
	if (cmdl_interactive)
		emsg("Unrecognized command");
}

LPTR *get_line(char **cp)
{
	LPTR *lp;
	char *p;
	char c;
	int lnum;
	int neg;
	int found;

	cmdl_getline_pos->index = 0;	/* shouldn't matter... check back later */

	p = *cp;
	/*
	 * Determine the basic form, if present.
	 */
	c = *p;
	p = p + 1;

	found = 0;

	if (c == '$')
	{
		cmdl_getline_pos->linep = Fileend->linep->prev;
		found = 1;
	}
	if (c == '.' && found == 0)
	{
		cmdl_getline_pos->linep = Curschar->linep;
		found = 1;
	}
	if (c == '\'' && found == 0)
	{
		lp = (LPTR *)getmark(*p);
		p = p + 1;
		if (lp == NULL)
		{
			emsg("Unknown mark");
			return (LPTR *)0;
		}
		*cmdl_getline_pos = *lp;
		found = 1;
	}
	if (c >= '0' && c <= '9' && found == 0)
	{
		lnum = c - '0';
		while (*p >= '0' && *p <= '9')
		{
			lnum = (lnum * 10) + (*p - '0');
			p = p + 1;
		}
		lp = (LPTR *)gotoline(lnum);
		*cmdl_getline_pos = *lp;
		found = 1;
	}
	if (found == 0)
	{
		return (LPTR *)0;
	}

	while (*p != NUL && (*p == ' ' || *p == '\t'))
		p = p + 1;

	if (*p == '-' || *p == '+')
	{
		neg = (*p == '-');
		p = p + 1;

		lnum = 0;
		while (*p >= '0' && *p <= '9')
		{
			lnum = (lnum * 10) + (*p - '0');
			p = p + 1;
		}

		if (neg)
			lnum = -lnum;

		lp = (LPTR *)gotoline(cntllines(Filemem, cmdl_getline_pos) + lnum);
		*cmdl_getline_pos = *lp;
	}

	*cp = p;
	return cmdl_getline_pos;
}

/*
 * get_range - parse a range specifier
 */
void get_range(char **cp)
{
	LPTR *l;
	char *p;

	l = (LPTR *)get_line(cp);
	if (l == NULL)
		return;

	*cmdl_l_pos = *l;

	p = *cp;
	while (*p != NUL && (*p == ' ' || *p == '\t'))
		p = p + 1;

	*cp = p;

	if (*p != ',')		/* is there another line spec ? */
	{
		*cmdl_u_pos = *cmdl_l_pos;
		return;
	}

	p = p + 1;
	*cp = p;

	l = (LPTR *)get_line(cp);
	if (l == NULL)
	{
		*cmdl_u_pos = *cmdl_l_pos;
		return;
	}

	*cmdl_u_pos = *l;
}

void doshell(void)
{
	char *sh;

	sh = (char *)getenv("SHELL");
	if (sh == NULL)
	{
		emsg("Shell variable not set");
		return;
	}
	gotocmd(TRUE, FALSE, 0);

	if (system(sh) < 0)
	{
		emsg("Exec failed");
		return;
	}

	wait_return();
}

bool_t doecmd(char *arg, bool_t force)
{
	int line;
	char *s;

	line = 1;		/* line # to go to in new file */

	if (!force && Changed)
	{
		emsg(cmdl_nowrtmsg);
		return FALSE;
	}
	if (arg != NULL)
	{
		/*
		 * First detect a ":e" on the current file. This is mainly
		 * for ":ta" commands where the destination is within the
		 * current file.
		 */
		if (Filename != NULL && strcmp(arg, Filename) == 0)
		{
			if (!Changed || (Changed && !force))
				return TRUE;
		}
		if (strcmp(arg, "#") == 0)	/* alternate */
		{
			s = Filename;

			if (cmdl_altfile == NULL)
			{
				emsg("No alternate file");
				return FALSE;
			}
			Filename = cmdl_altfile;
			cmdl_altfile = s;
			line = cmdl_altline;
			cmdl_altline = cntllines(Filemem, Curschar);
		}
		else
		{
			cmdl_altfile = Filename;
			cmdl_altline = cntllines(Filemem, Curschar);
			Filename = (char *)strsave(arg);
		}
	}
	if (Filename == NULL)
	{
		emsg("No filename");
		return FALSE;
	}

	/* clear mem and read file */
	freeall();
	filealloc();
	UNCHANGED;

	readfile(Filename, Filemem, 0);
	*Topchar = *Curschar;
	if (line != 1)
	{
		stuffnum(line);
		stuffin("G");
	}
	setpcmark();
	updatescreen();
	return TRUE;
}

/*
 * dotag(tag, force) - goto tag
 */
void dotag(char *tag, bool_t force)
{
	FILE *tp;
	char *fname;
	char *str;

	tp = (FILE *)fopen("tags", "r");
	if (tp == NULL)
	{
		emsg("Can't open tags file");
		return;
	}

	while (fgets(cmdl_lbuf, LSIZE, tp) != NULL)
	{
		fname = (char *)strchr(cmdl_lbuf, TAB);
		if (fname == NULL)
		{
			emsg("Format error in tags file");
			return;
		}
		*fname = '\0';
		fname = fname + 1;
		str = (char *)strchr(fname, TAB);
		if (str == NULL)
		{
			emsg("Format error in tags file");
			return;
		}
		*str = '\0';
		str = str + 1;

		if (strcmp(cmdl_lbuf, tag) == 0)
		{
			if (doecmd(fname, force))
			{
				stuffin(str);		/* str has \n at end */
				stuffin("\007");	/* CTRL('G') */
				fclose(tp);
				return;
			}
		}
	}
	emsg("tag not found");
	fclose(tp);
}

/*
 * readcmdline() - accept a command line starting with ':', '/', or '?'
 */
void readcmdline(int firstc, char *cmdline)
{
	int c;
	char *p;
	char *q;
	char *cmd;
	char *arg;
	char *messbuf;
	extern char *Version;

	/*
	 * Clear the range variables.
	 */
	cmdl_l_pos->linep = (LINE *)0;
	cmdl_u_pos->linep = (LINE *)0;

	cmdl_interactive = (cmdline == NULL);

	if (cmdl_interactive)
		gotocmd(1, 1, firstc);
	p = cmdl_buff;
	if (firstc != ':')
	{
		*p = firstc;
		p = p + 1;
	}

	if (cmdl_interactive)
	{
		/* collect the command string, handling '\b' and @ */
		while (1)
		{
			c = vgetc();
			if (c == '\n' || c == '\r' || c == EOF)
				break;
			if (c == '\b')
			{
				if ((int)p > (int)cmdl_buff)
				{
					p = p - 1;
					/* this is gross, but it relies
					 * only on 'gotocmd'
					 */
					if (firstc == ':')
						gotocmd(1, 0, ':');
					else
						gotocmd(1, 0, 0);
					q = cmdl_buff;
					while ((int)q < (int)p)
					{
						outchar(*q);
						q = q + 1;
					}
				}
				else
				{
					msg("");
					return;		/* back to cmd mode */
				}
			}
			else if (c == '@')
			{
				p = cmdl_buff;
				gotocmd(1, 1, firstc);
			}
			else
			{
				outchar(c);
				*p = c;
				p = p + 1;
			}
		}
		*p = '\0';
	}
	else
	{
		if (strlen(cmdline) > CMDSZ - 2)	/* should really do something */
			return;			/* better here... */
		strcpy(p, cmdline);
	}

	/* skip any initial white space */
	cmd = cmdl_buff;
	while (*cmd != NUL && (*cmd == ' ' || *cmd == '\t'))
		cmd = cmd + 1;

	/* search commands */
	c = *cmd;
	if (c == '/' || c == '?')
	{
		cmd = cmd + 1;
		if (*cmd == c)
		{
			/* the command was '//' or '??' */
			repsearch();
			return;
		}
		/* If there is a matching '/' or '?' at the end, toss it */
		p = (char *)strchr(cmd, NUL);
		if (*(p - 1) == c && *(p - 2) != '\\')
			*(p - 1) = NUL;
		if (c == '/')
			dosearch(FORWARD, cmd);
		else
			dosearch(BACKWARD, cmd);
		return;
	}

	/*
	 * Parse a range, if present (and update the cmd pointer).
	 */
	get_range(&cmd);

	/* isolate the command and find any argument */
	p = cmd;
	while (*p != NUL && *p != ' ' && *p != '\t')
		p = p + 1;

	if (*p == NUL)
		arg = (char *)0;
	else
	{
		*p = NUL;
		p = p + 1;
		while (*p != NUL && (*p == ' ' || *p == '\t'))
			p = p + 1;
		arg = p;
		if (*arg == '\0')
			arg = (char *)0;
	}
	if (strcmp(cmd, "q!") == 0)
		getout();
	if (strcmp(cmd, "q") == 0)
	{
		if (Changed)
			emsg(cmdl_nowrtmsg);
		else
			getout();
		return;
	}
	if (strcmp(cmd, "w") == 0)
	{
		if (arg == NULL)
		{
			if (Filename != NULL)
			{
				writeit(Filename, cmdl_l_pos, cmdl_u_pos);
				UNCHANGED;
			}
			else
				emsg("No output file");
		}
		else
			writeit(arg, cmdl_l_pos, cmdl_u_pos);
		return;
	}
	if (strcmp(cmd, "wq") == 0)
	{
		if (Filename != NULL)
		{
			if (writeit(Filename, (LPTR *)0, (LPTR *)0))
				getout();
		}
		else
			emsg("No output file");
		return;
	}
	if (strcmp(cmd, "x") == 0)
	{
		if (Changed)
		{
			if (Filename != NULL)
			{
				if (!writeit(Filename, (LPTR *)0, (LPTR *)0))
					return;
			}
			else
			{
				emsg("No output file");
				return;
			}
		}
		getout();
	}
	if (strcmp(cmd, "f") == 0 && arg == NULL)
	{
		fileinfo();
		return;
	}
	if (*cmd == 'n')
	{
		if ((curfile + 1) < numfiles)
		{
			/*
			 * stuff ":e[!] FILE\n"
			 */
			stuffin(":e");
			if (cmd[1] == '!')
				stuffin("!");
			stuffin(" ");
			curfile = curfile + 1;
			stuffin(files[curfile]);
			stuffin("\n");
		}
		else
			emsg("No more files!");
		return;
	}
	if (*cmd == 'p')
	{
		if (curfile > 0)
		{
			/*
			 * stuff ":e[!] FILE\n"
			 */
			stuffin(":e");
			if (cmd[1] == '!')
				stuffin("!");
			stuffin(" ");
			curfile = curfile - 1;
			stuffin(files[curfile]);
			stuffin("\n");
		}
		else
			emsg("No more files!");
		return;
	}
	if (strncmp(cmd, "rew", 3) == 0)
	{
		if (numfiles <= 1)		/* nothing to rewind */
			return;
		curfile = 0;
		/*
		 * stuff ":e[!] FILE\n"
		 */
		stuffin(":e");
		if (cmd[3] == '!')
			stuffin("!");
		stuffin(" ");
		stuffin(files[0]);
		stuffin("\n");
		return;
	}
	if (strcmp(cmd, "e") == 0 || strcmp(cmd, "e!") == 0)
	{
		doecmd(arg, cmd[1] == '!');
		return;
	}
	if (strcmp(cmd, "f") == 0)
	{
		Filename = (char *)strsave(arg);
		filemess("");
		return;
	}
	if (strcmp(cmd, "r") == 0 || strcmp(cmd, ".r") == 0)
	{
		if (arg == NULL)
		{
			badcmd();
			return;
		}
		if (readfile(arg, Curschar, 1))
		{
			emsg("Can't open file");
			return;
		}
		updatescreen();
		CHANGED;
		return;
	}
	if (strcmp(cmd, ".=") == 0)
	{
		messbuf = (char *)alloc(80);
		sprintf(messbuf, "line %d", cntllines(Filemem, Curschar));
		msg(messbuf);
		free(messbuf);
		return;
	}
	if (strcmp(cmd, "$=") == 0)
	{
		messbuf = (char *)alloc(16);
		sprintf(messbuf, "%d", cntllines(Filemem, Fileend) - 1);
		msg(messbuf);
		free(messbuf);
		return;
	}
	if (strncmp(cmd, "ta", 2) == 0)
	{
		dotag(arg, cmd[2] == '!');
		return;
	}
	if (strcmp(cmd, "set") == 0)
	{
		doset(arg, cmdl_interactive);
		return;
	}
	if (strcmp(cmd, "help") == 0)
	{
		if (help())
		{
			screenclear();
			updatescreen();
		}
		return;
	}
	if (strcmp(cmd, "version") == 0)
	{
		msg(Version);
		return;
	}
	if (strcmp(cmd, "sh") == 0)
	{
		doshell();
		return;
	}
	/*
	 * If we got a line, but no command, then go to the line.
	 */
	if (*cmd == NUL && cmdl_l_pos->linep != NULL)
	{
		*Curschar = *cmdl_l_pos;
		cursupdate();
		return;
	}

	badcmd();
}
