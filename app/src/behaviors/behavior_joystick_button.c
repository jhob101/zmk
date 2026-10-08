/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_joystick_button

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>

#include <zmk/behavior.h>
#include <zmk/hid.h>
#include <zmk/endpoints.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int behavior_joystick_button_init(const struct device *dev) { return 0; }

static int on_keymap_binding_pressed(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    LOG_DBG("position %d joystick button %d", event.position, binding->param1);

    int err = zmk_hid_joystick_button_press(binding->param1);
    if (err) {
        return err;
    }
    return zmk_endpoints_send_joystick_report();
}

static int on_keymap_binding_released(struct zmk_behavior_binding *binding,
                                      struct zmk_behavior_binding_event event) {
    LOG_DBG("position %d joystick button %d", event.position, binding->param1);

    int err = zmk_hid_joystick_button_release(binding->param1);
    if (err) {
        return err;
    }
    return zmk_endpoints_send_joystick_report();
}

static const struct behavior_driver_api behavior_joystick_button_driver_api = {
    .binding_pressed = on_keymap_binding_pressed, .binding_released = on_keymap_binding_released};

#define JB_INST(n)                                                                                 \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_joystick_button_init, NULL, NULL, NULL, POST_KERNEL,       \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                                   \
                            &behavior_joystick_button_driver_api);

DT_INST_FOREACH_STATUS_OKAY(JB_INST)

#endif /* DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT) */
