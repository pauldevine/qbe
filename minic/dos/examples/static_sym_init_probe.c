/* static_sym_init_probe.c — §6a: file-scope SCALAR initializer holding a
 * symbol address.
 *
 * The aggregate path (§1b/§1g) could already emit `$sym+off` data items,
 * but the scalar `'=' expr ';'` rule folded with const_eval only, so
 *   char **environ = __env;     (newlibc syscalls.c)
 *   int *p = &x;
 *   int *mid = &arr[2];
 * died ("non-constant in case label" / "unsupported operation in constant
 * expression").  Now routed through cival_eval → emit_global_sym_init.
 */
#include <stdio.h>

static char *words[2];
char **env_like = words;

int cell = 5;
int *pcell = &cell;

int arr[4];
int *mid = &arr[2];

int main()
{
	words[0] = "w0";
	*pcell += 2;
	arr[2] = 9;
	printf("%d\n", *pcell);
	printf("%d\n", *mid);
	printf("%s\n", env_like[0]);
	return 0;
}
