// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

import QtQuick

// The column on the left. Places, drives and bookmarks arrive in a later milestone; until then
// it shows a quiet heading so the layout reads as finished.
Rectangle {
    implicitWidth: Theme.sidebarWidth
    color: Theme.sidebar

    Text {
        anchors { top: parent.top; left: parent.left; margins: Theme.spacing * 1.5 }
        text: "Places"
        color: Theme.textFaint
        font.pixelSize: Theme.fontSmall
        font.weight: Font.DemiBold
        font.capitalization: Font.AllUppercase
        font.letterSpacing: 1
    }

    Rectangle {
        anchors { top: parent.top; bottom: parent.bottom; right: parent.right }
        width: 1
        color: Theme.divider
    }
}
