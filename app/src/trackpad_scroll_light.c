/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led.h>
#include <zephyr/init.h>
#include <zephyr/logging/log.h>

#include <zmk/trackpad.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

// The trackpad light follows scroll mode.
//
// On the bb9900 the light under the trackpad is on the pin the board template
// calls the CapsLock indicator (the zmk_indicator_capslock chosen node). The
// vendor firmware drove that pin to 100% while CapsLock was on, and CapsLock
// was also what put its trackpad into scroll mode, so the light changed with
// scroll mode as a side effect. fix9900 switched the CapsLock indicator off,
// which left the pin undriven.
//
// This drives the same pin the same way, but from the real scroll state: 100%
// while the trackpad is scrolling, 0% otherwise. Whether 100% means lit or
// dark depends on how the board wires the light, so
// CONFIG_ZMK_TRACKPAD_SCROLL_LIGHT_INVERT swaps the two levels.
//
// It cannot be combined with CONFIG_ZMK_INDICATOR_CAPSLOCK, which polls and
// would overwrite the pin every 50 ms.

BUILD_ASSERT(DT_HAS_CHOSEN(zmk_indicator_capslock),
             "CONFIG_ZMK_TRACKPAD_SCROLL_LIGHT is enabled but no zmk_indicator_capslock chosen "
             "node found");

static const struct device *const light_dev = DEVICE_DT_GET(DT_CHOSEN(zmk_indicator_capslock));

#define LEVEL_SCROLLING (IS_ENABLED(CONFIG_ZMK_TRACKPAD_SCROLL_LIGHT_INVERT) ? 0 : 100)
#define LEVEL_POINTER (IS_ENABLED(CONFIG_ZMK_TRACKPAD_SCROLL_LIGHT_INVERT) ? 100 : 0)

void zmk_trackpad_scroll_light_update(bool scrolling) {
    if (!device_is_ready(light_dev)) {
        return;
    }

    int rc = led_set_brightness(light_dev, 0, scrolling ? LEVEL_SCROLLING : LEVEL_POINTER);
    if (rc != 0) {
        LOG_ERR("Failed to set the trackpad light: %d", rc);
    }
}

static int zmk_trackpad_scroll_light_init(void) {
    zmk_trackpad_scroll_light_update(false);
    return 0;
}

SYS_INIT(zmk_trackpad_scroll_light_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
