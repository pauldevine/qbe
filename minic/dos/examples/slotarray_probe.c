/*
 * slotarray_probe.c -- two minic static-data/indexing bugs found on the real
 * Victor via MicroPython's type-slot dispatch (mp_obj_print_helper of an int):
 *
 *  BUG 1 (flexible array member init dropped all but the first element):
 *    a `T x[];` flexible array member with a multi-element static initializer
 *    emitted ONLY x[0] (agg_emit_value routed the braced list through
 *    agg_emit_scalar).  mp_obj_type_t.slots -- so slots[1] (the `print` fn)
 *    read past-the-array garbage and the (far) call jumped to a wild address.
 *
 *  BUG 2 (array-of-pointers subscript stride):
 *    array_vartyp registered an array-of-pointers with the element (pointer)
 *    type itself instead of IDIR(elem), so `arr[i]` scaled the subscript by
 *    sizeof(*elem) rather than sizeof(elem*).  mp_obj_get_type's static
 *    `types[]` thus returned a wrong-segment type ptr.
 *
 * Both are front-end (data-emission / typing) bugs, model-independent in
 * origin.  This probe is COMPACT + far-static-data (MicroPython's actual
 * config): BUG 1 is bug-loud because a dropped slot makes the indirect FAR
 * call jump to garbage; BUG 2 because the element is a 4-byte far `int *` but
 * the pointee is a 2-byte `int`, so the wrong stride (2 vs the correct 4)
 * reads the wrong element.  Far pointers in static data only carry their
 * segment under --far-static-data, so this probe MUST be built with
 * QBE_FAR_STATIC_DATA=1 (the gate sets it for this entry).
 *
 * Build:  QBE_FAR_STATIC_DATA=1 tools/build-example.sh --model=compact \
 *             minic/dos/examples/slotarray_probe.c
 * Verify: tools/run-dos-exe.sh build/examples/slotarray_probe/slotarray_probe.exe \
 *             | diff - minic/dos/tests/slotarray_probe.golden.txt
 */

typedef int (*fn_t)(void);

static int fa(void) { return 11; }
static int fb(void) { return 22; }
static int fc(void) { return 33; }

/* BUG 1: a struct ending in a flexible array member, like mp_obj_type_t with
 * its `const void *slots[]`.  The fixed fields carry the per-slot indices. */
struct disp {
    int tag;
    unsigned char idx_a;
    unsigned char idx_b;
    fn_t slots[];
};
static const struct disp D = { 7, 1, 3, { fa, fb, fc } };

/* BUG 2: an array of pointers, like mp_obj_get_type's static `types[]`.  Under
 * compact the element is a 4-byte far `int *` but the pointee is a 2-byte
 * `int`, so the wrong stride (sizeof(int)=2) differs from the correct one
 * (sizeof(int*)=4 far) and reads the wrong element. */
static int va = 100;
static int vb = 200;
static int vc = 300;
static int vd = 400;
static int *const ptrs[] = { &va, &vb, &vc, &vd };

int gi;  /* runtime index in a global -- defeats constant folding */

int main(void) {
    int i;
    /* Dispatch through the flexible-member slots by index (idx-1, like the
     * MP_OBJ_TYPE_GET_SLOT machinery): slots[0]=fa, slots[1]=fb, slots[2]=fc. */
    fn_t f1 = D.slots[D.idx_a - 1];   /* slots[0] -> fa -> 11 */
    fn_t f3 = D.slots[D.idx_b - 1];   /* slots[2] -> fc -> 33 */
    printf("f1=%d f3=%d\n", f1(), f3());
    printf("s0=%d s1=%d s2=%d\n",
           D.slots[0](), D.slots[1](), D.slots[2]());

    /* Runtime-indexed pointer array: each must read the matching scalar. */
    for (i = 0; i < 4; i++) {
        gi = i;
        printf("p%d=%d\n", i, *ptrs[gi]);
    }
    return 0;
}
