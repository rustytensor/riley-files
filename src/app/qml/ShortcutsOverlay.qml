// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

pragma ComponentBehavior: Bound

import QtQuick

// Every action that has a key, menu by menu, as the actions registry lists them (F1).
// Escape, F1 or a click outside closes it.
Rectangle {
    id: overlay
    objectName: "shortcuts"

    required property Browser browser

    visible: browser.shortcutsShown
    color: Theme.scrim

    Accessible.role: Accessible.Dialog
    Accessible.name: qsTr("Keyboard Shortcuts")

    // A click outside the card closes it, as Escape does; it is not a control.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        Accessible.ignored: true
        onClicked: overlay.browser.shortcutsShown = false
    }

    Rectangle {
        id: card
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.spacing * 4, 760)
        height: Math.min(parent.height - Theme.spacing * 4, content.height + title.height + Theme.spacing * 3)
        radius: Theme.radius
        color: Theme.card
        border.color: Theme.cardBorder
        border.width: 1

        // Clicks on the card stay on it.
        MouseArea {
            anchors.fill: parent
            Accessible.ignored: true
        }

        Text {
            id: title
            x: Theme.spacing * 1.5
            y: Theme.spacing
            text: qsTr("Keyboard Shortcuts")
            color: Theme.text
            font.pixelSize: Theme.fontLarge
            font.weight: Font.DemiBold
        }

        Flickable {
            anchors {
                top: title.bottom; topMargin: Theme.spacing
                left: parent.left; leftMargin: Theme.spacing * 1.5
                right: parent.right; rightMargin: Theme.spacing * 1.5
                bottom: parent.bottom; bottomMargin: Theme.spacing
            }
            clip: true
            contentHeight: content.height
            boundsBehavior: Flickable.StopAtBounds

            Flow {
                id: content
                width: parent.width
                spacing: Theme.spacing

                Repeater {
                    model: overlay.browser.actions.shortcutList

                    delegate: Item {
                        id: row
                        objectName: "shortcutRow"

                        required property var modelData
                        required property int index
                        readonly property string menu: modelData.menu
                        readonly property string label: modelData.label
                        readonly property string shortcut: modelData.shortcut
                        readonly property string others: modelData.others

                        width: (content.width - Theme.spacing) / 2
                        height: Theme.menuRowHeight

                        Text {
                            anchors { left: parent.left; verticalCenter: parent.verticalCenter }
                            text: row.menu + " › " + row.label
                            color: Theme.text
                            font.pixelSize: Theme.fontNormal
                        }
                        Text {
                            anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                            text: row.others.length > 0 ? qsTr("%1   or %2").arg(row.shortcut).arg(row.others) : row.shortcut
                            color: Theme.textMuted
                            font.pixelSize: Theme.fontSmall
                        }
                    }
                }
            }
        }
    }
}
