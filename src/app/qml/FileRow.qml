// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

import QtQuick

// One row of the list view.
Rectangle {
    id: row

    required property Browser browser
    required property int index
    required property string name
    required property bool isDir
    required property bool hidden
    required property string sizeText
    required property string modifiedText
    required property string category
    required property int depth
    required property bool open
    required property string lines

    readonly property bool current: ListView.isCurrentItem
    // How far the row is pushed in by the tree; 0 with the tree off.
    readonly property int indent: browser.tree ? depth * Theme.indent : 0
    readonly property int textLeft: Theme.spacing * 1.5 + indent + (browser.tree ? Theme.indent : 0)

    height: Theme.rowHeight
    color: current ? Theme.cursorRow : (mouse.containsMouse ? Theme.hoverRow : "transparent")

    // The cursor's left edge, in the one colour that marks what is chosen.
    Rectangle {
        anchors { left: parent.left; top: parent.top; bottom: parent.bottom }
        width: 2
        visible: row.current
        color: Theme.accent
    }

    TreeGuide {
        visible: row.browser.tree && row.depth > 0
        x: Theme.spacing * 1.5
        height: parent.height
        lines: row.lines
    }

    // The tree's open and close mark for folders.
    Text {
        id: disclosure
        visible: row.browser.tree && row.isDir
        x: Theme.spacing * 1.5 + row.indent
        width: Theme.indent
        height: parent.height
        horizontalAlignment: Text.AlignHCenter
        verticalAlignment: Text.AlignVCenter
        text: row.open ? "▾" : "▸"
        color: Theme.textMuted
        font.pixelSize: Theme.fontSmall

        MouseArea {
            anchors.fill: parent
            onClicked: {
                row.browser.cursor = row.index
                row.browser.model.toggleOpen(row.index)
            }
        }
    }

    FileIcon {
        id: icon
        x: row.textLeft
        anchors.verticalCenter: parent.verticalCenter
        category: row.category
        opacity: row.hidden ? 0.55 : 1
    }

    Text {
        anchors {
            left: icon.right; leftMargin: Theme.spacing
            right: size.left; rightMargin: Theme.spacing
            verticalCenter: parent.verticalCenter
        }
        text: row.name
        // While the name is edited, the field over it shows it instead.
        visible: !(row.current && row.browser.ops.editing)
        elide: Text.ElideMiddle
        color: row.hidden ? Theme.textMuted : Theme.text
        font.pixelSize: Theme.fontNormal
    }

    Text {
        id: size
        anchors { right: date.left; rightMargin: Theme.spacing; verticalCenter: parent.verticalCenter }
        width: Theme.sizeColumn
        horizontalAlignment: Text.AlignRight
        text: row.sizeText
        color: Theme.textMuted
        font.pixelSize: Theme.fontSmall
    }

    Text {
        id: date
        anchors { right: parent.right; rightMargin: Theme.spacing * 1.5; verticalCenter: parent.verticalCenter }
        width: Theme.dateColumn
        text: row.modifiedText
        color: Theme.textMuted
        font.pixelSize: Theme.fontSmall
    }

    MouseArea {
        id: mouse
        anchors { fill: parent; leftMargin: disclosure.visible ? disclosure.x + disclosure.width : 0 }
        hoverEnabled: true
        onClicked: row.browser.cursor = row.index
        onDoubleClicked: row.browser.activate(row.index)
    }
}
