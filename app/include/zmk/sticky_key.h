/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

/**
 * Releases, shortly afterwards, every armed (tapped and waiting) sticky key
 * that has release-on-mouse set, as a key press would. Called when a mouse
 * click or a trackpad scroll ends, so a one-shot modifier applies to that
 * click or scroll and then clears.
 */
void zmk_sticky_key_release_armed_soon(void);
