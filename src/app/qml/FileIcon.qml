// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

import QtQuick

// A small drawn icon: a folder, or a page tinted by what the file holds. Drawn rather than
// taken from a font, so it looks the same on every machine.
Item {
    id: icon

    property string category: "file"
    property int size: Theme.iconSize

    width: size
    height: size

    // Folder: a tab and a body.
    Rectangle {
        visible: icon.category === "folder"
        x: 0; y: icon.size * 0.12
        width: icon.size * 0.45; height: icon.size * 0.25
        radius: 2
        color: Theme.folderTab
    }
    Rectangle {
        visible: icon.category === "folder"
        x: 0; y: icon.size * 0.22
        width: icon.size; height: icon.size * 0.68
        radius: 3
        color: Theme.folder
    }

    // File: a page with a darker band along its top.
    Rectangle {
        id: page
        visible: icon.category !== "folder"
        x: icon.size * 0.14
        width: icon.size * 0.72; height: icon.size
        radius: 3
        color: Theme.categoryColor(icon.category)

        Rectangle {
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: icon.size * 0.14 }
            height: Math.max(2, icon.size * 0.1)
            radius: 1
            color: Theme.window
            opacity: 0.35
        }
    }
}
