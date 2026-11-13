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

    // Rows: the one under the cursor, and the one under the mouse.
    property color cursorRow: "#223144"
    property color hoverRow: "#1c2029"
    // The cursor in a column the keys are not in (columns view).
    property color restingRow: "#1f242c"
    property color treeGuide: "#343a45"

    // Editing a name: the field and its selected text.
    property color field: "#101216"
    property color selection: "#33507a"

    // File operations: the progress strip, messages, and the question about a taken name.
    property color strip: "#1b1e24"
    property color track: "#2a2f38"
    property color button: "#262a32"
    property color buttonHover: "#2f343e"
    property color notice: "#2b2620"
    property color noticeText: "#e3c58c"
    property color scrim: "#a0000000"
    property color card: "#1f232a"
    property color cardBorder: "#323844"

    // File icons: folders in the accent's family, files by what they hold.
    property color folder: "#6f93c4"
    property color folderTab: "#5a7aa6"
    property var categoryColors: ({
        image: "#b48ead",
        audio: "#d08770",
        video: "#c57b8f",
        text: "#a3acb9",
        code: "#8fbf9f",
        document: "#d6b46a",
        archive: "#a88f6a",
        file: "#7d8592"
    })
    function categoryColor(category) {
        return categoryColors[category] ?? categoryColors.file
    }

    property int headerHeight: 52
    property int rowHeight: 30
    property int iconSize: 18
    property int indent: 18
    property int sizeColumn: 96
    property int dateColumn: 150
    // Grid tiles and the columns view's columns.
    property int tileWidth: 112
    property int tileHeight: 104
    property int tileIcon: 44
    property int columnWidth: 240
    property int sidebarWidth: 220
    property int detailsWidth: 264
    property int spacing: 12
    property int radius: 8
    property int stripHeight: 58
    property int buttonHeight: 32
    property int dialogWidth: 460

    property int fontSmall: 12
    property int fontNormal: 14
    property int fontLarge: 17
    // Text files in the preview.
    property string monoFamily: "monospace"
}
