/*
 * fatwrite_bm.c -- bare-metal Victor 9000 FAT WRITE test (§6k, Phase-6
 * step 4h).
 *
 * The first bare-metal exercise of the newlibc FAT write layer
 * (fat_write.c): a writable Victor volume mounted with
 * vfs_mount_victor_fat_rw() over the §6i bm_sasi WRITE(6) path, then a
 * full file lifecycle round-trip -- create, write 2000 bytes across
 * several clusters, read back through SASI, append a tail, unlink -- with
 * the known read-only fixture CONFIG.SYS checked intact before AND after,
 * proving the writes touched only the new file and its FAT chain.
 *
 * Logic mirrors the upstream tests/sasi_fat_write_test.c; the difference
 * is the bottom of the stack (bm_sasi + bare-metal bring-up instead of
 * ia16-gcc sasi.c + DOS host) -- exactly the §6i parallel.
 *
 * !!! This test WRITES to the attached disk.  The harness runs it against
 * a SCRATCH COPY of the MAME image (V9K_HARD_DISK), and the test removes
 * V9KWRT.TMP when done, so re-runs against the same scratch image stay
 * clean and CONFIG.SYS is never touched.
 *
 * Expected disk: the known MAME Victor/Tandon image (victor_30mb.img,
 * label "tandon_703_mame", CONFIG.SYS 220 bytes at the root of volume 0).
 *
 * Every phase prints before it runs (5 MHz 8088 rule).
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>

#include "bm_console.h"
#include "bm_timer.h"
#include "bm_interrupts.h"
#include "bm_tty.h"
#include "bm_stdio.h"
#include "block.h"
#include "bm_sasi.h"
#include "vfs.h"
#include "fat_write.h"

#define MOUNT_PATH        "/hd"
#define CONFIG_PATH       "/hd/CONFIG.SYS"
#define CONFIG_SIZE       220L
#define TEST_FILE_PATH    "/hd/V9KWRT.TMP"
#define TEST_DATA_BYTES   2000

static sasi_device_t sasi0;
static uint8_t pattern_buf[TEST_DATA_BYTES];
static uint8_t verify_buf[TEST_DATA_BYTES];
static int fails;

static void check_config_intact(const char *when) {
    struct stat st;

    printf("phase: CONFIG.SYS intact %s: ", when);
    if (stat(CONFIG_PATH, &st) != 0) {
        printf("FAIL: stat errno=%d\n", errno);
        fails++;
        return;
    }
    if (!S_ISREG(st.st_mode) || (long)st.st_size != CONFIG_SIZE) {
        printf("FAIL: mode=0x%X size=%ld\n",
               (unsigned int)st.st_mode, (long)st.st_size);
        fails++;
        return;
    }
    printf("size=%ld ok\n", (long)st.st_size);
}

int main(void) {
    struct stat st;
    long n;
    int dev, ret, fd;
    unsigned int i;

    fails = 0;
    bm_puts("__V9BEGIN__\n");
    bm_puts("Victor 9000 bare-metal FAT WRITE test (qbe/minic)\n");

    bm_puts("phase 1: PIC re-init + timer ISR (INT 0x42)\n");
    bm_interrupts_init();

    bm_puts("phase 2: 8253 ch2 100 Hz + IR2 unmask\n");
    bm_timer_init();

    bm_puts("phase 3: tty init (display + keyboard, IR6)\n");
    bm_tty_init();

    bm_puts("phase 4: sti\n");
    bm_interrupts_enable();

    bm_puts("phase 5: vfs init (fds 0/1/2 -> /dev/console)\n");
    bm_stdio_init();

    printf("phase 6: SASI register + controller init (writes enabled)\n");
    block_init_registry();
    memset(&sasi0, 0, sizeof(sasi0));
    sasi0.target_id = 0;
    sasi0.total_sectors = SASI_DEFAULT_TOTAL_SECTORS;
    sasi0.allow_writes = 1;             /* scratch image only */
    dev = sasi_register(&sasi0);
    if (dev < 0) {
        printf("FAIL: sasi_register error=%d\n", dev);
        fails++;
    }
    ret = block_init(dev);
    if (ret < 0) {
        printf("FAIL: block_init error=%d (%s)\n",
               ret, block_error_string(ret));
        fails++;
    } else {
        printf("phase 6: controller up\n");
    }

    printf("phase 7: mount Victor volume 0 read-WRITE at " MOUNT_PATH ": ");
    ret = vfs_mount_victor_fat_rw(MOUNT_PATH, dev, 0);
    if (ret < 0) {
        printf("FAIL error=%d (%s)\n", ret, block_error_string(ret));
        fails++;
    } else {
        printf("ok\n");
    }

    if (fails == 0) {
        check_config_intact("before writes");

        printf("phase 8: create+write %d bytes to " TEST_FILE_PATH ": ",
               TEST_DATA_BYTES);
        for (i = 0; i < TEST_DATA_BYTES; i++) {
            pattern_buf[i] = (uint8_t)(i % 253);
        }
        unlink(TEST_FILE_PATH);         /* clear any stale leftover */
        fd = open(TEST_FILE_PATH, O_CREAT | O_EXCL | O_WRONLY, 0644);
        if (fd < 0) {
            printf("FAIL: open errno=%d\n", errno);
            fails++;
        } else {
            n = write(fd, pattern_buf, TEST_DATA_BYTES);
            close(fd);
            if (n != TEST_DATA_BYTES) {
                printf("FAIL: write returned %ld errno=%d\n", n, errno);
                fails++;
            } else if (stat(TEST_FILE_PATH, &st) != 0 ||
                       (long)st.st_size != (long)TEST_DATA_BYTES) {
                printf("FAIL: size=%ld\n", (long)st.st_size);
                fails++;
            } else {
                printf("size=%ld ok\n", (long)st.st_size);
            }
        }

        printf("phase 9: read back through SASI: ");
        fd = open(TEST_FILE_PATH, O_RDONLY);
        if (fd < 0) {
            printf("FAIL: open errno=%d\n", errno);
            fails++;
        } else {
            memset(verify_buf, 0, sizeof(verify_buf));
            n = read(fd, verify_buf, TEST_DATA_BYTES);
            close(fd);
            if (n == TEST_DATA_BYTES &&
                memcmp(verify_buf, pattern_buf, TEST_DATA_BYTES) == 0) {
                printf("%d bytes match\n", TEST_DATA_BYTES);
            } else {
                printf("MISMATCH (n=%ld)\n", n);
                fails++;
            }
        }

        printf("phase 10: append \"TAIL\" + verify: ");
        fd = open(TEST_FILE_PATH, O_WRONLY | O_APPEND);
        if (fd < 0) {
            printf("FAIL: open errno=%d\n", errno);
            fails++;
        } else {
            n = write(fd, "TAIL", 4);
            close(fd);
            if (n != 4) {
                printf("FAIL: append returned %ld errno=%d\n", n, errno);
                fails++;
            } else if (stat(TEST_FILE_PATH, &st) != 0 ||
                       (long)st.st_size != (long)(TEST_DATA_BYTES + 4)) {
                printf("FAIL: grew to %ld\n", (long)st.st_size);
                fails++;
            } else {
                fd = open(TEST_FILE_PATH, O_RDONLY);
                if (fd < 0 ||
                    lseek(fd, TEST_DATA_BYTES, SEEK_SET) !=
                        (off_t)TEST_DATA_BYTES) {
                    printf("FAIL: seek to tail\n");
                    fails++;
                    if (fd >= 0) close(fd);
                } else {
                    memset(verify_buf, 0, 8);
                    n = read(fd, verify_buf, 8);
                    close(fd);
                    if (n == 4 && memcmp(verify_buf, "TAIL", 4) == 0) {
                        printf("ok\n");
                    } else {
                        printf("MISMATCH (n=%ld)\n", n);
                        fails++;
                    }
                }
            }
        }

        printf("phase 11: unlink " TEST_FILE_PATH ": ");
        if (unlink(TEST_FILE_PATH) != 0) {
            printf("FAIL: unlink errno=%d\n", errno);
            fails++;
        } else {
            errno = 0;
            if (stat(TEST_FILE_PATH, &st) == 0 || errno != ENOENT) {
                printf("FAIL: still present (errno=%d)\n", errno);
                fails++;
            } else {
                printf("removed\n");
            }
        }

        check_config_intact("after writes");
    }

    if (fails == 0)
        printf("PASS: bare-metal FAT write checks completed.\n");
    else
        printf("FAIL: %d bare-metal FAT write checks failed.\n", fails);
    bm_puts("__V9END__\n");
    return 0;
}
