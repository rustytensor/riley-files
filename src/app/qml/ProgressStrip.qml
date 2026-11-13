// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

import QtQuick

// Along the bottom while a long file operation runs: what it does, how far it is, and Stop.
Rectangle {
    id: strip
    objectName: "progress"

    required property FileOps ops

    visible: ops.showProgress
    implicitHeight: visible ? Theme.stripHeight : 0
    color: Theme.strip

    Rectangle {
        anchors { left: parent.left; right: parent.right; top: parent.top }
        height: 1
        color: Theme.divider
    }

    Column {
        anchors {
            left: parent.left; leftMargin: Theme.spacing * 1.5
            right: stop.left; rightMargin: Theme.spacing * 1.5
            verticalCenter: parent.verticalCenter
        }
        spacing: 6

        Item {
            width: parent.width
            height: title.implicitHeight

            Text {
                id: title
                anchors { left: parent.left; right: detail.left; rightMargin: Theme.spacing }
                text: strip.ops.progressTitle
                elide: Text.ElideMiddle
                color: Theme.text
                font.pixelSize: Theme.fontNormal
            }
            Text {
                id: detail
                objectName: "progressDetail"
                anchors.right: parent.right
                text: strip.ops.progressDetail
                color: Theme.textMuted
                font.pixelSize: Theme.fontSmall
            }
        }

        Rectangle {
            width: parent.width
            height: 4
            radius: 2
            color: Theme.track

            Rectangle {
                width: parent.width * strip.ops.progressFraction
                height: parent.height
                radius: 2
                color: Theme.accent
                Behavior on width { NumberAnimation { duration: 120 } }
            }
        }
    }

    Rectangle {
        id: stop
        objectName: "stopButton"

        anchors { right: parent.right; rightMargin: Theme.spacing * 1.5; verticalCenter: parent.verticalCenter }
        width: 76
        height: Theme.buttonHeight
        radius: Theme.radius
        color: stopMouse.containsMouse ? Theme.buttonHover : Theme.button
        opacity: strip.ops.stopping ? 0.5 : 1

        Text {
            anchors.centerIn: parent
            text: strip.ops.stopping ? "Stopping" : "Stop"
            color: Theme.text
            font.pixelSize: Theme.fontNormal
        }

        MouseArea {
            id: stopMouse
            anchors.fill: parent
            hoverEnabled: true
            enabled: !strip.ops.stopping
            onClicked: strip.ops.stop()
        }
    }
}
