// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

pragma ComponentBehavior: Bound

import QtQuick

// The window's tabs, under the header, shown only when there are two or more. Each says its
// folder's name and has a close button; a click makes it the active tab, the middle button
// closes it. The tabs decide all of it.
Rectangle {
    id: strip
    objectName: "tabBar"

    required property Tabs tabs

    visible: strip.tabs.count > 1
    implicitHeight: visible ? Theme.rowHeight + Theme.spaceMedium * 2 : 0
    color: Theme.header

    Accessible.role: Accessible.PageTabList
    Accessible.name: qsTr("Tabs")

    Row {
        id: row
        anchors {
            fill: parent
            leftMargin: Theme.spacing; rightMargin: Theme.spacing
            topMargin: Theme.spaceMedium; bottomMargin: Theme.spaceMedium
        }
        spacing: Theme.spaceSmall

        Repeater {
            model: strip.tabs

            delegate: Rectangle {
                id: tab

                required property int index
                required property string title
                required property bool active

                objectName: "tab" + index
                width: Math.max(80, Math.min(240, (row.width - row.spacing * (strip.tabs.count - 1)) / Math.max(1, strip.tabs.count)))
                height: row.height
                radius: Theme.radius
                color: tab.active ? Theme.content : (mouse.containsMouse ? Theme.hoverRow : Theme.none)
                border.color: tab.active ? Theme.tableBorder : Theme.none
                border.width: 1
                Behavior on color { ColorAnimation { duration: Theme.quick } }

                Accessible.role: Accessible.PageTab
                Accessible.name: tab.title
                Accessible.selected: tab.active
                Accessible.onPressAction: strip.tabs.activate(tab.index)

                Text {
                    anchors {
                        left: parent.left; leftMargin: Theme.spacing
                        right: closeButton.left; rightMargin: Theme.spaceSmall
                        verticalCenter: parent.verticalCenter
                    }
                    text: tab.title
                    elide: Text.ElideMiddle
                    color: tab.active ? Theme.text : Theme.textMuted
                    font.pixelSize: Theme.fontNormal
                    font.weight: tab.active ? Font.DemiBold : Font.Normal
                }

                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: true
                    acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                    onClicked: event => {
                        if (event.button === Qt.MiddleButton)
                            strip.tabs.close(tab.index)
                        else
                            strip.tabs.activate(tab.index)
                    }
                }

                // Over the tab's own mouse area, so its click closes rather than activates.
                Rectangle {
                    id: closeButton
                    objectName: "tabClose" + tab.index
                    anchors { right: parent.right; rightMargin: Theme.spaceSmall; verticalCenter: parent.verticalCenter }
                    width: 22
                    height: 22
                    radius: Theme.radiusSmall
                    color: closeMouse.containsMouse ? Theme.buttonHover : Theme.none
                    opacity: tab.active || mouse.containsMouse || closeMouse.containsMouse ? 1 : 0.6

                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Close “%1”").arg(tab.title)
                    Accessible.onPressAction: strip.tabs.close(tab.index)

                    Text {
                        anchors.centerIn: parent
                        text: "×"
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontLarge
                    }

                    MouseArea {
                        id: closeMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: strip.tabs.close(tab.index)
                    }
                }
            }
        }
    }

    // The line between the tabs and the files.
    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 1
        color: Theme.divider
    }
}
