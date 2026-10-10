/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_scroll_hold

#include <zephyr/device.h>
#include <zephyr/sys/atomic.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>
#include <zmk/behavior.h>
#include <zmk/behavior_queue.h>
#include <zmk/trackpad.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

// Hold-to-scroll for the trackpad, ported from the uConsole QMK firmware's
// Select key: while the key is held, trackpad motion scrolls instead of moving
// the cursor. If the key is released without the trackpad having moved, the
// bound behavior is tapped instead, so the key keeps a function of its own.
//
// With the `toggle` property the key is a scroll switch instead: each press
// turns scrolling on or off, and it stays that way until the next press.
//
// The flags are written from key events here and read from the trackpad
// polling loop in main.c, which runs on a different thread. They live outside
// the devicetree guard so main.c links even with no instance in the keymap.

static atomic_t scroll_held = ATOMIC_INIT(0);
static atomic_t scroll_used = ATOMIC_INIT(0);
// Kept apart from scroll_held so that holding and releasing a hold-to-scroll
// key while the switch is on leaves the switch on.
static atomic_t scroll_toggled = ATOMIC_INIT(0);

bool zmk_trackpad_scroll_held(void) {
    return atomic_get(&scroll_held) != 0 || atomic_get(&scroll_toggled) != 0;
}

void zmk_trackpad_scroll_mark_used(void) { atomic_set(&scroll_used, 1); }

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

struct behavior_scroll_hold_config {
    struct zmk_behavior_binding tap_binding;
    uint32_t tap_ms;
    // With pass-through the bound behavior follows the key exactly: pressed
    // when the key goes down, released when it comes up, with scrolling on
    // top. It responds immediately and can be held, at the cost of also
    // being reported during a scroll. Without it, the bound behavior is only
    // tapped on release, and only if the trackpad did not move.
    bool pass_through;
    // A scroll switch: each press turns scrolling on or off. The bound
    // behavior is not tapped. With pass-through as well it is still pressed
    // and released along with the key.
    bool toggle;
};

static int on_scroll_hold_pressed(struct zmk_behavior_binding *binding,
                                  struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_scroll_hold_config *cfg = dev->config;

    if (cfg->toggle) {
        atomic_set(&scroll_toggled, atomic_get(&scroll_toggled) ? 0 : 1);
    } else {
        atomic_set(&scroll_used, 0);
        atomic_set(&scroll_held, 1);
    }

    if (cfg->pass_through) {
        behavior_keymap_binding_pressed((struct zmk_behavior_binding *)&cfg->tap_binding, event);
    }
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_scroll_hold_released(struct zmk_behavior_binding *binding,
                                   struct zmk_behavior_binding_event event) {
    const struct device *dev = zmk_behavior_get_binding(binding->behavior_dev);
    const struct behavior_scroll_hold_config *cfg = dev->config;

    if (!cfg->toggle) {
        atomic_set(&scroll_held, 0);
    }

    if (cfg->pass_through) {
        behavior_keymap_binding_released((struct zmk_behavior_binding *)&cfg->tap_binding, event);
    } else if (!cfg->toggle && atomic_get(&scroll_used) == 0) {
        zmk_behavior_queue_add(event.position, cfg->tap_binding, true, cfg->tap_ms);
        zmk_behavior_queue_add(event.position, cfg->tap_binding, false, 0);
    }

    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_scroll_hold_driver_api = {
    .binding_pressed = on_scroll_hold_pressed,
    .binding_released = on_scroll_hold_released,
};

static int behavior_scroll_hold_init(const struct device *dev) { return 0; }

#define _TRANSFORM_ENTRY(idx, node)                                                                \
    {                                                                                              \
        .behavior_dev = DEVICE_DT_NAME(DT_INST_PHANDLE_BY_IDX(node, bindings, idx)),               \
        .param1 = COND_CODE_0(DT_INST_PHA_HAS_CELL_AT_IDX(node, bindings, idx, param1), (0),       \
                              (DT_INST_PHA_BY_IDX(node, bindings, idx, param1))),                  \
        .param2 = COND_CODE_0(DT_INST_PHA_HAS_CELL_AT_IDX(node, bindings, idx, param2), (0),       \
                              (DT_INST_PHA_BY_IDX(node, bindings, idx, param2))),                  \
    }

#define SH_INST(n)                                                                                 \
    static const struct behavior_scroll_hold_config behavior_scroll_hold_config_##n = {           \
        .tap_binding = _TRANSFORM_ENTRY(0, n),                                                     \
        .tap_ms = DT_INST_PROP(n, tap_ms),                                                         \
        .pass_through = DT_INST_PROP(n, pass_through),                                             \
        .toggle = DT_INST_PROP(n, toggle),                                                         \
    };                                                                                             \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_scroll_hold_init, NULL, NULL,                              \
                            &behavior_scroll_hold_config_##n, POST_KERNEL,                         \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_scroll_hold_driver_api);

DT_INST_FOREACH_STATUS_OKAY(SH_INST)

#endif
