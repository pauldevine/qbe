/* bm_stdio.h -- bare-metal Victor 9000 newlibc stdio seam (§6h). */
#ifndef BM_STDIO_H
#define BM_STDIO_H

/* Including this header makes build-newlibc-baremetal.sh link the
 * newlibc stdio stack (printf/scanf wrappers -> libgloss syscalls ->
 * VFS /dev/console) over bm_shim.c, whose console device ops are
 * bm_tty -- so printf()/read(0)/fgets() work on the bare machine
 * through the SAME newlibc layers the DOS-hosted tests exercise.
 *
 * Call bm_stdio_init() (= vfs_init(): fds 0/1/2 -> /dev/console)
 * after bm_tty_init(); the usual full bring-up order is:
 *
 *     bm_interrupts_init();
 *     bm_timer_init();           (if the program uses the timer)
 *     bm_tty_init();
 *     bm_interrupts_enable();
 *     bm_stdio_init();
 */
void bm_stdio_init(void);

#endif
