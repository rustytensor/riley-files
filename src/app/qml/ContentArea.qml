// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

import QtQuick

// Where a folder's files are shown. Empty until browsing arrives.
Rectangle {
    color: Theme.content

    Column {
        anchors.centerIn: parent
        spacing: Theme.spacing / 2

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "No folder open"
            color: Theme.textMuted
            font.pixelSize: Theme.fontLarge
        }

        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            text: "Folders you open will appear here."
            color: Theme.textFaint
            font.pixelSize: Theme.fontNormal
        }
    }
}
