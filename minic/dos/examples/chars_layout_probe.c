/* chars_layout_probe.c — verify struct array layout under compact.
 * Specifically: { char ch_size; char *ch_str; }  sizeof should be 5,
 * and chars[c].ch_size should read the right byte.
 */

#include <stdio.h>

struct charinfo {
	char ch_size;
	char *ch_str;
};

struct charinfo chars[] = {
	/* 0 */ { 1, 0 },
	/* 1 */ { 7, "[ERROR]" },
	/* 2 */ { 1, 0 },
	/* 3 */ { 3, "abc" },
	/* 4 */ { 1, 0 },
	/* 5 */ { 1, 0 },
	/* 6 */ { 1, 0 },
	/* 7 */ { 1, 0 },
};

int main()
{
	int i;
	printf("sizeof(struct charinfo) = %d\r\n",
		(int)sizeof(struct charinfo));
	for (i = 0; i < 8; i++) {
		printf("chars[%d].ch_size=%d\r\n", i, (int)chars[i].ch_size);
	}
	return 0;
}
