/*
 * bm_keyboard.c -- bare-metal Victor 9000 keyboard driver (§6e,
 * Phase-6 step 4b).
 *
 * A minic-dialect port of newlibc's drivers/keyboard.c.  The Victor
 * keyboard shifts 8 data bits through VIA CS2's shift register, then
 * completes a stop-bit handshake on KBRDY/KBACK; the BIOS state machine
 * (SHIFTING -> STOP_LOW -> STOP_HIGH) is preserved exactly, as are the
 * MAME-validated ASCII map, the Shift/RPT(Ctrl)/Alt subsets, and the
 * S88-Return compatibility path.
 *
 * Two deliberate divergences from the original:
 *   - the port is INTERRUPT-DRIVEN ONLY.  The dedicated KBINT line
 *     (IR6) follows the VIA2 IRQ, so every state-machine step (SR
 *     ready, then each CB1 handshake edge) raises IR6 and the ISR is
 *     the only event producer.  The consumer side just pops the ring
 *     buffer (one-byte head/tail indexes -- no cli window needed), so
 *     the original's pushf/cli flags-save inline asm has no minic
 *     equivalent to need;
 *   - SAVE_ES/RESTORE_ES pairs are gone: the compiler-emitted
 *     __attribute__((interrupt)) ABI saves ES to CS-local static
 *     memory itself (the §6d hardware-validated rule).
 */

#include <stdint.h>
#include "v9k_hw.h"
#include "bm_pic.h"
#include "bm_interrupts.h"
#include "bm_keyboard.h"

#define VIA_IFR_SR             0x04    /* shift register interrupt flag */
#define VIA_IFR_CB1            0x10    /* CB1 edge interrupt flag */
#define VIA_IER_ENABLE         0x80
#define VIA_IER_DISABLE        0x00
#define VIA_ACR_SR_MASK        0x1C    /* shift register mode bits */
#define VIA_ACR_SR_EXT_IN      0x0C    /* shift in under external CB1 clock */
#define VIA_PCR_CB1_POS_EDGE   0x10

#define KBD_DATA_BIT           0x40    /* PA6: keyboard data */
#define KBD_ACK_CTL            0x02    /* PB1: keyboard acknowledge */
#define KBD_EVENT_DOWN         0x80
#define KBD_EVENT_KEY_MASK     0x7F
#define KBD_EVENT_BUFFER_SIZE  32      /* power of two */
#define KBD_EVENT_BUFFER_MASK  (KBD_EVENT_BUFFER_SIZE - 1)

#define KBD_KEY_SHIFT_1        75
#define KBD_KEY_SHIFT_2        87
#define KBD_KEY_S88            88      /* MAME Return compatibility */
#define KBD_KEY_RPT            95      /* MAME maps host Ctrl here */
#define KBD_KEY_ALT            96

#define KBD_MOD_SHIFT_1        0x01
#define KBD_MOD_SHIFT_2        0x02
#define KBD_MOD_SHIFT_S88      0x04
#define KBD_MOD_RPT            0x08
#define KBD_MOD_ALT            0x10
#define KBD_MOD_SHIFT_MASK     (KBD_MOD_SHIFT_1|KBD_MOD_SHIFT_2|KBD_MOD_SHIFT_S88)

#define KBD_STATE_SHIFTING     0
#define KBD_STATE_STOP_LOW     1
#define KBD_STATE_STOP_HIGH    2

typedef struct {
    uint8_t key;       /* S-key number, one-based */
    uint8_t normal;    /* ASCII without Shift */
    uint8_t shifted;   /* ASCII with Shift, 0 = reuse normal */
} keyboard_ascii_entry_t;

static volatile Via6522Registers __far *kbd_via;
static int kbd_state;
static uint8_t kbd_shift_data;
static volatile uint8_t event_buffer[KBD_EVENT_BUFFER_SIZE];
static volatile uint8_t event_head;
static volatile uint8_t event_tail;
static volatile uint16_t event_overruns;
static volatile uint16_t isr_entries;
static int pending_char;
static uint8_t modifier_state;
static int s88_pending_return;
static uint8_t isr_event;

static const uint8_t reverse_nibble[16] = {
    0x0, 0x8, 0x4, 0xC, 0x2, 0xA, 0x6, 0xE,
    0x1, 0x9, 0x5, 0xD, 0x3, 0xB, 0x7, 0xF
};

static uint8_t reverse_keyboard_byte(uint8_t value) {
    return (uint8_t)((reverse_nibble[value & 0x0F] << 4) |
                     reverse_nibble[(value >> 4) & 0x0F]);
}

static void event_buffer_clear(void) {
    event_head = 0;
    event_tail = 0;
    event_overruns = 0;
}

/* Producer: ISR context only. */
static int event_buffer_push(uint8_t event) {
    uint8_t next_head = (uint8_t)((event_head + 1) & KBD_EVENT_BUFFER_MASK);

    if (next_head == event_tail) {
        event_overruns = event_overruns + 1;
        return 0;
    }
    event_buffer[event_head] = event;
    event_head = next_head;
    return 1;
}

/* Consumer: main context only. */
static int event_buffer_pop(uint8_t *event) {
    if (event_head == event_tail)
        return 0;
    *event = event_buffer[event_tail];
    event_tail = (uint8_t)((event_tail + 1) & KBD_EVENT_BUFFER_MASK);
    return 1;
}

static void keyboard_reset_hardware(void) {
    volatile uint8_t dummy;

    /* Clear CB1 interrupt enable, release KBACK, arm the shift register. */
    kbd_via->int_enable_reg = (uint8_t)(VIA_IER_DISABLE | VIA_IFR_CB1);
    kbd_via->out_in_reg_b = kbd_via->out_in_reg_b & (uint8_t)~KBD_ACK_CTL;
    kbd_via->aux_ctl_reg =
        (uint8_t)((kbd_via->aux_ctl_reg & (uint8_t)~VIA_ACR_SR_MASK) |
                  VIA_ACR_SR_EXT_IN);

    dummy = kbd_via->shift_reg;      /* clear any pending SR interrupt */
    (void)dummy;

    kbd_via->int_enable_reg = (uint8_t)(VIA_IER_ENABLE | VIA_IFR_SR);
    kbd_state = KBD_STATE_SHIFTING;
}

static void keyboard_resync(void) {
    volatile uint16_t i;

    kbd_via->out_in_reg_b = kbd_via->out_in_reg_b | KBD_ACK_CTL;
    kbd_via->int_enable_reg = 0x7F;
    for (i = 0; i < 1000; i++)
        ;   /* let the keyboard see the forced acknowledge */
    keyboard_reset_hardware();
}

static int keyboard_service_hardware(uint8_t *event) {
    if (kbd_via == 0 || event == 0)
        return 0;

    if (kbd_state == KBD_STATE_SHIFTING) {
        if ((kbd_via->int_flag_reg & VIA_IFR_SR) == 0)
            return 0;
        kbd_via->aux_ctl_reg = kbd_via->aux_ctl_reg & (uint8_t)~VIA_ACR_SR_MASK;
        kbd_via->periph_ctl_reg =
            kbd_via->periph_ctl_reg & (uint8_t)~VIA_PCR_CB1_POS_EDGE;
        kbd_via->int_enable_reg = (uint8_t)(VIA_IER_ENABLE | VIA_IFR_CB1);

        kbd_shift_data = kbd_via->shift_reg;
        kbd_via->int_enable_reg = (uint8_t)(VIA_IER_DISABLE | VIA_IFR_SR);
        kbd_via->out_in_reg_b = kbd_via->out_in_reg_b | KBD_ACK_CTL;
        kbd_state = KBD_STATE_STOP_LOW;
    }

    if (kbd_state == KBD_STATE_STOP_LOW) {
        if ((kbd_via->int_flag_reg & VIA_IFR_CB1) == 0)
            return 0;
        if ((kbd_via->out_in_reg_a & KBD_DATA_BIT) != 0) {
            keyboard_resync();
            return 0;
        }
        kbd_via->periph_ctl_reg =
            kbd_via->periph_ctl_reg | VIA_PCR_CB1_POS_EDGE;
        kbd_via->out_in_reg_b = kbd_via->out_in_reg_b & (uint8_t)~KBD_ACK_CTL;
        kbd_state = KBD_STATE_STOP_HIGH;
    }

    if (kbd_state == KBD_STATE_STOP_HIGH) {
        if ((kbd_via->int_flag_reg & VIA_IFR_CB1) == 0)
            return 0;
        if ((kbd_via->out_in_reg_a & KBD_DATA_BIT) == 0) {
            keyboard_resync();
            return 0;
        }
        *event = reverse_keyboard_byte(kbd_shift_data);
        keyboard_reset_hardware();
        return 1;
    }

    keyboard_resync();
    return 0;
}

static const keyboard_ascii_entry_t keyboard_ascii_map[] = {
    { 14, '1', '!' }, { 15, '2', '@' }, { 16, '3', '#' },
    { 17, '4', '$' }, { 18, '5', '%' }, { 19, '6', '^' },
    { 20, '7', '&' }, { 21, '8', '*' }, { 22, '9', '(' },
    { 23, '0', ')' }, { 24, '-', '_' }, { 25, '=', '+' },
    { 26, '\b', 0 },  { 28, 0x7F, 0 },

    { 34, '\t', 0 },  { 35, 'q', 'Q' }, { 36, 'w', 'W' },
    { 37, 'e', 'E' }, { 38, 'r', 'R' }, { 39, 't', 'T' },
    { 40, 'y', 'Y' }, { 41, 'u', 'U' }, { 42, 'i', 'I' },
    { 43, 'o', 'O' }, { 44, 'p', 'P' }, { 46, ']', '[' },

    { 50, '7', 0 },   { 51, '8', 0 },   { 52, '9', 0 },
    { 53, '-', 0 },   { 54, 0x1B, 0 },

    { 56, 'a', 'A' }, { 57, 's', 'S' }, { 58, 'd', 'D' },
    { 59, 'f', 'F' }, { 60, 'g', 'G' }, { 61, 'h', 'H' },
    { 62, 'j', 'J' }, { 63, 'k', 'K' }, { 64, 'l', 'L' },
    { 65, ';', ':' }, { 66, '\'', '"' }, { 67, '\r', 0 },
    { 68, '\r', 0 },

    { 70, '4', 0 },   { 71, '5', 0 },   { 72, '6', 0 },
    { 73, '+', 0 },

    { 77, 'z', 'Z' }, { 78, 'x', 'X' }, { 79, 'c', 'C' },
    { 80, 'v', 'V' }, { 81, 'b', 'B' }, { 82, 'n', 'N' },
    { 83, 'm', 'M' }, { 84, ',', '<' }, { 85, '.', '>' },
    { 86, '/', '?' },

    { 91, '1', 0 },   { 92, '2', 0 },   { 93, '3', 0 },
    { 94, '\r', 0 },  { 97, ' ', 0 },   { 101, '0', 0 },
    { 103, '.', 0 }
};

#define KBD_ASCII_MAP_LEN \
    (sizeof(keyboard_ascii_map) / sizeof(keyboard_ascii_map[0]))

static int keyboard_key_to_ascii(uint8_t key, int shifted) {
    unsigned int i;

    for (i = 0; i < KBD_ASCII_MAP_LEN; i++) {
        if (keyboard_ascii_map[i].key == key) {
            if (shifted && keyboard_ascii_map[i].shifted != 0)
                return keyboard_ascii_map[i].shifted;
            return keyboard_ascii_map[i].normal;
        }
    }
    return -1;
}

/* Alt + top row emits the keytop alternate punctuation. */
static const char kbd_alt_low[3] = { '|', '<', '>' };
static const char kbd_alt_high[6] = { '^', '`', '{', '}', '~', '\\' };

static int keyboard_alt_to_ascii(uint8_t key) {
    if (key < 14 || key > 25)
        return -1;
    if (key <= 16)
        return kbd_alt_low[key - 14];
    if (key >= 20)
        return kbd_alt_high[key - 20];
    return -1;
}

static void keyboard_set_modifier(uint8_t modifier, int key_down) {
    if (key_down)
        modifier_state = modifier_state | modifier;
    else
        modifier_state = modifier_state & (uint8_t)~modifier;
}

static int keyboard_shift_active(void) {
    return (modifier_state & KBD_MOD_SHIFT_MASK) != 0;
}

static int keyboard_event_to_ascii(uint8_t event) {
    uint8_t key;
    int key_down;
    int c;

    key_down = (event & KBD_EVENT_DOWN) != 0;

    /* Packets store key numbers zero-based; normalize to S01..S104. */
    key = (uint8_t)((event & KBD_EVENT_KEY_MASK) + 1);

    if (key == KBD_KEY_SHIFT_1) {
        keyboard_set_modifier(KBD_MOD_SHIFT_1, key_down);
        return -1;
    }
    if (key == KBD_KEY_SHIFT_2) {
        keyboard_set_modifier(KBD_MOD_SHIFT_2, key_down);
        return -1;
    }
    if (key == KBD_KEY_RPT) {
        keyboard_set_modifier(KBD_MOD_RPT, key_down);
        return -1;
    }
    if (key == KBD_KEY_ALT) {
        keyboard_set_modifier(KBD_MOD_ALT, key_down);
        return -1;
    }

    /* MAME reports the host Return key as S88: pressed/released alone
     * it returns CR, held with another key it acts as Shift. */
    if (key == KBD_KEY_S88) {
        if (key_down) {
            s88_pending_return = 1;
            keyboard_set_modifier(KBD_MOD_SHIFT_S88, 1);
            return -1;
        }
        keyboard_set_modifier(KBD_MOD_SHIFT_S88, 0);
        if (s88_pending_return) {
            s88_pending_return = 0;
            return '\r';
        }
        return -1;
    }

    if (s88_pending_return)
        s88_pending_return = 0;

    if (!key_down)
        return -1;

    c = keyboard_key_to_ascii(key, keyboard_shift_active());
    if ((modifier_state & KBD_MOD_ALT) != 0) {
        int alt_c = keyboard_alt_to_ascii(key);

        if (alt_c >= 0)
            return alt_c;
        if (c >= 0x40 && c <= 0x7F)
            return c & 0x1F;
        return -1;
    }

    if ((modifier_state & KBD_MOD_RPT) != 0 && c >= 0x40 && c <= 0x7F)
        c = c & 0x1F;
    return c;
}

static int keyboard_read_char_nonblock(void) {
    uint8_t event;
    int c;

    while (event_buffer_pop(&event)) {
        c = keyboard_event_to_ascii(event);
        if (c >= 0)
            return c;
    }
    return -1;
}

static int keyboard_fill_pending(void) {
    if (pending_char >= 0)
        return 1;
    pending_char = keyboard_read_char_nonblock();
    return pending_char >= 0;
}

/* The IR6 ISR: one state-machine service per KBINT.  The compiler owns
 * the prologue/epilogue (ES to CS-local static memory, all registers,
 * DS/ES=DGROUP, iret); the EOI is SPECIFIC, to the memory-mapped PIC. */
void __far __attribute__((interrupt)) bm_keyboard_isr(void) {
    isr_entries = isr_entries + 1;
    if (keyboard_service_hardware(&isr_event))
        event_buffer_push(isr_event);
    HW_WRITE_BYTE(INTEL_DEV_SEGMENT, PIC_COMMAND_PORT,
                  (uint8_t)(0x60 | IRQ_KEYBOARD));
}

void bm_keyboard_init(void) {
    /* Keyboard VIA is CS2 at E800:0040; data path is the shift register
     * + KBACK handshake, but the interrupt is the DEDICATED KBINT line
     * on IR6 (not the shared VIA IR3). */
    kbd_via = (volatile Via6522Registers __far *)
        MK_FP(PHASE2_DEV_SEGMENT, VIA_KEYBOARD_OFFSET);

    kbd_via->out_in_reg_b =
        kbd_via->out_in_reg_b & (uint8_t)~(KBD_ACK_CTL | 0x01);
    kbd_via->data_dir_reg_a =
        kbd_via->data_dir_reg_a & (uint8_t)~KBD_DATA_BIT;
    kbd_via->data_dir_reg_b = kbd_via->data_dir_reg_b | KBD_ACK_CTL;
    kbd_via->int_enable_reg = 0x7F;
    kbd_via->int_flag_reg = 0x7F;
    kbd_via->periph_ctl_reg = 0;
    kbd_via->aux_ctl_reg = kbd_via->aux_ctl_reg & (uint8_t)~VIA_ACR_SR_MASK;

    pending_char = -1;
    modifier_state = 0;
    s88_pending_return = 0;
    isr_entries = 0;
    event_buffer_clear();

    /* A warm reload can leave the keyboard mid-handshake: force a clean
     * acknowledge window before arming the shift register. */
    keyboard_resync();

    /* ISR before unmask; interrupts are still globally disabled. */
    bm_install_isr(INT_KEYBOARD, (bm_isr_fn_t)bm_keyboard_isr);
    bm_pic_unmask(IRQ_KEYBOARD);
}

int bm_keyboard_getc_nonblock(void) {
    int c;

    if (pending_char >= 0) {
        c = pending_char;
        pending_char = -1;
        return c;
    }
    return keyboard_read_char_nonblock();
}

int bm_keyboard_getc(void) {
    int c;

    do {
        c = bm_keyboard_getc_nonblock();
    } while (c < 0);
    return c;
}

int bm_keyboard_hit(void) {
    return keyboard_fill_pending();
}

int bm_keyboard_get_raw_event_nonblock(void) {
    uint8_t event;

    if (event_buffer_pop(&event))
        return event;
    return -1;
}

unsigned int bm_keyboard_isr_count(void) {
    return isr_entries;
}

unsigned int bm_keyboard_overruns(void) {
    return event_overruns;
}
