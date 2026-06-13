/*
 * bm_sasi.h -- bare-metal Victor 9000 SASI hard disk block device
 * (§6i, Phase-6 step 4f).
 *
 * The minic-dialect port of newlibc's drivers/sasi.h.  The public
 * surface (names, constants, structs) is kept byte-for-byte compatible
 * with upstream so the upstream SASI tests port unchanged; only the
 * implementation TU differs (bm_sasi.c).
 */

#ifndef BM_SASI_H
#define BM_SASI_H

#include <stdint.h>

#include "block.h"

#define SASI_BASE_SEGMENT  0xEF30

#define SASI_HDCTL_OFFSET   0x00
#define SASI_HDCSD_OFFSET   0x10
#define SASI_HDBUS_OFFSET   0x20
#define SASI_HDDMAL_OFFSET  0x80
#define SASI_HDDMAM_OFFSET  0xA0
#define SASI_HDDMAH_OFFSET  0xC0

#define SASI_CTRL_DMAEN    0x01
#define SASI_CTRL_CPULOCK  0x02
#define SASI_CTRL_DMASTB   0x04
#define SASI_CTRL_DMADIR   0x08
#define SASI_CTRL_SELECT   0x10
#define SASI_CTRL_RESET    0x20

#define SASI_STAT_INPUT    0x01
#define SASI_STAT_CONTROL  0x02
#define SASI_STAT_BUSY     0x04
#define SASI_STAT_REQ      0x08
#define SASI_STAT_MSG      0x10

#define SASI_CMD_TEST_UNIT_READY  0x00
#define SASI_CMD_REQUEST_SENSE    0x03
#define SASI_CMD_READ             0x08
#define SASI_CMD_WRITE            0x0A
#define SASI_CMD_XEBEC_RAM_TEST   0xE0
#define SASI_CMD_XEBEC_CTRL_TEST  0xE4

#define SASI_SECTOR_SIZE  512

#define SASI_DIAG_STEP_NONE             0
#define SASI_DIAG_STEP_RESET            1
#define SASI_DIAG_STEP_BUSY_CLEAR       2
#define SASI_DIAG_STEP_SELECT_BUSY      3
#define SASI_DIAG_STEP_COMMAND_REQ      4
#define SASI_DIAG_STEP_CDB              5
#define SASI_DIAG_STEP_DATA_IN_PHASE    6
#define SASI_DIAG_STEP_DATA_IN_REQ      7
#define SASI_DIAG_STEP_STATUS_PHASE     8
#define SASI_DIAG_STEP_MESSAGE_PHASE    9
#define SASI_DIAG_STEP_FINISH           10
#define SASI_DIAG_STEP_REQUEST_SENSE    11
#define SASI_DIAG_STEP_READ_DATA        12
#define SASI_DIAG_STEP_DATA_OUT_PHASE   13
#define SASI_DIAG_STEP_DATA_OUT_REQ     14
#define SASI_DIAG_STEP_WRITE_DATA       15

typedef struct {
    int last_error;
    block_lba_t last_lba;
    uint8_t target_id;
    uint8_t last_step;
    uint8_t last_command;
    uint8_t last_bus;
    uint8_t status_byte;
    uint8_t message_byte;
    uint8_t sense[4];
    uint8_t sense_len;
} sasi_diagnostics_t;

/*
 * MAME's known Victor/Tandon test image reports one 59058-sector region.
 * Callers can override this if they know the attached target's capacity.
 */
#define SASI_DEFAULT_TOTAL_SECTORS  59058UL

typedef struct {
    uint8_t target_id;          /* Normally 0 for target bit 0x01 */
    block_lba_t total_sectors;  /* 0 selects SASI_DEFAULT_TOTAL_SECTORS */
    uint8_t initialized;
    /*
     * Writes stay opt-in: leave 0 and the device registers read-only
     * (block writes fail -EROFS); set 1 before sasi_register() to
     * enable WRITE(6).  Only enable against a scratch disk image.
     */
    uint8_t allow_writes;
    sasi_diagnostics_t diagnostics;
} sasi_device_t;

int sasi_register(sasi_device_t *dev);
void sasi_clear_diagnostics(sasi_device_t *dev);
void sasi_get_diagnostics(const sasi_device_t *dev,
                          sasi_diagnostics_t *diagnostics);
uint8_t sasi_read_bus_status(void);
int sasi_refresh_sense(sasi_device_t *dev);

#endif /* BM_SASI_H */
