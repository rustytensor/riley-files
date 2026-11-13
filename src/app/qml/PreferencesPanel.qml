// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

pragma ComponentBehavior: Bound

import QtQuick

// The Preferences (Ctrl+,): pages of options (tabs at the top: the options, the search index),
// one row per option, in the sections the browser lists them in. A switch turns with a click or
// Space, a choice is clicked or stepped with Left and Right, a text is edited in place, a button
// is pressed, and an item of a list goes with its Remove button (or Delete). Every change is kept
// and applied at once; Escape or a click outside closes.
Rectangle {
    id: panel
    objectName: "preferences"

    required property Browser browser
    readonly property Preferences prefs: browser.prefs

    visible: prefs.shown
    color: Theme.scrim

    Accessible.role: Accessible.Dialog
    Accessible.name: qsTr("Preferences")

    // The focus goes to the row the cursor is on, so the row a person is on is also the one
    // assistive technology reads; the list keeps it on its current row as the cursor moves. The
    // keys still go on to the browser (Tab, the arrows, Space), which moves the cursor.
    function focusCursor() {
        // A text being edited keeps the keys.
        if (!panel.visible || panel.prefs.editing >= 0)
            return
        if (panel.prefs.cursor < 0) {
            pageBar.forceActiveFocus()
            return
        }
        rows.positionViewAtIndex(panel.prefs.cursor, ListView.Contain)
        const row = rows.itemAtIndex(panel.prefs.cursor)
        if (row)
            row.forceActiveFocus()
    }
    onVisibleChanged: if (visible) focusCursor()

    // The keys move the cursor between the tabs and the rows, and onto rows scrolled away.
    Connections {
        target: panel.prefs
        function onCursorChanged() { Qt.callLater(panel.focusCursor) }
        function onModelReset() { Qt.callLater(panel.focusCursor) }
    }

    // A click outside the card closes the Preferences, as Escape does; it is not a control.
    MouseArea {
        anchors.fill: parent
        acceptedButtons: Qt.AllButtons
        onClicked: panel.prefs.shown = false
        onWheel: wheel => wheel.accepted = true
        Accessible.ignored: true
    }

    Rectangle {
        id: card
        anchors.centerIn: parent
        width: Math.min(parent.width - Theme.spacing * 4, 680)
        height: Math.min(parent.height - Theme.spacing * 4,
                         rows.contentHeight + title.height + pageBar.height + Theme.spacing * 5)
        radius: Theme.radius * 1.5
        color: Theme.card
        border.color: Theme.cardBorder
        border.width: 1

        // Clicks on the card stay on it.
        MouseArea {
            anchors.fill: parent
            Accessible.ignored: true
        }

        Text {
            id: title
            x: Theme.spacing * 2
            y: Theme.spacing * 1.5
            text: qsTr("Preferences")
            color: Theme.text
            font.pixelSize: Theme.fontLarge
            font.weight: Font.DemiBold
        }

        Text {
            anchors { right: parent.right; rightMargin: Theme.spacing * 2; baseline: title.baseline }
            text: qsTr("↑ ↓ or Tab choose · ← → change · Space turns · Esc closes")
            color: Theme.textFaint
            font.pixelSize: Theme.fontSmall
        }

        // The pages, as tabs. The keys reach them as the row above the first option (the cursor
        // -1): Left and Right turn the pages.
        Row {
            id: pageBar
            objectName: "prefPages"
            x: Theme.spacing * 2
            anchors { top: title.bottom; topMargin: Theme.spacing }
            spacing: 4
            Accessible.role: Accessible.PageTabList
            Accessible.name: qsTr("Pages")
            Accessible.focusable: true
            Accessible.focused: activeFocus

            Repeater {
                model: panel.prefs.pages

                delegate: Rectangle {
                    id: tab

                    required property string modelData
                    required property int index
                    readonly property bool chosen: panel.prefs.page === index

                    objectName: "prefPage_" + modelData
                    Accessible.role: Accessible.PageTab
                    Accessible.name: modelData
                    Accessible.checkable: true
                    Accessible.checked: chosen
                    Accessible.onPressAction: panel.prefs.page = tab.index
                    width: tabText.implicitWidth + Theme.spacing * 2
                    height: Theme.buttonHeight - 2
                    radius: Theme.radius - 2
                    color: chosen ? Theme.accentSoft : (tabMouse.containsMouse ? Theme.buttonHover : Theme.button)
                    Behavior on color { ColorAnimation { duration: Theme.quick } }
                    border.color: chosen ? Theme.accent : Theme.none
                    border.width: pageBar.activeFocus && chosen ? Theme.focusWidth : 1

                    Text {
                        id: tabText
                        anchors.centerIn: parent
                        text: tab.modelData
                        color: tab.chosen ? Theme.text : Theme.textMuted
                        font.pixelSize: Theme.fontNormal
                    }

                    MouseArea {
                        id: tabMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: panel.prefs.page = tab.index
                    }
                }
            }
        }

        ListView {
            id: rows
            objectName: "preferenceRows"
            anchors {
                top: pageBar.bottom; topMargin: Theme.spacing
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
                required property string placeholder
                required property bool removable
                required property string button

                readonly property bool current: panel.prefs.cursor === index

                objectName: "pref_" + optionId
                Accessible.role: kind === Preferences.Toggle ? Accessible.CheckBox
                               : kind === Preferences.Choice ? Accessible.RadioButton
                               : kind === Preferences.Text ? Accessible.EditableText
                               : kind === Preferences.Button ? Accessible.Button
                               : kind === Preferences.Info ? Accessible.StaticText : Accessible.ListItem
                Accessible.name: label
                Accessible.description: kind === Preferences.Choice ? (choices[choice] ?? "") + (hint.length > 0 ? " · " + hint : "")
                                      : kind === Preferences.Info ? value + (hint.length > 0 ? " · " + hint : "")
                                                                  : hint
                Accessible.focusable: true
                Accessible.focused: activeFocus
                Accessible.checkable: kind === Preferences.Toggle
                Accessible.checked: on
                Accessible.onPressAction: {
                    panel.prefs.setCursor(index)
                    if (kind === Preferences.Toggle)
                        panel.prefs.toggle(index)
                    else if (kind === Preferences.Button)
                        panel.prefs.press(index)
                }
                width: ListView.view ? ListView.view.width : 0
                height: Math.max(Theme.rowHeight + 14, texts.implicitHeight + 12)
                radius: Theme.radius - 2
                color: current ? Theme.cursorRow : (rowMouse.containsMouse ? Theme.hoverRow : Theme.none)
                Behavior on color { ColorAnimation { duration: Theme.quick } }
                border.width: current ? Theme.focusWidth : 0
                border.color: Theme.focusRing

                MouseArea {
                    id: rowMouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: panel.prefs.setCursor(row.index)
                }

                Column {
                    id: texts
                    anchors {
                        left: parent.left; leftMargin: Theme.spacing
                        right: remove.left; rightMargin: Theme.spacing
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

                // Remove, on an item of a list (a folder indexed, a pattern left out).
                Rectangle {
                    id: remove
                    objectName: "prefRemove_" + row.optionId
                    visible: row.removable
                    Accessible.role: Accessible.Button
                    Accessible.name: qsTr("Remove “%1”").arg(row.label)
                    Accessible.onPressAction: panel.prefs.remove(row.index)
                    anchors { right: control.left; rightMargin: row.removable && control.width > 0 ? Theme.spacing : 0
                              verticalCenter: parent.verticalCenter }
                    width: row.removable ? removeText.implicitWidth + Theme.spacing * 1.5 : 0
                    height: Theme.buttonHeight - 4
                    radius: Theme.radius - 2
                    color: removeMouse.containsMouse ? Theme.buttonHover : Theme.button

                    Text {
                        id: removeText
                        anchors.centerIn: parent
                        text: qsTr("Remove")
                        color: Theme.textMuted
                        font.pixelSize: Theme.fontSmall
                    }

                    MouseArea {
                        id: removeMouse
                        anchors.fill: parent
                        hoverEnabled: true
                        enabled: row.removable
                        onClicked: panel.prefs.remove(row.index)
                    }
                }

                Item {
                    id: control
                    anchors { right: parent.right; rightMargin: Theme.spacing; verticalCenter: parent.verticalCenter }
                    width: row.kind === Preferences.Text ? Math.min(260, row.width * 0.45)
                         : row.kind === Preferences.Choice ? segments.width
                         : row.kind === Preferences.Toggle ? 40
                         : row.kind === Preferences.Button ? pressable.width
                         : row.kind === Preferences.Info ? info.implicitWidth : 0
                    height: Theme.buttonHeight - 4

                    // A line that only tells (how far the index is).
                    Text {
                        id: info
                        objectName: "prefInfo_" + row.optionId
                        visible: row.kind === Preferences.Info
                        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                        text: row.kind === Preferences.Info ? row.value : ""
                        color: Theme.text
                        font.pixelSize: Theme.fontNormal
                    }

                    // A button.
                    Rectangle {
                        id: pressable
                        objectName: "prefButton_" + row.optionId
                        visible: row.kind === Preferences.Button
                        Accessible.role: Accessible.Button
                        Accessible.name: row.label
                        Accessible.onPressAction: panel.prefs.press(row.index)
                        anchors { right: parent.right; verticalCenter: parent.verticalCenter }
                        width: buttonText.implicitWidth + Theme.spacing * 2
                        height: parent.height
                        radius: Theme.radius - 2
                        color: buttonMouse.containsMouse ? Theme.buttonHover : Theme.button
                        border.color: Theme.cardBorder
                        border.width: 1

                        Text {
                            id: buttonText
                            anchors.centerIn: parent
                            text: row.button
                            color: Theme.text
                            font.pixelSize: Theme.fontSmall
                        }

                        MouseArea {
                            id: buttonMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            enabled: row.kind === Preferences.Button
                            onClicked: panel.prefs.press(row.index)
                        }
                    }

                    // A switch.
                    Rectangle {
                        objectName: "prefSwitch_" + row.optionId
                        visible: row.kind === Preferences.Toggle
                        Accessible.role: Accessible.CheckBox
                        Accessible.name: row.label
                        Accessible.checkable: true
                        Accessible.checked: row.on
                        Accessible.onPressAction: panel.prefs.toggle(row.index)
                        anchors.verticalCenter: parent.verticalCenter
                        width: 40
                        height: 22
                        radius: 11
                        color: row.on ? Theme.accent : (switchMouse.containsMouse ? Theme.buttonHover : Theme.track)
                        Behavior on color { ColorAnimation { duration: Theme.quick } }

                        Rectangle {
                            x: row.on ? parent.width - width - 3 : 3
                            Behavior on x { NumberAnimation { duration: Theme.quick; easing.type: Easing.OutCubic } }
                            anchors.verticalCenter: parent.verticalCenter
                            width: 16
                            height: 16
                            radius: 8
                            color: row.on ? Theme.card : Theme.textMuted
                        }

                        MouseArea {
                            id: switchMouse
                            anchors.fill: parent
                            hoverEnabled: true
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
                                Accessible.role: Accessible.RadioButton
                                Accessible.name: modelData
                                Accessible.checkable: true
                                Accessible.checked: chosen
                                Accessible.onPressAction: panel.prefs.choose(row.index, segment.index)
                                width: segmentText.implicitWidth + Theme.spacing * 1.5
                                height: control.height
                                radius: Theme.radius - 2
                                color: chosen ? Theme.accentSoft : (segmentMouse.containsMouse ? Theme.buttonHover : Theme.button)
                                Behavior on color { ColorAnimation { duration: Theme.quick } }
                                border.color: chosen ? Theme.accent : Theme.none
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
                            text: row.placeholder
                            color: Theme.textFaint
                            font.pixelSize: Theme.fontSmall
                        }

                        TextInput {
                            id: input
                            objectName: "prefText_" + row.optionId
                            Accessible.role: Accessible.EditableText
                            Accessible.name: row.label
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
