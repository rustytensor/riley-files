// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

pragma ComponentBehavior: Bound

import QtQuick

// A yes-or-no question before something is done (moving to the trash, when the Preferences
// ask for it). Return says yes, Escape no; the browser decides what follows.
Rectangle {
    id: dialog
    objectName: "question"

    required property Browser browser

    visible: browser.question.length > 0
    color: Theme.scrim

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: wheel => wheel.accepted = true
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(Theme.dialogWidth, dialog.width - Theme.spacing * 4)
        height: content.implicitHeight + Theme.spacing * 4
        radius: Theme.radius * 1.5
        color: Theme.card
        border.color: Theme.cardBorder
        border.width: 1

        Column {
            id: content
            anchors { left: parent.left; right: parent.right; top: parent.top; margins: Theme.spacing * 2 }
            spacing: Theme.spacing * 1.5

            Text {
                objectName: "questionText"
                width: parent.width
                text: dialog.browser.question
                wrapMode: Text.Wrap
                color: Theme.text
                font.pixelSize: Theme.fontLarge
                font.weight: Font.DemiBold
            }

            Row {
                anchors.right: parent.right
                spacing: 8

                Repeater {
                    model: [
                        { name: "noButton", label: "Cancel  (Esc)", yes: false },
                        { name: "yesButton", label: "Yes  (Return)", yes: true }
                    ]

                    delegate: Rectangle {
                        id: button

                        required property var modelData

                        objectName: modelData.name
                        width: label.implicitWidth + Theme.spacing * 3
                        height: Theme.buttonHeight
                        radius: Theme.radius
                        color: modelData.yes ? Theme.accentSoft : (mouse.containsMouse ? Theme.buttonHover : Theme.button)
                        border.color: modelData.yes ? Theme.accent : "transparent"
                        border.width: 1

                        Text {
                            id: label
                            anchors.centerIn: parent
                            text: button.modelData.label
                            color: Theme.text
                            font.pixelSize: Theme.fontNormal
                        }

                        MouseArea {
                            id: mouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: dialog.browser.answer(button.modelData.yes)
                        }
                    }
                }
            }
        }
    }
}
