// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

import QtQuick

// The grip on a pane's edge: dragging it sets the pane's width, a double-click gives the pane
// back the width of the chosen way. The widths and their limits are the browser's (PaneWidths);
// this only says how far the mouse moved.
Item {
    id: divider

    required property PaneWidths panes
    // "sidebar", "details" or "column".
    required property string pane

    width: Theme.dividerGrip

    Accessible.role: Accessible.Separator
    Accessible.name: pane === "sidebar" ? qsTr("Width of the sidebar")
                   : pane === "details" ? qsTr("Width of the details pane") : qsTr("Width of the columns")
    Accessible.description: qsTr("Drag to change; a double-click gives back the chosen width")

    MouseArea {
        id: grip

        property real startX: 0

        anchors.fill: parent
        cursorShape: Qt.SplitHCursor
        preventStealing: true

        function windowX(mouse) {
            return grip.mapToItem(null, mouse.x, mouse.y).x
        }

        onPressed: mouse => {
            startX = windowX(mouse)
            divider.panes.beginDrag(divider.pane)
        }
        onPositionChanged: mouse => {
            if (pressed)
                divider.panes.dragBy(divider.pane, windowX(mouse) - startX)
        }
        onReleased: divider.panes.endDrag(divider.pane)
        onCanceled: divider.panes.endDrag(divider.pane)
        onDoubleClicked: divider.panes.reset(divider.pane)
    }
}
