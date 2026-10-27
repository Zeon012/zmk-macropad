/*
 * IR transmitter on the nRF PWM. A whole frame is built as a sequence of carrier
 * periods (one PWM period per carrier cycle, either a 1/3-duty pulse or nothing)
 * and played by EasyDMA, so the timing doesn't depend on the CPU. The pin, from
 * ir-gpios in the zephyr,user node, drives the IR LED's transistor (high = on).
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/devicetree.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <nrfx_pwm.h>

#include "ir.h"

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define IR_NODE DT_PATH(zephyr_user)
#define IR_PIN                                                                                     \
    NRF_GPIO_PIN_MAP(DT_PROP(DT_GPIO_CTLR(IR_NODE, ir_gpios), port),                               \
                     DT_GPIO_PIN(IR_NODE, ir_gpios))

#define PWM_CLOCK_HZ 16000000u
#define POLARITY_NORMAL BIT(15) /* high for <compare> ticks, as Zephyr's pwm_nrfx sets it */

static nrfx_pwm_t pwm = NRFX_PWM_INSTANCE(2);
static uint16_t seq_buf[CONFIG_MACROPAD_IR_MAX_PERIODS];
static size_t seq_len;
static uint32_t carrier_hz;
static uint16_t mark_value;
static uint32_t frame_us;
static bool overflow;
static bool rc_toggle;

static struct ir_code pending;
static bool pending_repeat;
static void tx_work_handler(struct k_work *work);
static K_WORK_DELAYABLE_DEFINE(tx_work, tx_work_handler);

static void begin(uint32_t hz) {
    carrier_hz = hz;
    mark_value = (PWM_CLOCK_HZ / hz / 3) | POLARITY_NORMAL;
    seq_len = 0;
    frame_us = 0;
    overflow = false;
}

static void emit(bool mark, uint32_t us) {
    frame_us += us;
    if (!mark && seq_len == 0) {
        return; /* leading silence: the LED is already off */
    }
    uint32_t periods = (uint32_t)(((uint64_t)us * carrier_hz + 500000) / 1000000);
    if (seq_len + periods > ARRAY_SIZE(seq_buf)) {
        overflow = true;
        return;
    }
    uint16_t value = mark ? mark_value : POLARITY_NORMAL;
    for (uint32_t i = 0; i < periods; i++) {
        seq_buf[seq_len++] = value;
    }
}

/* Pulse-distance bits, least significant first (NEC family). */
static void pd_bits(uint32_t value, int count, uint32_t mark, uint32_t zero, uint32_t one) {
    for (int i = 0; i < count; i++) {
        emit(true, mark);
        emit(false, (value >> i) & 1 ? one : zero);
    }
}

static void nec_frame(uint32_t hdr_mark, uint32_t hdr_space, uint8_t b0, uint8_t b1, uint8_t b2,
                      uint8_t b3) {
    emit(true, hdr_mark);
    emit(false, hdr_space);
    pd_bits(b0 | b1 << 8 | b2 << 16 | (uint32_t)b3 << 24, 32, 560, 560, 1690);
    emit(true, 560);
}

/* Sony: pulse-width bits, LSB first, the frame sent three times 45 ms apart. */
static void sirc_frames(uint32_t cmd, uint32_t addr, int addr_bits) {
    for (int f = 0; f < 3; f++) {
        frame_us = 0;
        emit(true, 2400);
        emit(false, 600);
        uint32_t bits = (cmd & 0x7f) | addr << 7;
        for (int i = 0; i < 7 + addr_bits; i++) {
            emit(true, (bits >> i) & 1 ? 1200 : 600);
            emit(false, 600);
        }
        if (f < 2 && frame_us < 45000) {
            emit(false, 45000 - frame_us);
        }
    }
}

/* Philips RC5: Manchester, 889 us halves, 1 = space then mark. */
static void rc5_frame(uint32_t addr, uint32_t cmd, bool extended) {
    uint32_t bits = 1u << 13;
    bits |= (extended ? !(cmd & 0x40) : 1u) << 12;
    bits |= (uint32_t)rc_toggle << 11;
    bits |= (addr & 0x1f) << 6;
    bits |= cmd & 0x3f;
    for (int i = 13; i >= 0; i--) {
        bool one = (bits >> i) & 1;
        emit(!one, 889);
        emit(one, 889);
    }
}

/* Philips RC6 mode 0: leader, start bit, 3 mode bits, double-width toggle,
 * 8-bit address, 8-bit command; Manchester with 1 = mark then space. */
static void rc6_bit(bool one, uint32_t half) {
    emit(one, half);
    emit(!one, half);
}

static void rc6_frame(uint32_t addr, uint32_t cmd) {
    emit(true, 2664);
    emit(false, 888);
    rc6_bit(true, 444);
    for (int i = 0; i < 3; i++) {
        rc6_bit(false, 444);
    }
    rc6_bit(rc_toggle, 888);
    for (int i = 7; i >= 0; i--) {
        rc6_bit((addr >> i) & 1, 444);
    }
    for (int i = 7; i >= 0; i--) {
        rc6_bit((cmd >> i) & 1, 444);
    }
}

static bool build(const struct ir_code *c) {
    switch (c->proto) {
    case IR_PROTO_NEC:
        begin(38000);
        nec_frame(9000, 4500, c->addr, ~c->addr, c->cmd, ~c->cmd);
        break;
    case IR_PROTO_NECEXT:
        begin(38000);
        nec_frame(9000, 4500, c->addr, c->addr >> 8, c->cmd,
                  c->cmd > 0xff ? c->cmd >> 8 : ~c->cmd);
        break;
    case IR_PROTO_SAMSUNG32:
        begin(38000);
        nec_frame(4500, 4500, c->addr, c->addr, c->cmd, ~c->cmd);
        break;
    case IR_PROTO_SIRC:
        begin(40000);
        sirc_frames(c->cmd, c->addr & 0x1f, 5);
        break;
    case IR_PROTO_SIRC15:
        begin(40000);
        sirc_frames(c->cmd, c->addr & 0xff, 8);
        break;
    case IR_PROTO_SIRC20:
        begin(40000);
        sirc_frames(c->cmd, c->addr & 0x1fff, 13);
        break;
    case IR_PROTO_RC5:
    case IR_PROTO_RC5X:
        begin(36000);
        rc5_frame(c->addr, c->cmd, c->proto == IR_PROTO_RC5X);
        break;
    case IR_PROTO_RC6:
        begin(36000);
        rc6_frame(c->addr, c->cmd);
        break;
    default:
        return false;
    }
    emit(false, 1000); /* end on a space so the pin rests low */
    if (overflow) {
        LOG_ERR("IR frame too long for the %d-period buffer", (int)ARRAY_SIZE(seq_buf));
        return false;
    }
    return true;
}

static void tx_work_handler(struct k_work *work) {
    if (!nrfx_pwm_stopped_check(&pwm)) {
        k_work_reschedule(&tx_work, K_MSEC(5)); /* previous frame still playing */
        return;
    }
    if (!pending_repeat && (pending.proto == IR_PROTO_RC5 || pending.proto == IR_PROTO_RC5X ||
                            pending.proto == IR_PROTO_RC6)) {
        rc_toggle = !rc_toggle;
    }
    if (!build(&pending)) {
        return;
    }
    nrf_pwm_configure(pwm.p_reg, NRF_PWM_CLK_16MHz, NRF_PWM_MODE_UP, PWM_CLOCK_HZ / carrier_hz);
    nrf_pwm_sequence_t seq = {
        .values = {.p_common = seq_buf},
        .length = seq_len,
        .repeats = 0,
        .end_delay = 0,
    };
    nrfx_pwm_simple_playback(&pwm, &seq, 1, NRFX_PWM_FLAG_STOP);
}

int ir_tx_send(const struct ir_code *code, bool repeat) {
    if (code->proto == IR_PROTO_NONE) {
        return -ENOENT;
    }
    pending = *code;
    pending_repeat = repeat;
    k_work_reschedule(&tx_work, K_NO_WAIT);
    return 0;
}

static int ir_tx_init(void) {
    nrfx_pwm_config_t config = NRFX_PWM_DEFAULT_CONFIG(IR_PIN, NRF_PWM_PIN_NOT_CONNECTED,
                                                       NRF_PWM_PIN_NOT_CONNECTED,
                                                       NRF_PWM_PIN_NOT_CONNECTED);
    config.base_clock = NRF_PWM_CLK_16MHz;
    config.top_value = PWM_CLOCK_HZ / 38000;
    /* No handler: no interrupts, completion is polled before the next frame. */
    if (nrfx_pwm_init(&pwm, &config, NULL, NULL) != NRFX_SUCCESS) {
        LOG_ERR("IR: PWM2 init failed");
        return -EIO;
    }
    return 0;
}

SYS_INIT(ir_tx_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
