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
            right: parent.right; rightMargin: Theme.spacing * 1.5
            baseline: title.baseline
        }
        text: bar.browser.folder
        color: Theme.textFaint
        font.pixelSize: Theme.fontSmall
        elide: Text.ElideMiddle
    }

    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 1
        color: Theme.divider
    }
}
