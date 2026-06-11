/*
 * grouped_bss_def.c -- second translation unit for grouped_bss_probe.c.
 *
 * The globals below live in _BSS.  A target-frame near-data fixup must be
 * patched relative to DGROUP, not relative to the physical _BSS segment,
 * because generated code reaches them through DS.
 */

struct pair {
	void *p;
	int i;
};

char bss_pad[4096];
struct pair bss_pair;
int bss_counter;

void
write_bss_pair(void *p, int i)
{
	bss_pair.p = p;
	bss_pair.i = i;
}

int
check_bss_pair(void *p, int i)
{
	return bss_pair.p == p && bss_pair.i == i;
}

void
bump_bss_counter(void)
{
	bss_counter++;
}
