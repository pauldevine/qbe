#include <stdio.h>
#include <string.h>

typedef void *obj_t;

static obj_t g_a = (obj_t)0x11112222UL;
static obj_t g_b = (obj_t)0x33334444UL;
static obj_t g_c = (obj_t)0x55556666UL;
static obj_t g_d = (obj_t)0x77778888UL;
static obj_t victim[2];

static unsigned long bits(obj_t p)
{
	return (unsigned long)p;
}

static void copy_local_array(void)
{
	obj_t src[2];
	obj_t dst[5];

	src[0] = g_a;
	src[1] = g_b;
	dst[0] = 0;
	dst[1] = 0;
	memcpy(dst, src, 2 * sizeof(obj_t));

	printf("dst0=%lu\n", bits(dst[0]));
	printf("dst1=%lu\n", bits(dst[1]));
}

static void copy_shadowed_array(int n)
{
	obj_t src[2];

	src[0] = g_a;
	src[1] = g_b;
	if (n <= 5) {
		obj_t args2[5];
		args2[0] = 0;
		args2[1] = 0;
		memcpy(args2, src, 2 * sizeof(obj_t));
		printf("shadow0=%lu\n", bits(args2[0]));
		printf("shadow1=%lu\n", bits(args2[1]));
		printf("victim0=%lu\n", bits(victim[0]));
		printf("victim1=%lu\n", bits(victim[1]));
	} else {
		obj_t *args2 = victim;
		args2[0] = g_c;
		args2[1] = g_d;
		printf("ptr0=%lu\n", bits(args2[0]));
	}
}

int main(void)
{
	copy_local_array();
	copy_shadowed_array(9);
	copy_shadowed_array(2);
	return 0;
}
