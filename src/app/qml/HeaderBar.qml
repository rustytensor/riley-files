// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

import QtQuick

// The strip along the top. It will hold the path and the view buttons; for now, the title.
Rectangle {
    implicitHeight: Theme.headerHeight
    color: Theme.header

    Text {
        anchors { left: parent.left; leftMargin: Theme.spacing * 1.5; verticalCenter: parent.verticalCenter }
        text: "Files"
        color: Theme.text
        font.pixelSize: Theme.fontLarge
        font.weight: Font.DemiBold
    }

    Rectangle {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 1
        color: Theme.divider
    }
}
