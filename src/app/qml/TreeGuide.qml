// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

pragma ComponentBehavior: Bound

import QtQuick

// The tree's guide lines for one row, drawn from the model's line text: one character per level,
// "│" a line through, "├" a branch with more below, "└" the last branch, " " nothing.
Row {
    id: guide

    property string lines

    Repeater {
        model: guide.lines.length

        Item {
            id: level

            required property int index
            readonly property string mark: guide.lines.charAt(index)

            width: Theme.indent
            height: guide.height

            // The line down: full height, or to the middle for the last branch.
            Rectangle {
                visible: level.mark !== " "
                x: Math.round(Theme.indent / 2)
                width: 1
                height: level.mark === "└" ? Math.round(parent.height / 2) : parent.height
                color: Theme.treeGuide
            }
            // The branch across to the row's own mark.
            Rectangle {
                visible: level.mark === "├" || level.mark === "└"
                x: Math.round(Theme.indent / 2)
                y: Math.round(parent.height / 2)
                width: Math.round(Theme.indent / 2)
                height: 1
                color: Theme.treeGuide
            }
        }
    }
}
