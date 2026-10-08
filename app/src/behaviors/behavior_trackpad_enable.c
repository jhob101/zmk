/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_trackpad_enable

#include <zephyr/device.h>
#include <zephyr/sys/atomic.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>
#include <zmk/behavior.h>
#include <zmk/trackpad.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

// Lets the keymap switch the trackpad off, for the keyboard lock. Written from
// key events here and read from the trackpad polling loop in main.c, on a
// different thread. Kept outside the devicetree guard so main.c links even
// with no instance in the keymap. Not persisted: always on after power-up.

static atomic_t trackpad_enabled = ATOMIC_INIT(1);

bool zmk_trackpad_enabled(void) { return atomic_get(&trackpad_enabled) != 0; }

void zmk_trackpad_set_enabled(bool enabled) { atomic_set(&trackpad_enabled, enabled ? 1 : 0); }

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int on_trackpad_enable_pressed(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    zmk_trackpad_set_enabled(binding->param1 != 0);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_trackpad_enable_released(struct zmk_behavior_binding *binding,
                                       struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_trackpad_enable_driver_api = {
    .binding_pressed = on_trackpad_enable_pressed,
    .binding_released = on_trackpad_enable_released,
};

static int behavior_trackpad_enable_init(const struct device *dev) { return 0; }

#define TPE_INST(n)                                                                                \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_trackpad_enable_init, NULL, NULL, NULL, POST_KERNEL,       \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                                   \
                            &behavior_trackpad_enable_driver_api);

DT_INST_FOREACH_STATUS_OKAY(TPE_INST)

#endif
