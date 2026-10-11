/*
 * Per-action LED effects for the macropad, ported from the original ESP32
 * firmware (codex-micro-device/firmware/main.cpp). Owns the LED strip that is
 * chosen as zmk,underglow, so ZMK's RGB underglow must be disabled.
 *
 * SPDX-License-Identifier: MIT
 */

#include <string.h>

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led_strip.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

#include <zmk/activity.h>
#include <zmk/event_manager.h>
#include <zmk/events/activity_state_changed.h>
#include <zmk/events/position_state_changed.h>
#include <zmk/events/sensor_event.h>
#include <zmk/workqueue.h>

#if IS_ENABLED(CONFIG_ZMK_USB)
#include <zmk/events/usb_conn_state_changed.h>
#include <zmk/usb.h>
#endif

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#define STRIP_NODE DT_CHOSEN(zmk_underglow)
#define LED_COUNT DT_PROP(STRIP_NODE, chain_length)

#define LONG_PRESS_MS 700
#define REASONING_LEVEL_MAX 4
#define TRIGGERS_PER_ROTATION                                                                      \
    DT_PROP_OR(DT_INST(0, zmk_keymap_sensors), triggers_per_rotation, 20)

struct rgb {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

static const struct rgb agent_colors[] = {
    {0, 180, 255}, {30, 90, 255}, {125, 45, 255}, {230, 35, 210}, {255, 105, 10}, {80, 220, 35},
};
static const struct rgb cyan = {0, 190, 255};
static const struct rgb blue = {25, 80, 255};
static const struct rgb white = {255, 255, 255};
static const struct rgb green = {25, 255, 65};
static const struct rgb red = {255, 15, 8};
static const struct rgb purple = {165, 35, 255};
static const struct rgb orange = {255, 90, 5};
static const struct rgb magenta = {255, 15, 115};

enum led_action {
    ACTION_TASK_1,
    ACTION_TASK_2,
    ACTION_TASK_3,
    ACTION_TASK_4,
    ACTION_TASK_5,
    ACTION_TASK_6,
    ACTION_FAST,
    ACTION_APPROVE,
    ACTION_REJECT,
    ACTION_CONTINUE,
    ACTION_PUSH_TO_TALK,
    ACTION_SUBMIT,
};

/* Indexed by keymap position, in the order of the matrix transform. */
static const enum led_action position_actions[] = {
    ACTION_TASK_1,       ACTION_TASK_2,       ACTION_TASK_3, ACTION_TASK_4, ACTION_TASK_5,
    ACTION_TASK_6,       ACTION_FAST,         ACTION_APPROVE, ACTION_REJECT, ACTION_CONTINUE,
    ACTION_PUSH_TO_TALK, ACTION_PUSH_TO_TALK, ACTION_SUBMIT,
};

enum led_effect {
    EFFECT_NONE,
    EFFECT_AGENT_PULSE,
    EFFECT_SUBMIT_SWEEP,
    EFFECT_APPROVE_CONFIRM,
    EFFECT_REJECT_FLASH,
    EFFECT_CONTINUE_WAVE,
    EFFECT_FAST_COMET,
    EFFECT_ENCODER_GAUGE,
};

struct led_state {
    enum led_effect effect;
    uint32_t effect_started_ms;
    uint32_t effect_duration_ms;
    struct rgb effect_color;
    uint8_t selected_agent;
    uint8_t reasoning_level;
    bool approve_held;
    uint32_t approve_started_ms;
    uint8_t push_to_talk_count;
    int32_t encoder_remainder_deg;
};

static const struct device *const strip = DEVICE_DT_GET(STRIP_NODE);
static struct led_rgb pixels[LED_COUNT];

/* Guards `state`: key and encoder events arrive on the system work queue,
 * frames are rendered on the low priority one. */
static K_MUTEX_DEFINE(state_lock);
static struct led_state state = {
    .selected_agent = 0xFF,
    .reasoning_level = 2,
};
static bool running;

static void set_pixel(int32_t index, struct rgb color, uint8_t intensity) {
    while (index < 0) {
        index += LED_COUNT;
    }
    uint32_t scale = (uint32_t)intensity * CONFIG_MACROPAD_LEDS_BRIGHTNESS;
    struct led_rgb *pixel = &pixels[index % LED_COUNT];
    pixel->r = (uint8_t)((color.r * scale) / (255 * 100));
    pixel->g = (uint8_t)((color.g * scale) / (255 * 100));
    pixel->b = (uint8_t)((color.b * scale) / (255 * 100));
}

static void fill(struct rgb color, uint8_t intensity) {
    for (int32_t i = 0; i < LED_COUNT; i++) {
        set_pixel(i, color, intensity);
    }
}

static void start_effect(struct led_state *s, enum led_effect effect, uint32_t now,
                         uint32_t duration, struct rgb color) {
    s->effect = effect;
    s->effect_started_ms = now;
    s->effect_duration_ms = duration;
    s->effect_color = color;
}

static void render_idle(const struct led_state *s, uint32_t now) {
    bool has_agent = s->selected_agent < ARRAY_SIZE(agent_colors);
    struct rgb accent = has_agent ? agent_colors[s->selected_agent] : cyan;
    fill(accent, has_agent ? 24 : 10);

    int32_t head = (int32_t)((now / 90) % LED_COUNT);
    set_pixel(head, accent, 210);
    set_pixel(head - 1, accent, 105);
    set_pixel(head - 2, accent, 48);
}

static void render_approve_progress(uint32_t elapsed) {
    uint32_t lit = 1 + ((LED_COUNT - 1) * elapsed) / LONG_PRESS_MS;
    for (uint32_t i = 0; i < lit; i++) {
        set_pixel(i, green, 215);
    }
    if (lit < LED_COUNT) {
        set_pixel(lit, orange, 100);
    }
}

static void render_push_to_talk(uint32_t now) {
    uint32_t phase = now % 1200;
    uint32_t triangle = phase < 600 ? phase : 1200 - phase;
    fill(magenta, (uint8_t)(55 + (triangle * 145) / 600));
    set_pixel((int32_t)((now / 65) % LED_COUNT), white, 185);
}

static void render_effect(const struct led_state *s, uint32_t now) {
    uint32_t elapsed = now - s->effect_started_ms;
    uint32_t duration = s->effect_duration_ms == 0 ? 1 : s->effect_duration_ms;

    switch (s->effect) {
    case EFFECT_AGENT_PULSE:
    case EFFECT_APPROVE_CONFIRM:
        fill(s->effect_color, (uint8_t)(35 + ((duration - elapsed) * 220) / duration));
        break;
    case EFFECT_SUBMIT_SWEEP: {
        int32_t head = (int32_t)((LED_COUNT * elapsed) / duration);
        set_pixel(head, white, 255);
        set_pixel(head - 1, blue, 210);
        set_pixel(head - 2, blue, 100);
        set_pixel(head - 3, blue, 40);
        break;
    }
    case EFFECT_REJECT_FLASH:
        if ((elapsed / 130) % 2 == 0) {
            fill(red, 235);
        }
        break;
    case EFFECT_CONTINUE_WAVE: {
        int32_t head = (int32_t)((elapsed / 38) % LED_COUNT);
        set_pixel(head, purple, 245);
        set_pixel(head - 1, purple, 115);
        set_pixel(head + LED_COUNT / 2, purple, 245);
        set_pixel(head + LED_COUNT / 2 - 1, purple, 115);
        break;
    }
    case EFFECT_FAST_COMET: {
        int32_t head = (int32_t)((elapsed / 28) % LED_COUNT);
        for (int32_t tail = 0; tail < 5; tail++) {
            uint8_t intensity = (uint8_t)(235 - tail * 45);
            set_pixel(head - tail, orange, intensity);
            set_pixel(head + LED_COUNT / 2 - tail, orange, intensity);
        }
        break;
    }
    case EFFECT_ENCODER_GAUGE: {
        uint32_t lit = ((s->reasoning_level + 1) * LED_COUNT + REASONING_LEVEL_MAX) /
                       (REASONING_LEVEL_MAX + 1);
        for (uint32_t i = 0; i < lit && i < LED_COUNT; i++) {
            set_pixel(i, i < (LED_COUNT / 2) ? cyan : purple, 190);
        }
        break;
    }
    case EFFECT_NONE:
        break;
    }
}

static void render_frame(uint32_t now) {
    memset(pixels, 0, sizeof(pixels));

    k_mutex_lock(&state_lock, K_FOREVER);

    if (state.approve_held) {
        uint32_t elapsed = now - state.approve_started_ms;
        if (elapsed >= LONG_PRESS_MS) {
            state.approve_held = false;
            start_effect(&state, EFFECT_APPROVE_CONFIRM, now, 650, green);
        } else {
            render_approve_progress(elapsed);
            k_mutex_unlock(&state_lock);
            return;
        }
    }

    if (state.push_to_talk_count > 0) {
        render_push_to_talk(now);
    } else {
        if (state.effect != EFFECT_NONE &&
            now - state.effect_started_ms >= state.effect_duration_ms) {
            state.effect = EFFECT_NONE;
        }
        if (state.effect == EFFECT_NONE) {
            render_idle(&state, now);
        } else {
            render_effect(&state, now);
        }
    }

    k_mutex_unlock(&state_lock);
}

static void frame_work_handler(struct k_work *work) {
    if (running) {
        render_frame(k_uptime_get_32());
    } else {
        memset(pixels, 0, sizeof(pixels));
    }

    int err = led_strip_update_rgb(strip, pixels, LED_COUNT);
    if (err < 0) {
        LOG_ERR("Failed to update the LED strip (%d)", err);
    }
}

static K_WORK_DEFINE(frame_work, frame_work_handler);

static void frame_timer_handler(struct k_timer *timer) {
    k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &frame_work);
}

static K_TIMER_DEFINE(frame_timer, frame_timer_handler, NULL);

/* The strip is fed from USB 5 V, so it only runs while USB power is present. */
static void update_power(void) {
    bool want = zmk_activity_get_state() != ZMK_ACTIVITY_SLEEP;
#if IS_ENABLED(CONFIG_ZMK_USB)
    want = want && zmk_usb_is_powered();
#endif

    if (want == running) {
        return;
    }
    running = want;

    if (running) {
        k_timer_start(&frame_timer, K_NO_WAIT, K_MSEC(CONFIG_MACROPAD_LEDS_FRAME_MS));
    } else {
        k_timer_stop(&frame_timer);
        /* One last frame to blank the strip. */
        k_work_submit_to_queue(zmk_workqueue_lowprio_work_q(), &frame_work);
    }
}

static void on_key(uint32_t position, bool pressed, uint32_t now) {
    if (position >= ARRAY_SIZE(position_actions)) {
        return;
    }
    enum led_action action = position_actions[position];

    k_mutex_lock(&state_lock, K_FOREVER);

    if (!pressed) {
        if (action == ACTION_APPROVE) {
            state.approve_held = false;
        } else if (action == ACTION_PUSH_TO_TALK && state.push_to_talk_count > 0) {
            state.push_to_talk_count--;
        }
        k_mutex_unlock(&state_lock);
        return;
    }

    switch (action) {
    case ACTION_TASK_1:
    case ACTION_TASK_2:
    case ACTION_TASK_3:
    case ACTION_TASK_4:
    case ACTION_TASK_5:
    case ACTION_TASK_6:
        state.selected_agent = (uint8_t)action;
        start_effect(&state, EFFECT_AGENT_PULSE, now, 450, agent_colors[action]);
        break;
    case ACTION_SUBMIT:
        start_effect(&state, EFFECT_SUBMIT_SWEEP, now, 700, blue);
        break;
    case ACTION_APPROVE:
        state.approve_held = true;
        state.approve_started_ms = now;
        break;
    case ACTION_REJECT:
        start_effect(&state, EFFECT_REJECT_FLASH, now, 520, red);
        break;
    case ACTION_CONTINUE:
        start_effect(&state, EFFECT_CONTINUE_WAVE, now, 900, purple);
        break;
    case ACTION_FAST:
        start_effect(&state, EFFECT_FAST_COMET, now, 2400, orange);
        break;
    case ACTION_PUSH_TO_TALK:
        state.push_to_talk_count++;
        break;
    }

    k_mutex_unlock(&state_lock);
}

static void on_encoder(const struct zmk_sensor_event *ev, uint32_t now) {
    int32_t steps = 0;

    k_mutex_lock(&state_lock, K_FOREVER);

    for (size_t i = 0; i < ev->channel_data_size; i++) {
        if (ev->channel_data[i].channel != SENSOR_CHAN_ROTATION) {
            continue;
        }
        /* Same detent maths as ZMK's sensor rotate behaviors. */
        int32_t trigger_degrees = 360 / TRIGGERS_PER_ROTATION;
        state.encoder_remainder_deg += ev->channel_data[i].value.val1;
        steps += state.encoder_remainder_deg / trigger_degrees;
        state.encoder_remainder_deg %= trigger_degrees;
    }

    if (steps != 0) {
        int32_t level = state.reasoning_level + steps;
        state.reasoning_level = (uint8_t)CLAMP(level, 0, REASONING_LEVEL_MAX);
        start_effect(&state, EFFECT_ENCODER_GAUGE, now, 900, cyan);
    }

    k_mutex_unlock(&state_lock);
}

static int macropad_leds_listener(const zmk_event_t *eh) {
    const struct zmk_position_state_changed *pos_ev = as_zmk_position_state_changed(eh);
    if (pos_ev) {
        on_key(pos_ev->position, pos_ev->state, k_uptime_get_32());
        return ZMK_EV_EVENT_BUBBLE;
    }

    const struct zmk_sensor_event *sensor_ev = as_zmk_sensor_event(eh);
    if (sensor_ev) {
        on_encoder(sensor_ev, k_uptime_get_32());
        return ZMK_EV_EVENT_BUBBLE;
    }

    /* Activity or USB power changed. */
    update_power();
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(macropad_leds, macropad_leds_listener);
ZMK_SUBSCRIPTION(macropad_leds, zmk_position_state_changed);
ZMK_SUBSCRIPTION(macropad_leds, zmk_sensor_event);
ZMK_SUBSCRIPTION(macropad_leds, zmk_activity_state_changed);
#if IS_ENABLED(CONFIG_ZMK_USB)
ZMK_SUBSCRIPTION(macropad_leds, zmk_usb_conn_state_changed);
#endif

static int macropad_leds_init(void) {
    if (!device_is_ready(strip)) {
        LOG_ERR("LED strip device is not ready");
        return -ENODEV;
    }

    update_power();
    return 0;
}

SYS_INIT(macropad_leds_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
