/*
 * bm_sasi.c -- bare-metal Victor 9000 SASI hard disk block device
 * (§6i, Phase-6 step 4f).
 *
 * The minic-dialect port of newlibc's drivers/sasi.c: the SASI/Xebec
 * block backend using the manual polled byte-transfer path (no DMA),
 * READ(6) + opt-in WRITE(6), registered through the Phase-3 block
 * registry (drivers/block.c, which compiles unmodified).
 *
 * Two upstream constructs are dropped, not translated:
 *   - SAVE_ES/RESTORE_ES brackets: ia16-gcc ES damage control.  minic
 *     far accesses materialize their segment per access and the §6d ISR
 *     ABI restores ES on every interrupt return, so there is no ES to
 *     protect.
 *   - the pushf/cli critical sections: the SASI handshake is REQ-driven
 *     (the controller holds REQ until we respond), so an ISR delaying a
 *     poll loop only slows it; the timeout budgets are loop counts of
 *     bus-register reads, orders of magnitude above any ISR latency.
 *     Same category as the §6e finding: the asm was working around the
 *     other toolchain, not the hardware.
 */

#include <errno.h>
#include <stddef.h>
#include <string.h>

#include "v9k_hw.h"
#include "bm_sasi.h"

#ifndef ETIMEDOUT
#define ETIMEDOUT EIO
#endif

#define SASI_READ6_MAX_LBA  0x1FFFFFUL

#define SASI_TIMEOUT_SHORT   1000U
#define SASI_TIMEOUT_MEDIUM  10000U
#define SASI_TIMEOUT_OUTER   0x0100U
#define SASI_TIMEOUT_INNER   0xFFFFU

static sasi_device_t *sasi_active_diag;

static int sasi_block_init(void *ctx);
static int sasi_block_get_info(void *ctx, block_device_info_t *info);
static int sasi_block_status(void *ctx);
static int sasi_block_read_sector(void *ctx, block_lba_t lba, void *buffer);
static int sasi_block_write_sector(void *ctx, block_lba_t lba,
                                   const void *buffer);

static const block_device_ops_t sasi_ops = {
    .init = sasi_block_init,
    .get_info = sasi_block_get_info,
    .status = sasi_block_status,
    .read_sector = sasi_block_read_sector,
    .write_sector = sasi_block_write_sector,
};

static void sasi_delay(uint16_t count) {
    volatile uint16_t i;

    for (i = 0; i < count; i++) {
    }
}

static void sasi_diag_set_active(sasi_device_t *dev) {
    sasi_active_diag = dev;
}

static void sasi_diag_set_step(uint8_t step) {
    if (sasi_active_diag != NULL) {
        sasi_active_diag->diagnostics.last_step = step;
    }
}

static void sasi_diag_record_bus(uint8_t status) {
    if (sasi_active_diag != NULL) {
        sasi_active_diag->diagnostics.last_bus = status;
    }
}

static int sasi_diag_error(int error) {
    if (error < 0 && sasi_active_diag != NULL) {
        sasi_active_diag->diagnostics.last_error = error;
    }
    return error;
}

static void sasi_diag_start_command(sasi_device_t *dev, uint8_t command,
                                    block_lba_t lba, uint8_t step) {
    if (dev == NULL) {
        return;
    }

    dev->diagnostics.last_error = 0;
    dev->diagnostics.last_lba = lba;
    dev->diagnostics.target_id = dev->target_id;
    dev->diagnostics.last_step = step;
    dev->diagnostics.last_command = command;
    dev->diagnostics.last_bus = 0;
    dev->diagnostics.status_byte = 0;
    dev->diagnostics.message_byte = 0;
    dev->diagnostics.sense_len = 0;
    memset(dev->diagnostics.sense, 0, sizeof(dev->diagnostics.sense));
}

static uint8_t sasi_read_bus(void) {
    return HW_READ_BYTE(SASI_BASE_SEGMENT, SASI_HDBUS_OFFSET);
}

static uint8_t sasi_read_data(void) {
    return HW_READ_BYTE(SASI_BASE_SEGMENT, SASI_HDCSD_OFFSET);
}

static void sasi_write_control(uint8_t value) {
    HW_WRITE_BYTE(SASI_BASE_SEGMENT, SASI_HDCTL_OFFSET, value);
}

static void sasi_write_data(uint8_t value) {
    HW_WRITE_BYTE(SASI_BASE_SEGMENT, SASI_HDCSD_OFFSET, value);
}

static int sasi_wait_busy_clear(uint16_t timeout) {
    uint8_t status;

    sasi_diag_set_step(SASI_DIAG_STEP_BUSY_CLEAR);
    while (timeout-- != 0) {
        status = sasi_read_bus();
        sasi_diag_record_bus(status);
        if ((status & SASI_STAT_BUSY) == 0) {
            return 0;
        }
    }

    return sasi_diag_error(-ETIMEDOUT);
}

static int sasi_wait_busy_set(uint16_t timeout) {
    uint8_t status;

    sasi_diag_set_step(SASI_DIAG_STEP_SELECT_BUSY);
    while (timeout-- != 0) {
        status = sasi_read_bus();
        sasi_diag_record_bus(status);
        if ((status & SASI_STAT_BUSY) != 0) {
            return 0;
        }
    }

    return sasi_diag_error(-ETIMEDOUT);
}

static int sasi_wait_phase(uint8_t mask, uint8_t expected,
                           int require_busy, int long_timeout) {
    uint16_t outer;
    uint16_t inner;
    uint8_t status;

    outer = long_timeout ? SASI_TIMEOUT_OUTER : 1U;
    while (outer-- != 0) {
        inner = long_timeout ? SASI_TIMEOUT_INNER : SASI_TIMEOUT_MEDIUM;
        while (inner-- != 0) {
            status = sasi_read_bus();
            sasi_diag_record_bus(status);
            if (require_busy && (status & SASI_STAT_BUSY) == 0) {
                return sasi_diag_error(-EIO);
            }
            if ((status & mask) == expected) {
                return 0;
            }
        }
    }

    return sasi_diag_error(-ETIMEDOUT);
}

static int sasi_wait_command_req(void) {
    sasi_diag_set_step(SASI_DIAG_STEP_COMMAND_REQ);
    return sasi_wait_phase(SASI_STAT_BUSY | SASI_STAT_REQ |
                           SASI_STAT_CONTROL | SASI_STAT_INPUT,
                           SASI_STAT_BUSY | SASI_STAT_REQ |
                           SASI_STAT_CONTROL,
                           1, 0);
}

static int sasi_wait_data_in_phase(void) {
    sasi_diag_set_step(SASI_DIAG_STEP_DATA_IN_PHASE);
    return sasi_wait_phase(SASI_STAT_BUSY | SASI_STAT_CONTROL |
                           SASI_STAT_INPUT,
                           SASI_STAT_BUSY | SASI_STAT_INPUT,
                           1, 1);
}

static int sasi_wait_data_in_req(int long_timeout) {
    if (sasi_active_diag == NULL ||
        sasi_active_diag->diagnostics.last_step != SASI_DIAG_STEP_READ_DATA) {
        sasi_diag_set_step(SASI_DIAG_STEP_DATA_IN_REQ);
    }
    return sasi_wait_phase(SASI_STAT_BUSY | SASI_STAT_REQ |
                           SASI_STAT_CONTROL | SASI_STAT_INPUT,
                           SASI_STAT_BUSY | SASI_STAT_REQ |
                           SASI_STAT_INPUT,
                           1, long_timeout);
}

static int sasi_wait_data_out_phase(void) {
    sasi_diag_set_step(SASI_DIAG_STEP_DATA_OUT_PHASE);
    return sasi_wait_phase(SASI_STAT_BUSY | SASI_STAT_CONTROL |
                           SASI_STAT_INPUT,
                           SASI_STAT_BUSY,
                           1, 1);
}

static int sasi_wait_data_out_req(int long_timeout) {
    if (sasi_active_diag == NULL ||
        sasi_active_diag->diagnostics.last_step != SASI_DIAG_STEP_WRITE_DATA) {
        sasi_diag_set_step(SASI_DIAG_STEP_DATA_OUT_REQ);
    }
    return sasi_wait_phase(SASI_STAT_BUSY | SASI_STAT_REQ |
                           SASI_STAT_CONTROL | SASI_STAT_INPUT,
                           SASI_STAT_BUSY | SASI_STAT_REQ,
                           1, long_timeout);
}

static int sasi_wait_status_phase(void) {
    sasi_diag_set_step(SASI_DIAG_STEP_STATUS_PHASE);
    return sasi_wait_phase(SASI_STAT_BUSY | SASI_STAT_REQ |
                           SASI_STAT_CONTROL | SASI_STAT_INPUT,
                           SASI_STAT_BUSY | SASI_STAT_REQ |
                           SASI_STAT_CONTROL | SASI_STAT_INPUT,
                           1, 1);
}

static int sasi_wait_message_phase(void) {
    sasi_diag_set_step(SASI_DIAG_STEP_MESSAGE_PHASE);
    return sasi_wait_phase(SASI_STAT_BUSY | SASI_STAT_REQ |
                           SASI_STAT_MSG | SASI_STAT_CONTROL |
                           SASI_STAT_INPUT,
                           SASI_STAT_BUSY | SASI_STAT_REQ |
                           SASI_STAT_MSG | SASI_STAT_CONTROL |
                           SASI_STAT_INPUT,
                           1, 1);
}

static void sasi_wait_req_drop(void) {
    uint16_t timeout;

    timeout = 100U;
    while (timeout-- != 0) {
        if ((sasi_read_bus() & SASI_STAT_REQ) == 0) {
            return;
        }
    }
}

static int sasi_send_byte(uint8_t value) {
    int ret;

    sasi_diag_set_step(SASI_DIAG_STEP_CDB);
    ret = sasi_wait_command_req();
    if (ret < 0) {
        return ret;
    }

    sasi_write_data(value);
    sasi_wait_req_drop();
    return 0;
}

static int sasi_select_target(uint8_t target_id) {
    uint8_t target_bit;
    int ret;

    if (target_id > 7) {
        return -EINVAL;
    }

    ret = sasi_wait_busy_clear(SASI_TIMEOUT_MEDIUM);
    if (ret < 0) {
        return ret;
    }

    target_bit = (uint8_t)(1U << target_id);
    sasi_write_data(target_bit);
    sasi_delay(10);

    sasi_write_control(SASI_CTRL_SELECT);
    ret = sasi_wait_busy_set(SASI_TIMEOUT_SHORT);
    if (ret < 0) {
        sasi_write_control(0);
        return ret;
    }

    sasi_write_control(0);
    ret = sasi_wait_command_req();
    if (ret < 0) {
        sasi_write_control(0);
        return ret;
    }

    return 0;
}

static int sasi_send_cdb(const uint8_t cdb[6]) {
    int i;
    int ret;

    for (i = 0; i < 6; i++) {
        ret = sasi_send_byte(cdb[i]);
        if (ret < 0) {
            return ret;
        }
    }

    return 0;
}

static int sasi_finish_command(void) {
    uint8_t status_byte;
    uint8_t message_byte;
    int ret;

    sasi_diag_set_step(SASI_DIAG_STEP_FINISH);
    ret = sasi_wait_status_phase();
    if (ret < 0) {
        return ret;
    }
    status_byte = sasi_read_data();
    if (sasi_active_diag != NULL) {
        sasi_active_diag->diagnostics.status_byte = status_byte;
    }

    ret = sasi_wait_message_phase();
    if (ret < 0) {
        return ret;
    }
    message_byte = sasi_read_data();
    if (sasi_active_diag != NULL) {
        sasi_active_diag->diagnostics.message_byte = message_byte;
    }

    if (status_byte != 0 || message_byte != 0) {
        return sasi_diag_error(-EIO);
    }

    return 0;
}

static int sasi_command_no_data(sasi_device_t *dev, uint8_t command) {
    uint8_t cdb[6];
    int ret;

    sasi_diag_start_command(dev, command, 0, SASI_DIAG_STEP_COMMAND_REQ);
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = command;

    ret = sasi_select_target(dev->target_id);
    if (ret < 0) {
        return ret;
    }

    ret = sasi_send_cdb(cdb);
    if (ret < 0) {
        return ret;
    }

    return sasi_finish_command();
}

static int sasi_request_sense(sasi_device_t *dev) {
    uint8_t cdb[6];
    int i;
    int ret;

    sasi_diag_start_command(dev, SASI_CMD_REQUEST_SENSE, 0,
                            SASI_DIAG_STEP_REQUEST_SENSE);
    memset(cdb, 0, sizeof(cdb));
    cdb[0] = SASI_CMD_REQUEST_SENSE;

    ret = sasi_select_target(dev->target_id);
    if (ret < 0) {
        return ret;
    }

    ret = sasi_send_cdb(cdb);
    if (ret < 0) {
        return ret;
    }

    ret = sasi_wait_data_in_phase();
    if (ret < 0) {
        return ret;
    }

    for (i = 0; i < 4; i++) {
        ret = sasi_wait_data_in_req(i == 0);
        if (ret < 0) {
            return ret;
        }
        dev->diagnostics.sense[i] = sasi_read_data();
        dev->diagnostics.sense_len = (uint8_t)(i + 1);
    }

    return sasi_finish_command();
}

static int sasi_reset_controller(void) {
    sasi_diag_set_step(SASI_DIAG_STEP_RESET);
    sasi_write_control(SASI_CTRL_RESET);
    sasi_delay(SASI_TIMEOUT_MEDIUM);
    sasi_write_control(0);
    sasi_delay(SASI_TIMEOUT_MEDIUM);

    return 0;
}

static int sasi_initialize_hardware(sasi_device_t *dev) {
    int ret;

    ret = sasi_reset_controller();
    if (ret < 0) {
        return ret;
    }

    ret = sasi_command_no_data(dev, SASI_CMD_TEST_UNIT_READY);
    if (ret < 0) {
        return ret;
    }

    ret = sasi_command_no_data(dev, SASI_CMD_XEBEC_RAM_TEST);
    if (ret < 0) {
        return ret;
    }

    ret = sasi_command_no_data(dev, SASI_CMD_XEBEC_CTRL_TEST);
    if (ret < 0) {
        return ret;
    }

    return sasi_request_sense(dev);
}

static int sasi_read_sector_manual(sasi_device_t *dev, block_lba_t lba,
                                   void *buffer) {
    uint8_t cdb[6];
    uint8_t *out;
    uint16_t i;
    int ret;

    sasi_diag_start_command(dev, SASI_CMD_READ, lba,
                            SASI_DIAG_STEP_COMMAND_REQ);
    if (lba > SASI_READ6_MAX_LBA) {
        return sasi_diag_error(-EINVAL);
    }

    cdb[0] = SASI_CMD_READ;
    cdb[1] = (uint8_t)((lba >> 16) & 0x1F);
    cdb[2] = (uint8_t)((lba >> 8) & 0xFF);
    cdb[3] = (uint8_t)(lba & 0xFF);
    cdb[4] = 1;
    cdb[5] = 0;

    ret = sasi_select_target(dev->target_id);
    if (ret < 0) {
        return ret;
    }

    ret = sasi_send_cdb(cdb);
    if (ret < 0) {
        return ret;
    }

    ret = sasi_wait_data_in_phase();
    if (ret < 0) {
        return ret;
    }

    out = (uint8_t *)buffer;
    for (i = 0; i < SASI_SECTOR_SIZE; i++) {
        sasi_diag_set_step(SASI_DIAG_STEP_READ_DATA);
        ret = sasi_wait_data_in_req(i == 0);
        if (ret < 0) {
            return ret;
        }
        out[i] = sasi_read_data();
    }

    return sasi_finish_command();
}

static int sasi_write_sector_manual(sasi_device_t *dev, block_lba_t lba,
                                    const void *buffer) {
    uint8_t cdb[6];
    const uint8_t *in;
    uint16_t i;
    int ret;

    sasi_diag_start_command(dev, SASI_CMD_WRITE, lba,
                            SASI_DIAG_STEP_COMMAND_REQ);
    if (lba > SASI_READ6_MAX_LBA) {
        return sasi_diag_error(-EINVAL);
    }

    cdb[0] = SASI_CMD_WRITE;
    cdb[1] = (uint8_t)((lba >> 16) & 0x1F);
    cdb[2] = (uint8_t)((lba >> 8) & 0xFF);
    cdb[3] = (uint8_t)(lba & 0xFF);
    cdb[4] = 1;
    cdb[5] = 0;

    ret = sasi_select_target(dev->target_id);
    if (ret < 0) {
        return ret;
    }

    ret = sasi_send_cdb(cdb);
    if (ret < 0) {
        return ret;
    }

    ret = sasi_wait_data_out_phase();
    if (ret < 0) {
        return ret;
    }

    in = (const uint8_t *)buffer;
    for (i = 0; i < SASI_SECTOR_SIZE; i++) {
        sasi_diag_set_step(SASI_DIAG_STEP_WRITE_DATA);
        ret = sasi_wait_data_out_req(i == 0);
        if (ret < 0) {
            return ret;
        }
        sasi_write_data(in[i]);
        sasi_wait_req_drop();
    }

    return sasi_finish_command();
}

static const char *sasi_name_for_target(uint8_t target_id) {
    switch (target_id) {
    case 0:
        return "sasi0";
    case 1:
        return "sasi1";
    case 2:
        return "sasi2";
    case 3:
        return "sasi3";
    default:
        return "sasi";
    }
}

static int sasi_validate(sasi_device_t *dev) {
    if (dev == NULL || dev->target_id > 7) {
        return -EINVAL;
    }

    return 0;
}

void sasi_clear_diagnostics(sasi_device_t *dev) {
    if (dev == NULL) {
        return;
    }

    memset(&dev->diagnostics, 0, sizeof(dev->diagnostics));
    dev->diagnostics.target_id = dev->target_id;
}

void sasi_get_diagnostics(const sasi_device_t *dev,
                          sasi_diagnostics_t *diagnostics) {
    if (dev == NULL || diagnostics == NULL) {
        return;
    }

    *diagnostics = dev->diagnostics;
}

uint8_t sasi_read_bus_status(void) {
    return sasi_read_bus();
}

int sasi_refresh_sense(sasi_device_t *dev) {
    int ret;

    ret = sasi_validate(dev);
    if (ret < 0) {
        return ret;
    }

    sasi_diag_set_active(dev);
    ret = sasi_request_sense(dev);
    if (ret < 0) {
        sasi_write_control(0);
    }
    sasi_diag_set_active(NULL);

    return ret;
}

int sasi_register(sasi_device_t *dev) {
    int ret;

    ret = sasi_validate(dev);
    if (ret < 0) {
        return ret;
    }

    if (dev->total_sectors == 0) {
        dev->total_sectors = SASI_DEFAULT_TOTAL_SECTORS;
    }
    dev->initialized = 0;
    sasi_clear_diagnostics(dev);

    return block_register(sasi_name_for_target(dev->target_id), &sasi_ops, dev);
}

static int sasi_block_init(void *ctx) {
    sasi_device_t *dev = (sasi_device_t *)ctx;
    int ret;

    ret = sasi_validate(dev);
    if (ret < 0) {
        return ret;
    }

    if (dev->initialized) {
        return 0;
    }

    sasi_diag_set_active(dev);
    ret = sasi_initialize_hardware(dev);
    if (ret < 0) {
        sasi_write_control(0);
    }
    sasi_diag_set_active(NULL);

    if (ret < 0) {
        dev->initialized = 0;
        return ret;
    }

    dev->initialized = 1;
    return 0;
}

static int sasi_block_get_info(void *ctx, block_device_info_t *info) {
    sasi_device_t *dev = (sasi_device_t *)ctx;
    int ret;

    if (info == NULL) {
        return -EINVAL;
    }

    ret = sasi_validate(dev);
    if (ret < 0) {
        return ret;
    }

    memset(info, 0, sizeof(*info));
    info->name = sasi_name_for_target(dev->target_id);
    info->sector_size = SASI_SECTOR_SIZE;
    info->total_sectors = dev->total_sectors != 0 ?
        dev->total_sectors : SASI_DEFAULT_TOTAL_SECTORS;
    if (!dev->allow_writes) {
        info->flags = BLOCK_DEVICE_FLAG_READ_ONLY;
    }

    return 0;
}

static int sasi_block_status(void *ctx) {
    sasi_device_t *dev = (sasi_device_t *)ctx;
    int ret;

    ret = sasi_validate(dev);
    if (ret < 0) {
        return ret;
    }

    return (dev->initialized ? BLOCK_STATUS_READY : 0) |
        (dev->allow_writes ? 0 : BLOCK_STATUS_READ_ONLY);
}

static int sasi_block_read_sector(void *ctx, block_lba_t lba, void *buffer) {
    sasi_device_t *dev = (sasi_device_t *)ctx;
    int ret;

    if (buffer == NULL) {
        return -EINVAL;
    }

    ret = sasi_validate(dev);
    if (ret < 0) {
        return ret;
    }

    if (!dev->initialized) {
        return -ENODEV;
    }

    sasi_diag_set_active(dev);
    ret = sasi_read_sector_manual(dev, lba, buffer);
    if (ret < 0) {
        sasi_write_control(0);
    }
    sasi_diag_set_active(NULL);

    return ret;
}

static int sasi_block_write_sector(void *ctx, block_lba_t lba,
                                   const void *buffer) {
    sasi_device_t *dev = (sasi_device_t *)ctx;
    int ret;

    if (buffer == NULL) {
        return -EINVAL;
    }

    ret = sasi_validate(dev);
    if (ret < 0) {
        return ret;
    }

    if (!dev->allow_writes) {
        return -EROFS;
    }

    if (!dev->initialized) {
        return -ENODEV;
    }

    sasi_diag_set_active(dev);
    ret = sasi_write_sector_manual(dev, lba, buffer);
    if (ret < 0) {
        sasi_write_control(0);
    }
    sasi_diag_set_active(NULL);

    return ret;
}
