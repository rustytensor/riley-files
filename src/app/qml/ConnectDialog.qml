// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

pragma ComponentBehavior: Bound

import QtQuick

// Connect to Server: an address typed in (sftp://, smb://, ftp://, dav:// or davs://). Return
// connects, Escape leaves it; the browser checks the address and runs the connection.
Rectangle {
    id: dialog
    objectName: "connect"

    required property Browser browser

    visible: browser.connectShown
    color: Theme.scrim

    Accessible.role: Accessible.Dialog
    Accessible.name: qsTr("Connect to Server")

    onVisibleChanged: {
        if (visible) {
            field.text = ""
            field.forceActiveFocus()
        }
    }

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: wheel => wheel.accepted = true
        onClicked: dialog.browser.cancelConnect()
        // A click outside closes, as Escape does; it is not a control.
        Accessible.ignored: true
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(Theme.dialogWidth, dialog.width - Theme.spacing * 4)
        height: content.implicitHeight + Theme.spacing * 4
        radius: Theme.radius * 1.5
        color: Theme.card
        border.color: Theme.cardBorder
        border.width: 1

        // Clicks on the card stay on it.
        MouseArea { anchors.fill: parent; Accessible.ignored: true }

        Column {
            id: content
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: Theme.spacing * 2 }
            spacing: Theme.spacing * 1.25

            Text {
                width: parent.width
                text: qsTr("Connect to Server")
                color: Theme.text
                font.pixelSize: Theme.fontLarge
                font.weight: Font.DemiBold
            }

            Rectangle {
                width: parent.width
                height: Theme.rowHeight + 4
                radius: 4
                color: Theme.field
                border.color: Theme.accent
                border.width: 1

                TextInput {
                    id: field
                    objectName: "connectField"
                    Accessible.role: Accessible.EditableText
                    Accessible.name: qsTr("Server address")
                    Accessible.description: qsTr("sftp, smb, ftp, dav and davs addresses · Return connects · Esc cancels")

                    anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
                    verticalAlignment: TextInput.AlignVCenter
                    clip: true
                    color: Theme.text
                    selectionColor: Theme.selection
                    selectedTextColor: Theme.text
                    font.pixelSize: Theme.fontNormal
                    selectByMouse: true

                    Keys.onReturnPressed: event => {
                        dialog.browser.connectToServer(text)
                        event.accepted = true
                    }
                    Keys.onEnterPressed: event => {
                        dialog.browser.connectToServer(text)
                        event.accepted = true
                    }
                    Keys.onEscapePressed: event => {
                        dialog.browser.cancelConnect()
                        event.accepted = true
                    }
                }

                Text {
                    anchors { fill: field }
                    visible: field.text.length === 0
                    verticalAlignment: Text.AlignVCenter
                    text: qsTr("sftp://server/folder")
                    color: Theme.textFaint
                    font.pixelSize: Theme.fontNormal
                }
            }

            Text {
                width: parent.width
                text: qsTr("sftp, smb, ftp, dav and davs addresses · Return connects · Esc cancels")
                wrapMode: Text.Wrap
                color: Theme.textFaint
                font.pixelSize: Theme.fontSmall
            }
        }
    }
}
