/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdbool.h>

/**
 * Shared state between keymap behaviors (set from key events) and the trackpad
 * polling loop in main.c (which turns motion into cursor and scroll reports).
 */

/**
 * True while trackpad motion should scroll: a key bound to the scroll-hold
 * behavior is held down, or a scroll switch (its `toggle` option) is on.
 */
bool zmk_trackpad_scroll_held(void);

/**
 * Called by the trackpad loop when it sees motion while scroll is held, so
 * the key's release is treated as the end of a scroll rather than a tap.
 */
void zmk_trackpad_scroll_mark_used(void);

/**
 * Sets the trackpad light for the given scroll state. Only built with
 * CONFIG_ZMK_TRACKPAD_SCROLL_LIGHT.
 */
void zmk_trackpad_scroll_light_update(bool scrolling);

/**
 * Trackpad lift detection: the sensor stops reporting motion as a finger
 * leaves it, so the pointer does not jump. It does not suit every trackpad,
 * so it is switched from the keymap and the choice is saved.
 */
bool zmk_trackpad_lift_detection(void);
void zmk_trackpad_set_lift_detection(bool on);

/** Driver side of the above: what the A320's OFN_Engine register should hold. */
void a320_set_ofn_engine(uint8_t value);

/** False while the trackpad is switched off by the trackpad-enable behavior. */
bool zmk_trackpad_enabled(void);
void zmk_trackpad_set_enabled(bool enabled);
