// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

pragma ComponentBehavior: Bound

import QtQuick

// The Preferences (Ctrl+,): one row per option, in the sections the browser lists them in. A
// switch turns with a click or Space, a choice is clicked or stepped with Left and Right, a text
// is edited in place. Every change is kept and applied at once; Escape or a click outside closes.
Rectangle {
    id: panel
    objectName: "preferences"

    required property Browser browser
    readonly property Preferences prefs: browser.prefs

    visible: prefs.shown
    color: Theme.scrim

    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onClicked: panel.prefs.shown = false
        onWheel: wheel => wheel.accepted = true
    }

    Rectangle {
        id: card
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.spacing * 4, 680)
        height: Math.min(parent.height - Theme.spacing * 4, rows.contentHeight + title.height + Theme.spacing * 4)
        radius: Theme.radius * 1.5
        color: Theme.card
        border.color: Theme.cardBorder
        border.width: 1

        // Clicks on the card stay on it.
        MouseArea {
            anchors.fill: parent
        }

        Text {
            id: title
            x: Theme.spacing * 2
            y: Theme.spacing * 1.5
            text: "Preferences"
            color: Theme.text
            font.pixelSize: Theme.fontLarge
            font.weight: Font.DemiBold
        }

        Text {
            anchors { right: parent.right; rightMargin: Theme.spacing * 2; baseline: title.baseline }
            text: "↑ ↓ choose · ← → change · Space turns · Esc closes"
            color: Theme.textFaint
            font.pixelSize: Theme.fontSmall
        }

        ListView {
            id: rows
            objectName: "preferenceRows"
            anchors {
                top: title.bottom; topMargin: Theme.spacing
                left: parent.left; leftMargin: Theme.spacing * 2
                right: parent.right; rightMargin: Theme.spacing * 2
                bottom: parent.bottom; bottomMargin: Theme.spacing * 1.5
            }
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            model: panel.prefs
            currentIndex: panel.prefs.cursor
            highlightFollowsCurrentItem: false
            keyNavigationEnabled: false

            section.property: "section"
            section.delegate: Text {
                objectName: "prefSection"
                required property string section
                width: ListView.view ? ListView.view.width : 0
                topPadding: Theme.spacing
                bottomPadding: Theme.spacing / 2
                text: section
                color: Theme.textFaint
                font.pixelSize: Theme.fontSmall
                font.weight: Font.DemiBold
                font.capitalization: Font.AllUppercase
                font.letterSpacing: 1
            }

            delegate: Rectangle {
                id: row

                required property int index
                required property string optionId
                required property string label
                required property string hint
                required property int kind
                required property var choices
                required property int choice
                required property bool on
                required property string value

                readonly property bool current: panel.prefs.cursor === index

                objectName: "pref_" + optionId
                width: ListView.view ? ListView.view.width : 0
                height: Math.max(Theme.rowHeight + 14, texts.implicitHeight + 12)
                radius: Theme.radius - 2
                color: current ? Theme.cursorRow : "transparent"
                border.width: current ? 1 : 0
                border.color: Theme.accent

                MouseArea {
                    anchors.fill: parent
                    onClicked: panel.prefs.setCursor(row.index)
                }

                Column {
                    id: texts
                    anchors {
                        left: parent.left; leftMargin: Theme.spacing
                        right: control.left; rightMargin: Theme.spacing
                        verticalCenter: parent.verticalCenter
                    }
                    spacing: 2

                    Text {
                        width: parent.width
                        text: row.label
                        elide: Text.ElideRight
                        color: Theme.text
                        font.pixelSize: Theme.fontNormal
                    }
                    Text {
                        width: parent.width
                        visible: row.hint.length > 0
                        text: row.hint
                        elide: Text.ElideRight
                        color: Theme.textFaint
                        font.pixelSize: Theme.fontSmall
                    }
                }

                Item {
                    id: control
                    anchors { right: parent.right; rightMargin: Theme.spacing; verticalCenter: parent.verticalCenter }
                    width: row.kind === Preferences.Text ? Math.min(260, row.width * 0.45)
                                                         : (row.kind === Preferences.Choice ? segments.width : 40)
                    height: Theme.buttonHeight - 4

                    // A switch.
                    Rectangle {
                        objectName: "prefSwitch_" + row.optionId
                        visible: row.kind === Preferences.Toggle
                        anchors.verticalCenter: parent.verticalCenter
                        width: 40
                        height: 22
                        radius: 11
                        color: row.on ? Theme.accent : Theme.track

                        Rectangle {
                            x: row.on ? parent.width - width - 3 : 3
                            anchors.verticalCenter: parent.verticalCenter
                            width: 16
                            height: 16
                            radius: 8
                            color: row.on ? Theme.card : Theme.textMuted
                        }

                        MouseArea {
                            anchors.fill: parent
                            onClicked: panel.prefs.toggle(row.index)
                        }
                    }

                    // A choice: one segment per value.
                    Row {
                        id: segments
                        visible: row.kind === Preferences.Choice
                        spacing: 4

                        Repeater {
                            model: row.kind === Preferences.Choice ? row.choices : []

                            delegate: Rectangle {
                                id: segment

                                required property string modelData
                                required property int index
                                readonly property bool chosen: row.choice === index

                                objectName: "prefChoice_" + row.optionId + "_" + modelData
                                width: segmentText.implicitWidth + Theme.spacing * 1.5
                                height: control.height
                                radius: Theme.radius - 2
                                color: chosen ? Theme.accentSoft : (segmentMouse.containsMouse ? Theme.buttonHover : Theme.button)
                                border.color: chosen ? Theme.accent : "transparent"
                                border.width: 1

                                Text {
                                    id: segmentText
                                    anchors.centerIn: parent
                                    text: segment.modelData
                                    color: segment.chosen ? Theme.text : Theme.textMuted
                                    font.pixelSize: Theme.fontSmall
                                }

                                MouseArea {
                                    id: segmentMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    onClicked: panel.prefs.choose(row.index, segment.index)
                                }
                            }
                        }
                    }

                    // A line of text.
                    Rectangle {
                        visible: row.kind === Preferences.Text
                        anchors.fill: parent
                        radius: Theme.radius - 2
                        color: Theme.field
                        border.color: input.activeFocus ? Theme.accent : Theme.cardBorder
                        border.width: 1

                        Text {
                            anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                            visible: !input.activeFocus && row.value.length === 0
                            verticalAlignment: Text.AlignVCenter
                            text: "Home"
                            color: Theme.textFaint
                            font.pixelSize: Theme.fontSmall
                        }

                        TextInput {
                            id: input
                            objectName: "prefText_" + row.optionId
                            anchors { fill: parent; leftMargin: 8; rightMargin: 8 }
                            verticalAlignment: TextInput.AlignVCenter
                            clip: true
                            text: row.value
                            color: Theme.text
                            selectionColor: Theme.selection
                            selectByMouse: true
                            font.pixelSize: Theme.fontSmall

                            readonly property bool editing: panel.prefs.editing === row.index
                            onEditingChanged: {
                                if (editing) {
                                    text = row.value
                                    forceActiveFocus()
                                    selectAll()
                                }
                            }
                            onActiveFocusChanged: {
                                if (activeFocus)
                                    panel.prefs.editText(row.index)
                                else if (editing)
                                    panel.prefs.setText(row.index, text)
                            }
                            Keys.onReturnPressed: panel.prefs.setText(row.index, text)
                            Keys.onEnterPressed: panel.prefs.setText(row.index, text)
                            Keys.onEscapePressed: {
                                text = row.value
                                panel.prefs.cancelEditing()
                            }
                        }
                    }
                }
            }
        }
    }
}
