// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

pragma ComponentBehavior: Bound

import QtQuick

// The columns view: the folder in the first column, and each folder opened from it in a
// column to its right. The column the keys are in, and the one after it, stay in sight.
Flickable {
    id: columns
    objectName: "columns"

    required property Browser browser

    clip: true
    contentWidth: strip.width
    contentHeight: height
    flickableDirection: Flickable.HorizontalFlick
    boundsBehavior: Flickable.StopAtBounds

    Row {
        id: strip
        height: columns.height

        Repeater {
            model: columns.browser.columns

            ColumnPane {
                browser: columns.browser
                height: strip.height
            }
        }
    }

    function showActive() {
        const shown = Math.min(browser.columns.length, browser.activeColumn + 2)
        const right = shown * Theme.columnWidth
        const left = browser.activeColumn * Theme.columnWidth
        let x = contentX
        if (right > x + width)
            x = right - width
        if (left < x)
            x = left
        contentX = Math.max(0, Math.min(x, strip.width - width))
    }

    Connections {
        target: columns.browser
        function onActiveColumnChanged() { Qt.callLater(columns.showActive) }
        function onColumnsChanged() { Qt.callLater(columns.showActive) }
    }
}
