/*
 * Copyright (c) 2026 thoughtfix
 *
 * SPDX-License-Identifier: MIT
 */

#define DT_DRV_COMPAT zmk_behavior_vol_shift

#include <zephyr/device.h>
#include <drivers/behavior.h>
#include <zephyr/logging/log.h>
#include <zmk/behavior.h>
#include <zmk/hid.h>
#include <zmk/endpoints.h>
#include <zmk/event_manager.h>
#include <zmk/events/keycode_state_changed.h>
#include <dt-bindings/zmk/hid_usage_pages.h>
#include <dt-bindings/zmk/hid_usage.h>
#include <dt-bindings/zmk/keys.h>
#include <dt-bindings/zmk/modifiers.h>

LOG_MODULE_DECLARE(zmk, CONFIG_ZMK_LOG_LEVEL);

#if DT_HAS_COMPAT_STATUS_OKAY(DT_DRV_COMPAT)

// Volume/Mute fix (see NOTES.md): Speaker key alone = Volume Down;
// Shift+Speaker = Volume Up, matching ClusterM's factory-spec
// do_the_key() behavior. A plain zmk,behavior-mod-morph can pick which
// *code* to send based on Shift state, but Consumer-page reports have no
// modifier byte, so Shift's own physical key keeps reporting held the
// whole time regardless, which stops strict WM-level keybind matching
// (e.g. labwc's unmodified XF86_AudioRaiseVolume bind) from ever firing.
// This behavior hides Shift from the host for the duration of the press.
//
// Two things here matter once Shift can be a one-shot (sticky) key:
//
// - The volume key is raised as an ordinary key event rather than written
//   straight into the HID report. Sticky keys only notice key events, so
//   without this a Shift held for Shift+Speaker was never seen as "used",
//   stayed armed after it was let go, and turned the next plain Speaker
//   press into Volume Up as well.
// - Shift is hidden with the modifier mask, not by unregistering and
//   re-registering it. The mask leaves the modifier's own bookkeeping
//   alone, so it stays right whether Shift is released before, during or
//   after the volume key, including by a sticky key letting go mid-press.

struct behavior_vol_shift_data {
    bool masked;
    bool sent_vol_up;
};

static struct behavior_vol_shift_data vol_shift_data = {0};

static int on_vol_shift_pressed(struct zmk_behavior_binding *binding,
                                 struct zmk_behavior_binding_event event) {
    zmk_mod_flags_t shift_mods = zmk_hid_get_explicit_mods() & (MOD_LSFT | MOD_RSFT);

    vol_shift_data.sent_vol_up = shift_mods != 0;
    vol_shift_data.masked = shift_mods != 0;

    if (shift_mods) {
        // tell the host Shift is up before the volume key goes down
        zmk_hid_masked_modifiers_set(shift_mods);
        zmk_endpoints_send_report(HID_USAGE_KEY);
    }

    return ZMK_EVENT_RAISE(zmk_keycode_state_changed_from_encoded(
        vol_shift_data.sent_vol_up ? C_VOLUME_UP : C_VOLUME_DOWN, true, event.timestamp));
}

static int on_vol_shift_released(struct zmk_behavior_binding *binding,
                                  struct zmk_behavior_binding_event event) {
    int ret = ZMK_EVENT_RAISE(zmk_keycode_state_changed_from_encoded(
        vol_shift_data.sent_vol_up ? C_VOLUME_UP : C_VOLUME_DOWN, false, event.timestamp));

    if (vol_shift_data.masked) {
        // Shift shows again only if it is still actually held
        zmk_hid_masked_modifiers_clear();
        zmk_endpoints_send_report(HID_USAGE_KEY);
        vol_shift_data.masked = false;
    }

    return ret;
}

static const struct behavior_driver_api behavior_vol_shift_driver_api = {
    .binding_pressed = on_vol_shift_pressed,
    .binding_released = on_vol_shift_released,
};

static int behavior_vol_shift_init(const struct device *dev) { return 0; }

#define VS_INST(n)                                                                                \
    BEHAVIOR_DT_INST_DEFINE(n, behavior_vol_shift_init, NULL, NULL, NULL, POST_KERNEL,            \
                             CONFIG_KERNEL_INIT_PRIORITY_DEFAULT, &behavior_vol_shift_driver_api);

DT_INST_FOREACH_STATUS_OKAY(VS_INST)

#endif
