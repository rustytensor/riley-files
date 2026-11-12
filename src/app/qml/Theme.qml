// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

pragma Singleton

import QtQuick

// Every colour, size and spacing the window uses, by name. Restyling Riley Files means
// changing this one object; no other file holds a literal colour.
QtObject {
    // Surfaces, from the back of the window to the front.
    property color window: "#16181d"
    property color sidebar: "#1b1e24"
    property color header: "#1d2027"
    property color content: "#16181d"
    property color divider: "#262a32"

    // Text.
    property color text: "#d9dce3"
    property color textMuted: "#8a909c"
    property color textFaint: "#5c626e"

    // The one colour that marks what is active or chosen.
    property color accent: "#7aa2d6"
    property color accentSoft: "#223144"

    property int headerHeight: 52
    property int sidebarWidth: 220
    property int spacing: 12
    property int radius: 8

    property int fontSmall: 12
    property int fontNormal: 14
    property int fontLarge: 17
}
