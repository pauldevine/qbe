/* local_shadow_probe.c — §6a: block-scope locals shadow file-scope bindings.
 *
 * Before the fix, minic supported alpha-renaming only for inner-block
 * locals colliding with other LOCALS (§1k); a local named like a global
 * variable, a declared function, or an enum constant died with "double
 * definition".  newlibc vfs_open() declares `const fat_mount_t *fat_mount;`
 * next to the file-scope function fat_mount().  Fixed by extending
 * block_scope_decl's rename trigger to any global/extern/function/enum
 * binding and wiring block_scope_decl into the dcls-chain local rules
 * (function-body depth), not just the stmt-context ones.
 */
#include <stdio.h>

int counter = 100;

int fat_mount(int dev)
{
	return dev * 2;
}

enum { RED = 77 };

static int use_shadow(void)
{
	int counter = 5;   /* shadows the global */
	int fat_mount;     /* shadows the function */
	int RED = 1;       /* shadows the enum constant */
	fat_mount = 3;
	counter += fat_mount + RED;
	return counter;
}

int main()
{
	printf("%d\n", use_shadow());
	printf("%d\n", counter);
	printf("%d\n", fat_mount(21));
	printf("%d\n", RED);
	return 0;
}
