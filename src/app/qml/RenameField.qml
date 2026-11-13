// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

import QtQuick

// The field a name is edited in, in place of the name it edits. Return gives the new name,
// Escape leaves it as it was; the file operations decide what happens.
Rectangle {
    id: field

    required property FileOps ops
    // Where this field is meant to show (the list puts one over the cursor's row).
    property bool active: true

    visible: active && ops.editing
    color: Theme.field
    border.color: Theme.accent
    border.width: 1
    radius: 4

    function begin() {
        input.text = field.ops.editingName
        input.selectAll()
        input.forceActiveFocus()
    }

    onVisibleChanged: if (visible) begin()

    Connections {
        target: field.ops
        function onEditingChanged() {
            if (field.visible)
                field.begin()
        }
    }

    TextInput {
        id: input
        objectName: "renameField"
        Accessible.role: Accessible.EditableText
        Accessible.name: qsTr("New name")

        anchors { fill: parent; leftMargin: 6; rightMargin: 6 }
        verticalAlignment: TextInput.AlignVCenter
        clip: true
        color: Theme.text
        selectionColor: Theme.selection
        selectedTextColor: Theme.text
        font.pixelSize: Theme.fontNormal
        selectByMouse: true

        // Return is taken here, so it never goes on to open what is under the cursor.
        Keys.onReturnPressed: event => {
            field.ops.commitRename(text)
            event.accepted = true
        }
        Keys.onEnterPressed: event => {
            field.ops.commitRename(text)
            event.accepted = true
        }
        Keys.onEscapePressed: event => {
            field.ops.cancelRename()
            event.accepted = true
        }
        // Clicking away keeps what was typed, as Return would.
        onActiveFocusChanged: if (!activeFocus && field.visible) field.ops.commitRename(text)
    }
}
