// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

pragma ComponentBehavior: Bound

import QtQuick

// The menu bar: File, Edit, View, Go and Help, as the actions say. A title opens its menu; with
// a menu open, moving onto another title opens that one instead.
Rectangle {
    id: bar
    objectName: "menuBar"

    required property Actions actions

    // Where a menu's title is, so its menu opens below it.
    function titleX(index) {
        const title = titles.itemAt(index)
        return title ? title.x + row.x : 0
    }

    implicitHeight: Theme.menuBarHeight
    color: Theme.header

    Row {
        id: row
        x: Theme.spacing / 2
        height: parent.height

        Repeater {
            id: titles
            model: bar.actions.menuTitles

            delegate: Rectangle {
                id: titleItem
                objectName: "menuTitle"

                required property string modelData
                required property int index
                readonly property string title: modelData
                readonly property bool open: bar.actions.barMenu === index

                width: label.implicitWidth + Theme.spacing * 1.5
                height: row.height
                color: open ? Theme.accentSoft : (mouse.containsMouse ? Theme.hoverRow : "transparent")

                Text {
                    id: label
                    anchors.centerIn: parent
                    text: titleItem.title
                    color: titleItem.open ? Theme.text : Theme.textMuted
                    font.pixelSize: Theme.fontNormal
                }

                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onPressed: bar.actions.openBarMenu(titleItem.index)
                    function moved() {
                        const at = mapToItem(null, mouseX, mouseY)
                        Qt.callLater(bar.actions.hoverBarMenu, titleItem.index, at.x, at.y)
                    }
                    onContainsMouseChanged: if (containsMouse) moved()
                    onPositionChanged: moved()
                }
            }
        }
    }

    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 1
        color: Theme.divider
    }
}
