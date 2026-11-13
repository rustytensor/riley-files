// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

pragma Singleton

import QtQuick

// Every colour, size and spacing the window uses, by name. No other file holds a literal colour.
//
// The colours follow the Riley desktop's active theme (ThemeFile reads
// $XDG_CONFIG_HOME/riley/current-theme/colors.toml and follows switches as they happen). A colour
// the theme does not give, or every colour when there is no theme, comes from Riley's own palette
// below: warm charcoal and ginger, in a dark and a light variant. The theme's colours are mapped
// onto the window's roles in one place, the "Roles" section.
QtObject {
    id: theme

    // Riley's own palette, in Riley's colour names.
    readonly property var builtinDark: ({
        accent: "#e0894a",
        selection: "#4a3526",
        muted: "#7b6f62",
        background: "#1c1916",
        dark_background: "#171512",
        darker_background: "#11100e",
        lighter_background: "#26221e",
        foreground: "#e4d8c8",
        dark_foreground: "#a99b8a",
        light_foreground: "#efe5d8",
        bright_foreground: "#faf4ec",
        red: "#d96a5b",
        yellow: "#e2b763",
        orange: "#e0894a",
        green: "#9fb872",
        cyan: "#7fb6a7",
        blue: "#82a0c2",
        magenta: "#c08ba8"
    })
    readonly property var builtinLight: ({
        accent: "#bf6527",
        selection: "#f1d6bd",
        muted: "#9a8b7a",
        background: "#f7f2eb",
        dark_background: "#efe8de",
        darker_background: "#e5dccf",
        lighter_background: "#fdfbf8",
        foreground: "#2a231c",
        dark_foreground: "#66594b",
        light_foreground: "#3b3229",
        bright_foreground: "#14100c",
        red: "#b5463a",
        yellow: "#9c7418",
        orange: "#bf6527",
        green: "#5d7d31",
        cyan: "#3c8375",
        blue: "#3d6494",
        magenta: "#8d4e71"
    })

    // "dark" or "light", as the theme says (dark without a theme).
    readonly property string mode: ThemeFile.mode === "light" ? "light" : "dark"
    readonly property bool light: mode === "light"
    readonly property var builtin: light ? builtinLight : builtinDark

    // A Riley colour: the theme's, or the palette's when the theme has none by that name.
    function riley(name) {
        const given = ThemeFile.colors[name]
        return given !== undefined ? given : builtin[name]
    }
    // A colour `amount` (0 to 1) of the way from a to b.
    function mix(a, b, amount) {
        return Qt.rgba(a.r + (b.r - a.r) * amount, a.g + (b.g - a.g) * amount,
                       a.b + (b.b - a.b) * amount, a.a + (b.a - a.a) * amount)
    }
    function withAlpha(c, alpha) {
        return Qt.rgba(c.r, c.g, c.b, alpha)
    }

    // ---- The theme's colours, by Riley's names.
    readonly property color accent: riley("accent")
    readonly property color selection: riley("selection")
    readonly property color muted: riley("muted")
    readonly property color background: riley("background")
    readonly property color darkBackground: riley("dark_background")
    readonly property color darkerBackground: riley("darker_background")
    readonly property color lighterBackground: riley("lighter_background")
    readonly property color foreground: riley("foreground")
    readonly property color darkForeground: riley("dark_foreground")
    readonly property color lightForeground: riley("light_foreground")
    readonly property color brightForeground: riley("bright_foreground")
    readonly property color red: riley("red")
    readonly property color yellow: riley("yellow")
    readonly property color orange: riley("orange")
    readonly property color green: riley("green")
    readonly property color cyan: riley("cyan")
    readonly property color blue: riley("blue")
    readonly property color magenta: riley("magenta")

    // ---- Roles: what each part of the window is drawn in.
    // Nothing: a fill or border that is not there.
    readonly property color none: "transparent"

    // Surfaces, from the back of the window to the front.
    readonly property color window: background
    readonly property color content: background
    readonly property color sidebar: darkBackground
    readonly property color header: darkBackground
    readonly property color divider: mix(background, foreground, 0.11)

    // Text.
    readonly property color text: foreground
    readonly property color textMuted: darkForeground
    readonly property color textFaint: muted
    // Text on the accent colour (a chosen button).
    readonly property color onAccent: light ? lighterBackground : darkerBackground

    // The one colour that marks what is active or chosen, and a soft wash of it.
    readonly property color accentSoft: mix(background, accent, 0.2)

    // Rows: the one under the cursor, and the one under the mouse.
    readonly property color cursorRow: selection
    readonly property color hoverRow: mix(background, foreground, 0.05)
    // The cursor in a column the keys are not in (columns view), and the place shown.
    readonly property color restingRow: mix(background, foreground, 0.085)
    readonly property color treeGuide: mix(background, foreground, 0.2)
    // The ring drawn around whatever the keys are on (a button, a field).
    readonly property color focusRing: accent

    // Editing a name: the field.
    readonly property color field: darkerBackground

    // File operations: the progress strip, messages, and the question about a taken name.
    readonly property color strip: darkBackground
    readonly property color track: mix(background, foreground, 0.13)
    readonly property color button: lighterBackground
    readonly property color buttonHover: mix(lighterBackground, foreground, 0.09)
    readonly property color buttonPressed: mix(lighterBackground, foreground, 0.15)
    readonly property color notice: mix(background, yellow, 0.14)
    readonly property color noticeText: yellow
    readonly property color warning: yellow
    readonly property color danger: red
    readonly property color scrim: withAlpha(darkerBackground, light ? 0.45 : 0.63)
    readonly property color card: lighterBackground
    readonly property color cardBorder: mix(lighterBackground, foreground, 0.14)

    // File icons: folders in the accent's family, files by what they hold.
    readonly property color folder: mix(accent, foreground, 0.12)
    readonly property color folderTab: mix(accent, background, 0.25)
    readonly property var categoryColors: ({
        image: magenta,
        audio: orange,
        video: red,
        text: darkForeground,
        code: green,
        document: yellow,
        archive: mix(orange, muted, 0.5),
        file: muted
    })
    function categoryColor(category) {
        return categoryColors[category] ?? categoryColors.file
    }

    // The graph view: night space with stars, whatever the variant, the nodes in the icons'
    // colours, thin lines between them, and a soft halo round the node the keys are on.
    readonly property color graphSpace: light ? mix(brightForeground, blue, 0.12) : darkerBackground
    readonly property color graphStar: light ? lighterBackground : brightForeground
    readonly property color graphEdge: mix(graphSpace, light ? lighterBackground : muted, 0.55)
    readonly property color graphMark: graphStar
    readonly property color graphLight: light ? lighterBackground : brightForeground
    readonly property color graphText: light ? lighterBackground : foreground
    readonly property color graphTextMuted: light ? darkerBackground : darkForeground

    // ---- Sizes, in logical pixels (Qt scales them for the screen).
    // The spacing scale: every gap and margin is one of these or a step of `spacing`.
    readonly property int spaceTiny: 2
    readonly property int spaceSmall: 4
    readonly property int spaceMedium: 8
    property int spacing: 12
    readonly property int spaceLarge: 18
    readonly property int spaceHuge: 24

    // Corner radii: small for marks and rows, normal for buttons and fields, large for cards.
    readonly property int radiusSmall: 4
    property int radius: 8
    readonly property int radiusLarge: 12

    // The type scale.
    readonly property int fontTiny: 11
    property int fontSmall: 12
    property int fontNormal: 14
    property int fontLarge: 17
    readonly property int fontTitle: 20
    // Text files in the preview.
    property string monoFamily: "monospace"

    // Transitions stay short, so nothing ever waits on them.
    readonly property int quick: 90

    property int headerHeight: 52
    property int rowHeight: 30
    property int iconSize: 18
    property int indent: 18
    property int sizeColumn: 96
    property int dateColumn: 150
    // Grid tiles and the columns view's columns.
    property int tileWidth: 112
    property int tileHeight: 104
    property int tileIcon: 44
    // The sidebar's, the details pane's and the columns' widths come from the browser
    // (PaneWidths); this is how wide the grip on their dividers is.
    property int dividerGrip: 7
    // The accent stripe beside the row under the cursor, and the focus ring.
    readonly property int stripe: 2
    readonly property int focusWidth: 2
    property int stripHeight: 58
    property int buttonHeight: 32
    property int dialogWidth: 460
    // The menu bar and its menus.
    property int menuBarHeight: 28
    property int menuWidth: 250
    property int menuRowHeight: 26
    property int menuPadding: 4
}
