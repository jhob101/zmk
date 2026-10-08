/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_joystick_dpad

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>

#include <zmk/behavior.h>
#include <zmk/hid.h>
#include <zmk/endpoints.h>
#include <dt-bindings/zmk/joystick.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

// D-pad keys as joystick axes, ported from js_update_axes() in the uConsole
// QMK firmware. The four directions are separate keys rather than a rocker,
// so two opposing ones can be down at once. The most recently pressed of the
// pair wins, and releasing it falls back to the other if that is still held.

#define AXIS_X 0
#define AXIS_Y 1

struct axis_state {
    bool negative_held; // left, or up
    bool positive_held; // right, or down
    int8_t last;        // -1 or +1: whichever of the pair was pressed last
};

static struct axis_state axes[ZMK_HID_JOYSTICK_NUM_AXES];

static int8_t axis_value(const struct axis_state *state) {
    if (state->negative_held && state->positive_held) {
        return state->last * ZMK_HID_JOYSTICK_AXIS_MAX;
    }
    if (state->negative_held) {
        return -ZMK_HID_JOYSTICK_AXIS_MAX;
    }
    if (state->positive_held) {
        return ZMK_HID_JOYSTICK_AXIS_MAX;
    }
    return 0;
}

static int update_direction(uint32_t direction, bool pressed) {
    uint8_t axis;
    bool positive;

    switch (direction) {
    case JD_LEFT:
        axis = AXIS_X;
        positive = false;
        break;
    case JD_RIGHT:
        axis = AXIS_X;
        positive = true;
        break;
    case JD_UP:
        axis = AXIS_Y;
        positive = false;
        break;
    case JD_DOWN:
        axis = AXIS_Y;
        positive = true;
        break;
    default:
        return -EINVAL;
    }

    struct axis_state *state = &axes[axis];

    if (positive) {
        state->positive_held = pressed;
    } else {
        state->negative_held = pressed;
    }
    if (pressed) {
        state->last = positive ? 1 : -1;
    }

    zmk_hid_joystick_axis_set(axis, axis_value(state));
    return zmk_endpoints_send_joystick_report();
}

static int behavior_joystick_dpad_init(const struct device *dev) { return 0; }

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    LOG_DBG("position %d joystick direction %d", event.position, binding->param1);
    return update_direction(binding->param1, true);
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    LOG_DBG("position %d joystick direction %d", event.position, binding->param1);
    return update_direction(binding->param1, false);
}

static const struct behavior_driver_api behavior_joystick_dpad_driver_api = {
    .binding_pressed = on_keymap_binding_pressed, .binding_released = on_keymap_binding_released};

#define JD_INST(n)                                                                                 \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_joystick_dpad_init, NULL, NULL, NULL, POST_KERNEL,         \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                                   \
                            &behavior_joystick_dpad_driver_api);

DT_INST_FOREACH_STATUS_OKAY(JD_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
