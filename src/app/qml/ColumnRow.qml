// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

import QtQuick

// One row of a column in the columns view: icon and name, and a mark for folders.
Rectangle {
    id: row

    required property Browser browser
    required property int index
    required property string name
    required property bool isDir
    required property bool hidden
    required property string category
    required property string path

    // Whether the keys are in this row's column.
    property bool active: false

    readonly property bool current: ListView.isCurrentItem

    height: Theme.rowHeight
    color: current ? (active ? Theme.cursorRow : Theme.restingRow)
                   : (mouse.containsMouse ? Theme.hoverRow : "transparent")

    Rectangle {
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
        width: 2
        visible: row.current && row.active
        color: Theme.accent
    }

    FileIcon {
        id: icon
        x: Theme.spacing
        anchors.verticalCenter: parent.verticalCenter
        category: row.category
        customSource: row.browser.customIconSource(row.path, row.browser.iconsRevision)
        opacity: row.hidden ? 0.55 : 1
    }

    Text {
        anchors {
            left: icon.right; leftMargin: Theme.spacing * 0.75
            right: mark.left; rightMargin: Theme.spacing / 2
            verticalCenter: parent.verticalCenter
        }
        text: row.name
        elide: Text.ElideMiddle
        color: row.hidden ? Theme.textMuted : Theme.text
        font.pixelSize: Theme.fontNormal
    }

    Text {
        id: mark
        anchors { right: parent.right; rightMargin: Theme.spacing; verticalCenter: parent.verticalCenter }
        text: row.isDir ? "›" : ""
        color: Theme.textMuted
        font.pixelSize: Theme.fontNormal
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
    }
}
