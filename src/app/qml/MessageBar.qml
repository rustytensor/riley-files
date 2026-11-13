// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

import QtQuick

// A short message along the bottom: what could not be done, and why. It goes by itself, with
// Escape, or with its close mark.
Rectangle {
    id: bar
    objectName: "message"

    required property FileOps ops

    visible: ops.message.length > 0
    implicitHeight: visible ? Math.max(Theme.rowHeight + 8, text.implicitHeight + 16) : 0
    color: Theme.notice

    Text {
        id: text
        objectName: "messageText"
        anchors {
            left: parent.left; leftMargin: Theme.spacing * 1.5
            right: close.left; rightMargin: Theme.spacing
            verticalCenter: parent.verticalCenter
        }
        text: bar.ops.message
        wrapMode: Text.Wrap
        color: Theme.noticeText
        font.pixelSize: Theme.fontNormal
    }

    Text {
        id: close
        anchors { right: parent.right; rightMargin: Theme.spacing * 1.5; verticalCenter: parent.verticalCenter }
        text: "×"
        color: closeMouse.containsMouse ? Theme.text : Theme.noticeText
        font.pixelSize: Theme.fontLarge

        MouseArea {
            id: closeMouse
            anchors { fill: parent; margins: -6 }
            hoverEnabled: true
            onClicked: bar.ops.dismissMessage()
        }
    }
}
