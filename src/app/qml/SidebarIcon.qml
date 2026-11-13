// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

import QtQuick

// A small drawn icon for a sidebar row: a house, a bin, or a folder for the rest. Drawn rather
// than taken from a font, so it looks the same on every machine.
Item {
    id: icon

    property string icon: "folder"
    property color tint: Theme.textMuted
    property int size: 16

    width: size
    height: size

    // Home: a roof and a body.
    Rectangle {
        visible: icon.icon === "home"
        x: icon.size * 0.5 - width / 2; y: icon.size * 0.08
        width: icon.size * 0.62; height: width
        rotation: 45
        color: icon.tint
    }
    Rectangle {
        visible: icon.icon === "home"
        x: icon.size * 0.18; y: icon.size * 0.42
        width: icon.size * 0.64; height: icon.size * 0.5
        radius: 1
        color: icon.tint
    }

    // Trash: a lid and a bin.
    Rectangle {
        visible: icon.icon === "trash"
        x: icon.size * 0.12; y: icon.size * 0.12
        width: icon.size * 0.76; height: Math.max(2, icon.size * 0.12)
        radius: 1
        color: icon.tint
    }
    Rectangle {
        visible: icon.icon === "trash"
        x: icon.size * 0.22; y: icon.size * 0.3
        width: icon.size * 0.56; height: icon.size * 0.62
        radius: 2
        color: icon.tint
    }

    // Every other place: a folder, its tab a little lighter.
    Rectangle {
        visible: icon.icon !== "home" && icon.icon !== "trash"
        x: 0; y: icon.size * 0.14
        width: icon.size * 0.45; height: icon.size * 0.25
        radius: 2
        color: icon.tint
        opacity: 0.75
    }
    Rectangle {
        visible: icon.icon !== "home" && icon.icon !== "trash"
        x: 0; y: icon.size * 0.24
        width: icon.size; height: icon.size * 0.64
        radius: 2
        color: icon.tint
    }
}
