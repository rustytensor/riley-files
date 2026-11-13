// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

pragma ComponentBehavior: Bound

import QtQuick

// Asked when a name is already taken where something is copied or moved. Nothing is ever
// overwritten without this answer, and Replace sends the old one to the trash. The keys answer
// it: Left and Right choose, Return answers, or S, K, R (and M for Merge, offered when a folder
// meets a folder) straight away; Escape skips.
Rectangle {
    id: dialog
    objectName: "clashDialog"

    required property FileOps ops

    visible: ops.asking
    color: Theme.scrim

    Accessible.role: Accessible.Dialog
    Accessible.name: ops.clashTitle

    // Clicks and the wheel stay here while the question is open; it is not a control.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onWheel: wheel => wheel.accepted = true
        Accessible.ignored: true
    }

    Rectangle {
        id: card
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
            spacing: Theme.spacing

            Text {
                objectName: "clashTitle"
                width: parent.width
                text: dialog.ops.clashTitle
                wrapMode: Text.Wrap
                color: Theme.text
                font.pixelSize: Theme.fontLarge
                font.weight: Font.DemiBold
            }

            Grid {
                columns: 2
                columnSpacing: Theme.spacing
                rowSpacing: 4

                Text { text: qsTr("Already there"); color: Theme.textFaint; font.pixelSize: Theme.fontSmall }
                Text { text: dialog.ops.clashOld; color: Theme.textMuted; font.pixelSize: Theme.fontSmall }
                Text { text: qsTr("Coming in"); color: Theme.textFaint; font.pixelSize: Theme.fontSmall }
                Text { text: dialog.ops.clashNew; color: Theme.textMuted; font.pixelSize: Theme.fontSmall }
            }

            // "For all": the same answer for every later clash of this operation.
            Row {
                objectName: "forAll"
                visible: dialog.ops.clashOffersForAll
                spacing: 8

                Accessible.role: Accessible.CheckBox
                Accessible.name: qsTr("Do the same for the rest")
                Accessible.checkable: true
                Accessible.checked: dialog.ops.clashForAll
                Accessible.onPressAction: dialog.ops.clashForAll = !dialog.ops.clashForAll

                Rectangle {
                    width: 16
                    height: 16
                    radius: 3
                    anchors.verticalCenter: parent.verticalCenter
                    color: dialog.ops.clashForAll ? Theme.accent : Theme.none
                    border.color: dialog.ops.clashForAll ? Theme.accent : Theme.textFaint
                    border.width: 1

                    Text {
                        anchors.centerIn: parent
                        visible: dialog.ops.clashForAll
                        text: "✓"
                        color: Theme.onAccent
                        font.pixelSize: Theme.fontSmall
                    }
                }
                Text {
                    text: qsTr("Do the same for the rest  (A)")
                    color: Theme.textMuted
                    font.pixelSize: Theme.fontNormal
                    anchors.verticalCenter: parent.verticalCenter
                }

                TapHandler { onTapped: dialog.ops.clashForAll = !dialog.ops.clashForAll }
            }

            Row {
                anchors.right: parent.right
                spacing: 8

                Repeater {
                    // Merge only when a folder meets a folder.
                    model: [
                        { name: "skipButton", label: qsTr("Skip"), key: "S", button: FileOps.SkipButton },
                        { name: "keepBothButton", label: qsTr("Keep both"), key: "K", button: FileOps.KeepBothButton },
                        { name: "replaceButton", label: qsTr("Replace"), key: "R", button: FileOps.ReplaceButton }
                    ].concat(dialog.ops.clashCanMerge
                             ? [{ name: "mergeButton", label: qsTr("Merge"), key: "M", button: FileOps.MergeButton }]
                             : [])

                    delegate: Rectangle {
                        id: choice

                        required property var modelData
                        readonly property bool chosen: dialog.ops.clashButton === modelData.button
                        readonly property string text: modelData.label

                        objectName: modelData.name
                        Accessible.role: Accessible.Button
                        Accessible.name: modelData.label
                        Accessible.description: modelData.key
                        Accessible.focusable: true
                        Accessible.focused: chosen
                        Accessible.onPressAction: dialog.ops.answerClash(modelData.button)
                        width: label.implicitWidth + Theme.spacing * 3
                        height: Theme.buttonHeight
                        radius: Theme.radius
                        color: chosen ? Theme.accentSoft : (mouse.containsMouse ? Theme.buttonHover : Theme.button)
                        Behavior on color { ColorAnimation { duration: Theme.quick } }
                        border.color: chosen ? Theme.accent : Theme.none
                        border.width: 1

                        Text {
                            id: label
                            anchors.centerIn: parent
                            text: choice.modelData.label
                            color: choice.chosen ? Theme.text : Theme.textMuted
                            font.pixelSize: Theme.fontNormal
                        }

                        MouseArea {
                            id: mouse
                            anchors.fill: parent
                            hoverEnabled: true
                            onClicked: dialog.ops.answerClash(choice.modelData.button)
                        }
                    }
                }
            }

            Text {
                width: parent.width
                horizontalAlignment: Text.AlignRight
                text: dialog.ops.clashCanMerge
                      ? qsTr("← → or Tab choose · Return answers · S, K, R, M · Merge puts its items into the folder already there, asking about each name taken")
                      : qsTr("← → or Tab choose · Return answers · S, K, R · Replace sends the old one to the trash")
                wrapMode: Text.Wrap
                color: Theme.textFaint
                font.pixelSize: Theme.fontSmall
            }
        }
    }
}
