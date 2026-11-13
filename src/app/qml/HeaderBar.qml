// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

import QtQuick

// The strip along the top: back, forward and up, then the folder's name and where it is.
Rectangle {
    id: bar

    required property Browser browser

    implicitHeight: Theme.headerHeight
    color: Theme.header

    component NavButton: Rectangle {
        id: button

        property string glyph
        property bool active: true
        signal clicked()

        width: 30
        height: 30
        radius: Theme.radius
        color: mouse.containsMouse && button.active ? Theme.hoverRow : "transparent"

        Text {
            anchors.centerIn: parent
            text: button.glyph
            color: button.active ? Theme.text : Theme.textFaint
            font.pixelSize: Theme.fontLarge
        }

        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            enabled: button.active
            onClicked: button.clicked()
        }
    }

    Row {
        id: nav
        anchors { left: parent.left; leftMargin: Theme.spacing; verticalCenter: parent.verticalCenter }
        spacing: 2

        NavButton { glyph: "‹"; active: bar.browser.canGoBack; onClicked: bar.browser.goBack() }
        NavButton { glyph: "›"; active: bar.browser.canGoForward; onClicked: bar.browser.goForward() }
        NavButton { glyph: "↑"; active: bar.browser.canGoUp; onClicked: bar.browser.goUp() }
    }

    Text {
        id: title
        visible: !bar.browser.locationEditing
        anchors { left: nav.right; leftMargin: Theme.spacing; verticalCenter: parent.verticalCenter }
        text: bar.browser.folderName.length > 0 ? bar.browser.folderName : "Files"
        color: Theme.text
        font.pixelSize: Theme.fontLarge
        font.weight: Font.DemiBold
    }

    Text {
        anchors {
            left: title.right; leftMargin: Theme.spacing
            right: tools.left; rightMargin: Theme.spacing
            baseline: title.baseline
        }
        visible: !bar.browser.locationEditing
        text: bar.browser.folder
        color: Theme.textFaint
        font.pixelSize: Theme.fontSmall
        elide: Text.ElideMiddle
    }

    // The folder's path, typed (Ctrl+L). Return opens it, Escape leaves the folder as it is.
    Rectangle {
        id: locationBox
        visible: bar.browser.locationEditing
        anchors {
            left: nav.right; leftMargin: Theme.spacing
            right: tools.left; rightMargin: Theme.spacing
            verticalCenter: parent.verticalCenter
        }
        height: Theme.rowHeight
        radius: 4
        color: Theme.field
        border.color: Theme.accent
        border.width: 1

        onVisibleChanged: {
            if (visible) {
                location.text = bar.browser.folder
                location.selectAll()
                location.forceActiveFocus()
            }
        }

        TextInput {
            id: location
            objectName: "location"

            anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
            verticalAlignment: TextInput.AlignVCenter
            clip: true
            color: Theme.text
            selectionColor: Theme.selection
            selectedTextColor: Theme.text
            font.pixelSize: Theme.fontNormal
            selectByMouse: true

            Keys.onReturnPressed: event => {
                bar.browser.openTypedLocation(text)
                event.accepted = true
            }
            Keys.onEnterPressed: event => {
                bar.browser.openTypedLocation(text)
                event.accepted = true
            }
            Keys.onEscapePressed: event => {
                bar.browser.cancelLocation()
                event.accepted = true
            }
            onActiveFocusChanged: if (!activeFocus && locationBox.visible) bar.browser.cancelLocation()
        }
    }

    // On the right: the details pane's switch (Ctrl+I), lit while the pane is shown.
    Row {
        id: tools
        anchors { right: parent.right; rightMargin: Theme.spacing; verticalCenter: parent.verticalCenter }
        spacing: 2

        Rectangle {
            id: detailsButton
            objectName: "detailsButton"

            readonly property bool on: bar.browser.detailsShown

            width: 30
            height: 30
            radius: Theme.radius
            color: on ? Theme.accentSoft : (detailsMouse.containsMouse ? Theme.hoverRow : "transparent")

            Rectangle {
                anchors.centerIn: parent
                width: 16
                height: 16
                radius: 8
                color: "transparent"
                border.width: 1.5
                border.color: detailsButton.on ? Theme.accent : Theme.textMuted

                Text {
                    anchors.centerIn: parent
                    text: "i"
                    color: detailsButton.on ? Theme.accent : Theme.textMuted
                    font.pixelSize: Theme.fontSmall
                    font.weight: Font.Bold
                }
            }

            MouseArea {
                id: detailsMouse
                anchors.fill: parent
                hoverEnabled: true
                onClicked: bar.browser.detailsShown = !bar.browser.detailsShown
            }
        }
    }

    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 1
        color: Theme.divider
    }
}
