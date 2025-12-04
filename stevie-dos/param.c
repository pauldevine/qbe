/*
 * STevie - ST editor for VI enthusiasts.    ...Tim Thompson...twitch!tjt...
 *
 * Extensive modifications by:  Tony Andrews       onecom!wldrdg!tony
 *
 * DOS port for MiniC/QBE 8086 compiler
 */

/*
 * Code to handle user-settable parameters. This is all pretty much table-
 * driven. To add a new parameter, put it in the params array, and add a
 * macro for it in param.h. If it's a numeric parameter, add any necessary
 * bounds checks to doset(). String parameters aren't currently supported.
 */

#include "stevie.h"

/*
 * MiniC doesn't support global struct array initializers, so we
 * initialize the params array dynamically in init_params().
 */
#define NPARAMS 13

void init_param(int i, char *full, char *sht, int val, int flg)
{
	params[i].fullname = full;
	params[i].shortname = sht;
	params[i].value = val;
	params[i].flags = flg;
}

void init_params(void)
{
	init_param(0, "tabstop", "ts", 8, P_NUM);
	init_param(1, "scroll", "scroll", 12, P_NUM);
	init_param(2, "report", "report", 5, P_NUM);
	init_param(3, "lines", "lines", 25, P_NUM);
	init_param(4, "vbell", "vb", TRUE, P_BOOL);
	init_param(5, "showmatch", "sm", FALSE, P_BOOL);
	init_param(6, "wrapscan", "ws", TRUE, P_BOOL);
	init_param(7, "errorbells", "eb", FALSE, P_BOOL);
	init_param(8, "showmode", "mo", FALSE, P_BOOL);
	init_param(9, "backup", "bk", FALSE, P_BOOL);
	init_param(10, "return", "cr", TRUE, P_BOOL);
	init_param(11, "list", "list", FALSE, P_BOOL);
	init_param(12, "", "", 0, 0);	/* end marker */
}

void showparms(bool_t all)
{
	struct param *p;
	char *buf;
	int i;

	buf = (char *)alloc(64);
	gotocmd(TRUE, TRUE, 0);
	outstr("Parameters:\r\n");

	i = 0;
	while (params[i].fullname[0] != NUL)
	{
		p = &params[i];
		if (!all && ((p->flags & P_CHANGED) == 0))
		{
			i = i + 1;
			continue;
		}
		if (p->flags & P_BOOL)
		{
			if (p->value)
				sprintf(buf, "\t%s\r\n", p->fullname);
			else
				sprintf(buf, "\tno%s\r\n", p->fullname);
		}
		else
			sprintf(buf, "\t%s=%d\r\n", p->fullname, p->value);

		outstr(buf);
		i = i + 1;
	}
	free(buf);
	wait_return();
}

void doset(char *arg, bool_t inter)
{
	int i;
	char *s;
	bool_t did_lines;
	bool_t state;

	did_lines = FALSE;
	state = TRUE;		/* new state of boolean parms. */

	if (arg == NULL) {
		showparms(FALSE);
		return;
	}
	if (strncmp(arg, "all", 3) == 0) {
		showparms(TRUE);
		return;
	}
	if (strncmp(arg, "no", 2) == 0) {
		state = FALSE;
		arg = arg + 2;
	}

	i = 0;
	while (params[i].fullname[0] != NUL)
	{
		s = params[i].fullname;
		if (strncmp(arg, s, strlen(s)) == 0)	/* matched full name */
			break;
		s = params[i].shortname;
		if (strncmp(arg, s, strlen(s)) == 0)	/* matched short name */
			break;
		i = i + 1;
	}

	if (params[i].fullname[0] != NUL) {	/* found a match */
		if (params[i].flags & P_NUM) {
			did_lines = (i == P_LI);
			if (inter && (arg[strlen(s)] != '=' || state == FALSE))
				emsg("Invalid set of numeric parameter");
			else {
				params[i].value = atoi(arg + strlen(s) + 1);
				params[i].flags = params[i].flags | P_CHANGED;
			}
		} else /* boolean */ {
			if (inter && (arg[strlen(s)] == '='))
				emsg("Invalid set of boolean parameter");
			else {
				params[i].value = state;
				params[i].flags = params[i].flags | P_CHANGED;
			}
		}
	} else {
		if (inter)
			emsg("Unrecognized 'set' option");
	}

	/*
	 * Check the bounds for numeric parameters here
	 */
	if (P(P_TS) <= 0 || P(P_TS) > 32) {
		if (inter)
			emsg("Invalid tab size specified");
		P(P_TS) = 8;
		return;
	}
	updatetabstoptable();

	/*
	 * Update the screen in case we changed something like "tabstop"
	 * or "list" that will change its appearance.
	 */
	if (inter)
		updatescreen();

	if (did_lines)
	{
		Rows = P(P_LI);
		screenalloc();		/* allocate new screen buffers */
		screenclear();
		updatescreen();
	}
	if (P(P_SS) <= 0 || P(P_SS) > Rows)
	{
		if (inter)
			emsg("Invalid scroll size specified");
		P(P_SS) = 12;
		return;
	}

	/*
	 * Check for another argument, and call doset() recursively, if
	 * found.
	 */
	while (*arg != ' ' && *arg != 9)	/* skip to next white space (tab=9) */
	{
		if (*arg == NUL)
			return;			/* end of parameter list */
		arg = arg + 1;
	}
	while (*arg == ' ' || *arg == 9)	/* skip to next non-white */
		arg = arg + 1;

	if (*arg)
		doset(arg, inter);	/* recurse on next parameter, if present */
}
