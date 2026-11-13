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
        text: bar.browser.folder
        color: Theme.textFaint
        font.pixelSize: Theme.fontSmall
        elide: Text.ElideMiddle
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
