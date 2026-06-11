/*
 * grouped_bss_probe.c -- regression for OMF target-frame fixups into DGROUP.
 *
 * This must be linked with grouped_bss_def.c:
 *   tools/build-example.sh --model=medium \
 *       minic/dos/examples/grouped_bss_probe.c \
 *       minic/dos/examples/grouped_bss_def.c
 *
 * Old omf_link behavior used the target segment as the frame for NASM's
 * target-frame 16-bit offset fixups.  For _BSS members of DGROUP that makes
 * near DS references land too early, aliasing initialized data.  The data
 * guard below makes that alias visible even if all BSS references are
 * consistently wrong.
 */

#include <stdio.h>

struct pair {
	void *p;
	int i;
};

extern struct pair bss_pair;
extern int bss_counter;

void write_bss_pair(void *p, int i);
int check_bss_pair(void *p, int i);
void bump_bss_counter(void);

char data_guard[8192] = { 7 };

static int
data_ok(void)
{
	int i;

	if (data_guard[0] != 7)
		return 0;
	for (i = 1; i < 8192; i++) {
		if (data_guard[i] != 0)
			return 0;
	}
	return 1;
}

int
main(void)
{
	int local;

	write_bss_pair(&local, 42);
	bump_bss_counter();
	printf("same=%d i=%d check=%d counter=%d data_ok=%d\r\n",
	    bss_pair.p == &local, bss_pair.i,
	    check_bss_pair(&local, 42), bss_counter, data_ok());
	return 0;
}
