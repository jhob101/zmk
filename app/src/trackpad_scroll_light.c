/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/led.h>
#include <zephyr/init.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>

#include <zmk/trackpad.h>

#if IS_ENABLED(CONFIG_ZMK_TRACKPAD_LIGHT_HOST_SLEEP)
#include <dt-bindings/zmk/hid_usage.h>
#include <zmk/event_manager.h>
#include <zmk/usb.h>
#include <zmk/events/hid_indicators_changed.h>
#include <zmk/events/usb_conn_state_changed.h>
#endif

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
// while the trackpad is scrolling, 0% otherwise. On top of that the light
// can be switched off from the keymap (zmk,behavior-trackpad-light, saved),
// and the keyboard lock holds it off while locked. The host can also hold it
// off by switching on the Kana LED (CONFIG_ZMK_TRACKPAD_LIGHT_HOST_SLEEP), which
// is how a sleep service puts it out with the screen. Whether 100% means lit or
// dark depends on how the board wires the light, so
// CONFIG_ZMK_TRACKPAD_SCROLL_LIGHT_INVERT swaps the two levels.
//
// It cannot be combined with CONFIG_ZMK_INDICATOR_CAPSLOCK, which polls and
// would overwrite the pin every 50 ms.

BUILD_ASSERT(DT_HAS_CHOSEN(zmk_indicator_capslock),
             "CONFIG_ZMK_TRACKPAD_SCROLL_LIGHT is enabled but no zmk_indicator_capslock chosen "
             "node found");

static const struct device *const light_dev = DEVICE_DT_GET(DT_CHOSEN(zmk_indicator_capslock));

// The light is lit only when all four allow it: the user has it switched
// on (saved), the keyboard is not locked, the host is not asleep, and the
// trackpad is not scrolling. Locked and asleep are kept apart so waking the
// host while locked leaves the light off.
#define LEVEL_LIT (IS_ENABLED(CONFIG_ZMK_TRACKPAD_SCROLL_LIGHT_INVERT) ? 100 : 0)
#define LEVEL_DARK (IS_ENABLED(CONFIG_ZMK_TRACKPAD_SCROLL_LIGHT_INVERT) ? 0 : 100)

static bool light_enabled = true;
static bool light_locked;
static bool light_host_asleep;
static bool light_scrolling;

static void light_refresh(void) {
    if (!device_is_ready(light_dev)) {
        return;
    }

    const bool lit = light_enabled && !light_locked && !light_host_asleep && !light_scrolling;
    int rc = led_set_brightness(light_dev, 0, lit ? LEVEL_LIT : LEVEL_DARK);
    if (rc != 0) {
        LOG_ERR("Failed to set the trackpad light: %d", rc);
    }
}

#if IS_ENABLED(CONFIG_SETTINGS)
static int light_settings_load_cb(const char *name, size_t len, settings_read_cb read_cb,
                                  void *cb_arg, void *param) {
    const char *next;
    if (settings_name_steq(name, "light", &next) && !next) {
        uint8_t saved;
        if (len != sizeof(saved)) {
            return -EINVAL;
        }

        int rc = read_cb(cb_arg, &saved, sizeof(saved));
        if (rc >= 0) {
            light_enabled = saved != 0;
        }
        return MIN(rc, 0);
    }
    return -ENOENT;
}

static void light_save_work_handler(struct k_work *work) {
    uint8_t saved = light_enabled ? 1 : 0;
    settings_save_one("trackpad/light", &saved, sizeof(saved));
}

static struct k_work_delayable light_save_work;
#endif

void zmk_trackpad_scroll_light_update(bool scrolling) {
    light_scrolling = scrolling;
    light_refresh();
}

bool zmk_trackpad_light_enabled(void) { return light_enabled; }

void zmk_trackpad_light_set_enabled(bool on) {
    if (light_enabled == on) {
        return;
    }
    light_enabled = on;
    light_refresh();
#if IS_ENABLED(CONFIG_SETTINGS)
    k_work_reschedule(&light_save_work, K_MSEC(CONFIG_ZMK_SETTINGS_SAVE_DEBOUNCE));
#endif
}

void zmk_trackpad_light_set_suspended(bool suspended) {
    light_locked = suspended;
    light_refresh();
}

#if IS_ENABLED(CONFIG_ZMK_TRACKPAD_LIGHT_HOST_SLEEP)
// HID LED usages start at 1, bit 0 of the report is Num Lock.
#define KANA_BIT BIT(HID_USAGE_LED_KANA - 1)

static void light_set_host_asleep(bool asleep) {
    if (light_host_asleep == asleep) {
        return;
    }
    light_host_asleep = asleep;
    light_refresh();
}

static int light_host_listener(const zmk_event_t *eh) {
    const struct zmk_hid_indicators_changed *ind = as_zmk_hid_indicators_changed(eh);
    if (ind) {
        light_set_host_asleep(ind->indicators & KANA_BIT);
        return ZMK_EV_EVENT_BUBBLE;
    }

    // A rebooted or unplugged host won't switch Kana off again, so don't wait
    // for it.
    if (as_zmk_usb_conn_state_changed(eh)) {
        const enum usb_dc_status_code status = zmk_usb_get_status();
        if (status == USB_DC_RESET || status == USB_DC_DISCONNECTED) {
            light_set_host_asleep(false);
        }
    }
    return ZMK_EV_EVENT_BUBBLE;
}

ZMK_LISTENER(trackpad_light_host, light_host_listener);
ZMK_SUBSCRIPTION(trackpad_light_host, zmk_hid_indicators_changed);
ZMK_SUBSCRIPTION(trackpad_light_host, zmk_usb_conn_state_changed);
#endif

static int zmk_trackpad_scroll_light_init(void) {
#if IS_ENABLED(CONFIG_SETTINGS)
    settings_subsys_init();
    int rc = settings_load_subtree_direct("trackpad", light_settings_load_cb, NULL);
    if (rc != 0) {
        LOG_ERR("Failed to load trackpad light settings: %d", rc);
    }
    k_work_init_delayable(&light_save_work, light_save_work_handler);
#endif
    light_refresh();
    return 0;
}

SYS_INIT(zmk_trackpad_scroll_light_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
