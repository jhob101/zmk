/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_trackpad_lift

#include <zephyr/device.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/settings/settings.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>
#include <zmk/behavior.h>
#include <zmk/trackpad.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

// Trackpad lift detection, switched from the keymap and saved.
//
// With it on, the sensor drops motion as a finger leaves, so the pointer
// does not jump when a thumb is lifted. It relies on fixed thresholds in the
// sensor, and on some trackpads those reject a bare finger completely. One
// build therefore cannot have it simply on or simply off for everyone: it
// starts off (CONFIG_ZMK_TRACKPAD_LIFT_DETECTION_ON_START), a key switches
// it, and the choice is kept in settings, which survive both power cycles
// and firmware updates.
//
// The state and its init live outside the devicetree guard so that the
// sensor is always told what to do, even with no instance in the keymap.

static bool lift_detection = IS_ENABLED(CONFIG_ZMK_TRACKPAD_LIFT_DETECTION_ON_START);

static void lift_detection_apply(void) {
    a320_set_ofn_engine(lift_detection ? CONFIG_INPUT_A320_OFN_ENGINE : 0x00);
}

#if IS_ENABLED(CONFIG_SETTINGS)
static int lift_settings_load_cb(const char *name, size_t len, settings_read_cb read_cb,
                                 void *cb_arg, void *param) {
    const char *next;
    if (settings_name_steq(name, "lift", &next) && !next) {
        uint8_t saved;
        if (len != sizeof(saved)) {
            return -EINVAL;
        }

        int rc = read_cb(cb_arg, &saved, sizeof(saved));
        if (rc >= 0) {
            lift_detection = saved != 0;
        }
        return MIN(rc, 0);
    }
    return -ENOENT;
}

static void lift_save_work_handler(struct k_work *work) {
    uint8_t saved = lift_detection ? 1 : 0;
    settings_save_one("trackpad/lift", &saved, sizeof(saved));
}

static struct k_work_delayable lift_save_work;
#endif

bool zmk_trackpad_lift_detection(void) { return lift_detection; }

void zmk_trackpad_set_lift_detection(bool on) {
    if (lift_detection == on) {
        return;
    }
    lift_detection = on;
    lift_detection_apply();
#if IS_ENABLED(CONFIG_SETTINGS)
    k_work_reschedule(&lift_save_work, K_MSEC(CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE));
#endif
}

static int zmk_trackpad_lift_init(void) {
#if IS_ENABLED(CONFIG_SETTINGS)
    settings_subsys_init();
    int rc = settings_load_subtree_direct("trackpad", lift_settings_load_cb, NULL);
    if (rc != 0) {
        LOG_ERR("Failed to load trackpad settings: %d", rc);
    }
    k_work_init_delayable(&lift_save_work, lift_save_work_handler);
#endif
    lift_detection_apply();
    return 0;
}

SYS_INIT(zmk_trackpad_lift_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

static int on_trackpad_lift_pressed(struct zmk_behavior_binding *binding,
                                    struct zmk_behavior_binding_event event) {
    zmk_trackpad_set_lift_detection(binding->param1 != 0);
    return ZMK_BEHAVIOR_OPAQUE;
}

static int on_trackpad_lift_released(struct zmk_behavior_binding *binding,
                                     struct zmk_behavior_binding_event event) {
    return ZMK_BEHAVIOR_OPAQUE;
}

static const struct behavior_driver_api behavior_trackpad_lift_driver_api = {
    .binding_pressed = on_trackpad_lift_pressed,
    .binding_released = on_trackpad_lift_released,
};

static int behavior_trackpad_lift_init(const struct device *dev) { return 0; }

#define TPL_INST(n)                                                                                \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_trackpad_lift_init, NULL, NULL, NULL, POST_KERNEL,         \
                            CONFIG_KERNEL_INIT_PRIORITY_DEFAULT,                                   \
                            &behavior_trackpad_lift_driver_api);

DT_INST_FOREACH_STATUS_OKAY(TPL_INST)

#endif
