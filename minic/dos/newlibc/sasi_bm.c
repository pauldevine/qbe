/*
 * sasi_bm.c -- bare-metal Victor 9000 SASI + FAT test (§6i, Phase-6
 * step 4f).
 *
 * The first bare-metal disk I/O through this toolchain: the minic-built
 * SASI/Xebec driver (bm_sasi.c) reads real sectors from the MAME
 * -scsi:0 hard disk, the unmodified newlibc block registry + FAT + VFS
 * stack mounts the Victor virtual volume, and CONFIG.SYS comes back
 * through open()/read() AND fopen()/fgets() -- the same file the
 * upstream sasi_fat_smoke_test validates.  WRITE(6) round-trips a
 * pattern on a high scratch LBA (the harness runs against a scratch
 * copy of the image, so writes are safe).
 *
 * Expected disk: the known MAME Victor/Tandon image (victor_30mb.img,
 * label "tandon_703_mame", one 59058-sector region, CONFIG.SYS 220
 * bytes at the root of volume 0).
 *
 * Every phase prints before it runs (5 MHz 8088 rule).
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
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

#define CONFIG_PATH       "/fat/CONFIG.SYS"
#define CONFIG_SIZE       220L
#define SCRATCH_LBA       59057UL

static const char expected_prefix[] =
    "buffers = 15\r\n"
    "break = on\r\n"
    "files = 20\r\n";

static sasi_device_t sasi0;
static uint8_t sector[SASI_SECTOR_SIZE];
static uint8_t sector2[SASI_SECTOR_SIZE];
static int fails;

static uint16_t sector_checksum(const uint8_t *buffer) {
    uint16_t sum;
    unsigned int i;

    sum = 0;
    for (i = 0; i < SASI_SECTOR_SIZE; i++) {
        sum = (uint16_t)((sum << 1) ^ buffer[i] ^ (sum >> 15));
    }

    return sum;
}

int main(void) {
    block_device_info_t info;
    struct stat st;
    char line[64];
    FILE *fp;
    long n;
    int dev, ret, fd, i;

    fails = 0;
    bm_puts("__V9BEGIN__\n");
    bm_puts("Victor 9000 bare-metal SASI+FAT test (qbe/minic)\n");

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

    printf("phase 6: SASI register + controller init\n");
    block_init_registry();
    memset(&sasi0, 0, sizeof(sasi0));
    sasi0.target_id = 0;
    sasi0.total_sectors = SASI_DEFAULT_TOTAL_SECTORS;
    sasi0.allow_writes = 1;
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
        printf("phase 6: controller up (reset + TUR + RAM/CTRL tests)\n");
    }

    printf("phase 7: geometry: ");
    ret = block_get_info(dev, &info);
    if (ret < 0) {
        printf("FAIL error=%d\n", ret);
        fails++;
    } else {
        printf("%lu sectors x %u bytes, flags=0x%X\n",
               (unsigned long)info.total_sectors,
               (unsigned int)info.sector_size,
               (unsigned int)info.flags);
    }

    printf("phase 8: read LBA 0 (Victor disk label)\n");
    memset(sector, 0, sizeof(sector));
    ret = block_read_sector(dev, 0, sector);
    if (ret < 0) {
        printf("FAIL: read error=%d (%s)\n", ret, block_error_string(ret));
        fails++;
    }
    printf("phase 8: LBA0[0..15] =");
    for (i = 0; i < 16; i++) {
        printf(" %02X", (unsigned int)sector[i]);
    }
    printf("\n");

    printf("phase 9: repeat uncached read + checksum: ");
    {
        uint16_t sum1, sum2;

        sum1 = sector_checksum(sector);
        block_cache_invalidate(dev);
        memset(sector, 0, sizeof(sector));
        ret = block_read_sector(dev, 0, sector);
        sum2 = sector_checksum(sector);
        printf("0x%04X vs 0x%04X -- ", (unsigned int)sum1,
               (unsigned int)sum2);
        if (ret == 0 && sum1 == sum2) {
            printf("match\n");
        } else {
            printf("MISMATCH\n");
            fails++;
        }
    }

    printf("phase 10: mount Victor volume 0 at /fat: ");
    ret = vfs_mount_victor_fat("/fat", dev, 0);
    if (ret < 0) {
        printf("FAIL error=%d (%s)\n", ret, block_error_string(ret));
        fails++;
    } else {
        printf("ok\n");
    }

    printf("phase 11: stat " CONFIG_PATH ": ");
    if (stat(CONFIG_PATH, &st) != 0) {
        printf("FAIL\n");
        fails++;
    } else {
        printf("size=%ld -- %s\n", (long)st.st_size,
               (long)st.st_size == CONFIG_SIZE ? "yes" : "WRONG");
        if ((long)st.st_size != CONFIG_SIZE) {
            fails++;
        }
    }

    printf("phase 12: open+read prefix: ");
    fd = open(CONFIG_PATH, O_RDONLY);
    if (fd < 0) {
        printf("FAIL: open\n");
        fails++;
    } else {
        memset(line, 0, sizeof(line));
        n = read(fd, line, sizeof(expected_prefix) - 1);
        close(fd);
        if (n == (long)(sizeof(expected_prefix) - 1) &&
            memcmp(line, expected_prefix, sizeof(expected_prefix) - 1) == 0) {
            printf("matches known image contents\n");
        } else {
            printf("MISMATCH (n=%ld)\n", n);
            fails++;
        }
    }

    printf("phase 13: fopen+fgets first line: ");
    fp = fopen(CONFIG_PATH, "r");
    if (fp == 0) {
        printf("FAIL: fopen\n");
        fails++;
    } else {
        memset(line, 0, sizeof(line));
        if (fgets(line, sizeof(line), fp) != line) {
            printf("FAIL: fgets\n");
            fails++;
        } else {
            /* strip the CRLF for a stable one-line golden */
            for (i = 0; line[i] != 0; i++) {
                if (line[i] == '\r' || line[i] == '\n') {
                    line[i] = 0;
                    break;
                }
            }
            printf("\"%s\"\n", line);
        }
        fclose(fp);
    }

    printf("phase 14: WRITE(6) round-trip @ LBA %lu: ", SCRATCH_LBA);
    for (i = 0; i < SASI_SECTOR_SIZE; i++) {
        sector[i] = (uint8_t)(i ^ 0xA5);
    }
    ret = block_write_sector(dev, SCRATCH_LBA, sector);
    if (ret < 0) {
        printf("FAIL: write error=%d (%s)\n", ret, block_error_string(ret));
        fails++;
    } else {
        block_cache_invalidate(dev);
        memset(sector2, 0, sizeof(sector2));
        ret = block_read_sector(dev, SCRATCH_LBA, sector2);
        if (ret == 0 && memcmp(sector, sector2, SASI_SECTOR_SIZE) == 0) {
            printf("pattern verified\n");
        } else {
            printf("READBACK MISMATCH (ret=%d)\n", ret);
            fails++;
        }
    }

    printf("phase 15: re-read CONFIG.SYS after write: ");
    fd = open(CONFIG_PATH, O_RDONLY);
    if (fd >= 0) {
        memset(line, 0, sizeof(line));
        n = read(fd, line, 12);
        close(fd);
        if (n == 12 && memcmp(line, "buffers = 15", 12) == 0) {
            printf("still intact\n");
        } else {
            printf("CORRUPTED\n");
            fails++;
        }
    } else {
        printf("FAIL: open\n");
        fails++;
    }

    if (fails == 0)
        printf("PASS: bare-metal SASI+FAT checks completed.\n");
    else
        printf("FAIL: %d bare-metal SASI+FAT checks failed.\n", fails);
    bm_puts("__V9END__\n");
    return 0;
}
