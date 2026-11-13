// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

pragma ComponentBehavior: Bound

import QtQuick

// The open menu and its open submenus, each beside the one it came from. The actions keep
// which menu is open and which row is current; this only draws them and passes on the mouse.
Item {
    id: popups

    required property Actions actions
    // Where a menu of the bar opens: below its title.
    required property AppMenuBar menuBar
    // Where a context menu opened from the keyboard goes, in this item's coordinates.
    property point keyboardPoint: Qt.point(width / 3, height / 4)

    visible: actions.menuOpen

    Repeater {
        id: levels
        model: popups.actions.levels

        delegate: Rectangle {
            id: level
            objectName: "menuLevel" + index

            required property var modelData
            required property int index
            readonly property var items: modelData.items
            readonly property int current: modelData.current

            // The y of a row, for the submenu it opens.
            function rowY(row) {
                const item = rows.itemAt(row)
                return item ? item.y + column.y : 0
            }

            readonly property point wanted: {
                if (index > 0) {
                    const before = levels.itemAt(index - 1)
                    if (!before)
                        return Qt.point(0, 0)
                    const right = before.x + before.width - 4
                    // A submenu that does not fit on the right opens on the left.
                    const x = right + width > popups.width ? before.x - width + 4 : right
                    return Qt.point(x, before.y + before.rowY(modelData.parentRow) - column.y)
                }
                if (popups.actions.barMenu >= 0)
                    return Qt.point(popups.menuBar.titleX(popups.actions.barMenu), popups.menuBar.height)
                const at = popups.actions.menuPoint
                return at.x < 0 ? popups.keyboardPoint : Qt.point(at.x, at.y)
            }

            x: Math.max(0, Math.min(wanted.x, popups.width - width))
            y: Math.max(0, Math.min(wanted.y, popups.height - height))
            width: Theme.menuWidth
            height: column.height + Theme.menuPadding * 2
            radius: Theme.radius
            color: Theme.card
            border.color: Theme.cardBorder
            border.width: 1

            Accessible.role: Accessible.PopupMenu
            Accessible.name: qsTr("Menu")

            // The menu takes the mouse, so a click on it never reaches what is below. It is not a
            // control of its own.
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.AllButtons
                Accessible.ignored: true
            }

            Column {
                id: column
                y: Theme.menuPadding
                width: parent.width

                Repeater {
                    id: rows
                    model: level.items

                    delegate: Item {
                        id: entry
                        objectName: "menuItem"

                        required property var modelData
                        required property int index
                        readonly property int row: index
                        readonly property int levelIndex: level.index
                        readonly property string label: modelData.label
                        readonly property string shortcut: modelData.shortcut
                        readonly property bool usable: modelData.enabled
                        readonly property bool checkable: modelData.checkable
                        readonly property bool checked: modelData.checked
                        readonly property bool separator: modelData.separator
                        readonly property bool submenu: modelData.submenu
                        readonly property bool current: level.current === index

                        width: column.width
                        height: separator ? Theme.spacing : Theme.menuRowHeight

                        Accessible.role: separator ? Accessible.Separator : Accessible.MenuItem
                        Accessible.name: separator ? qsTr("Separator") : label
                        Accessible.description: shortcut
                        Accessible.checkable: checkable
                        Accessible.checked: checked
                        Accessible.selected: current
                        Accessible.onPressAction: if (!separator) popups.actions.choose(levelIndex, row)

                        Rectangle {
                            visible: entry.separator
                            anchors.verticalCenter: parent.verticalCenter
                            x: Theme.spacing
                            width: parent.width - Theme.spacing * 2
                            height: 1
                            color: Theme.divider
                        }

                        Rectangle {
                            visible: !entry.separator && entry.current
                            anchors { fill: parent; leftMargin: 4; rightMargin: 4 }
                            radius: 4
                            color: Theme.cursorRow
                        }

                        Text {
                            visible: entry.checked
                            x: Theme.spacing * 0.75
                            anchors.verticalCenter: parent.verticalCenter
                            text: "✓"
                            color: Theme.accent
                            font.pixelSize: Theme.fontSmall
                        }

                        Text {
                            visible: !entry.separator
                            anchors { left: parent.left; leftMargin: Theme.spacing * 2; verticalCenter: parent.verticalCenter }
                            text: entry.label
                            color: entry.usable ? Theme.text : Theme.textFaint
                            font.pixelSize: Theme.fontNormal
                        }

                        Text {
                            visible: !entry.separator
                            anchors { right: parent.right; rightMargin: Theme.spacing; verticalCenter: parent.verticalCenter }
                            text: entry.submenu ? "›" : entry.shortcut
                            color: Theme.textFaint
                            font.pixelSize: entry.submenu ? Theme.fontNormal : Theme.fontSmall
                        }

                        MouseArea {
                            anchors.fill: parent
                            enabled: !entry.separator
                            hoverEnabled: true
                            // Later, not while the menu is being drawn: a menu that opens under a
                            // resting mouse would otherwise change itself as it appears.
                            function moved() {
                                const at = mapToItem(null, mouseX, mouseY)
                                Qt.callLater(popups.actions.hover, entry.levelIndex, entry.row, at.x, at.y)
                            }
                            onContainsMouseChanged: if (containsMouse) moved()
                            onPositionChanged: moved()
                            onClicked: popups.actions.choose(entry.levelIndex, entry.row)
                        }
                    }
                }
            }
        }
    }
}
