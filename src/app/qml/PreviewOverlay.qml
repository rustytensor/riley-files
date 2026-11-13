// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

pragma ComponentBehavior: Bound

import QtQuick

// The preview (Space): over the window, the file under the cursor. A picture is shown whole,
// fitted to the room there is and never made larger; a text file shows its first lines.
// Up, Down, Left and Right go to the other files; Space or Escape closes it.
Rectangle {
    id: overlay
    objectName: "preview"

    required property Browser browser
    readonly property Preview preview: browser.preview

    visible: preview.open
    color: Theme.scrim

    Accessible.role: Accessible.Dialog
    Accessible.name: qsTr("Preview of “%1”").arg(preview.name)

    // Clicks and the wheel stay here; a click outside the card closes it.
    // A click outside the card closes it, as Escape does; it is not a control.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        Accessible.ignored: true
        onWheel: wheel => wheel.accepted = true
        onClicked: overlay.browser.handleKey(Qt.Key_Escape, Qt.NoModifier, "")
    }

    Rectangle {
        id: card
        anchors { fill: parent; margins: Theme.spacing * 3 }
        radius: Theme.radius * 1.5
        color: Theme.card
        border.color: Theme.cardBorder
        border.width: 1

        MouseArea { anchors.fill: parent; acceptedButtons: Qt.AllButtons; Accessible.ignored: true }

        Text {
            id: title
            objectName: "previewName"
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: Theme.spacing * 1.5 }
            text: overlay.preview.name
            elide: Text.ElideMiddle
            color: Theme.text
            font.pixelSize: Theme.fontLarge
            font.weight: Font.DemiBold
        }

        Text {
            id: hint
            anchors { left: parent.left; right: parent.right; bottom: parent.bottom; margins: Theme.spacing }
            horizontalAlignment: Text.AlignHCenter
            text: qsTr("↑ ↓ ← → other files · Space or Escape closes")
            color: Theme.textFaint
            font.pixelSize: Theme.fontSmall
        }

        Item {
            id: room
            anchors {
                left: parent.left; right: parent.right; top: title.bottom; bottom: hint.top
                margins: Theme.spacing * 1.5
            }
            clip: true

            Image {
                id: picture
                objectName: "previewImage"
                anchors.centerIn: parent
                // Its own size, made smaller only when it does not fit.
                width: Math.min(implicitWidth, room.width)
                height: Math.min(implicitHeight, room.height)
                source: overlay.preview.kind === Preview.Image ? overlay.preview.imageSource : ""
                asynchronous: true
                cache: false
                fillMode: Image.PreserveAspectFit
                smooth: true
                visible: overlay.preview.kind === Preview.Image
            }

            Flickable {
                anchors.fill: parent
                visible: overlay.preview.kind === Preview.Text
                contentWidth: lines.implicitWidth
                contentHeight: lines.implicitHeight
                boundsBehavior: Flickable.StopAtBounds
                clip: true

                Text {
                    id: lines
                    objectName: "previewText"
                    text: overlay.preview.kind === Preview.Text ? overlay.preview.text : ""
                    textFormat: Text.PlainText
                    color: Theme.text
                    font.family: Theme.monoFamily
                    font.pixelSize: Theme.fontSmall + 1
                }
            }

            Column {
                anchors.centerIn: parent
                spacing: Theme.spacing
                visible: overlay.preview.kind === Preview.Other

                FileIcon {
                    anchors.horizontalCenter: parent.horizontalCenter
                    size: 72
                    category: overlay.preview.category
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: overlay.preview.category === "folder" ? qsTr("A folder") : qsTr("No preview for this kind of file")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontNormal
                }
            }
        }
    }
}
