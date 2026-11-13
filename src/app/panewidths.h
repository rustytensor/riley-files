// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include <QFont>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

class QSettings;

// How wide the sidebar, the details pane and the columns of the columns view are.
//
// Three ways, chosen in View > Pane Widths or the Preferences:
//   fixed         sidebar 200, details 300, column 260 (the default);
//   automatic     the sidebar and the columns fit their widest row, the details pane is fixed;
//   proportional  the fixed width x window width / 1280, rounded.
// Each is then held within its limits: sidebar 140..400, details 220..600, column 180..480.
// Every divider can be dragged in any way; the width dragged to (also held within the limits)
// stays, kept in the settings, until that divider is double-clicked or the way is changed.
class PaneWidths : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The browser makes the pane widths; QML only reads them.")

    Q_PROPERTY(int sidebar READ sidebar NOTIFY widthsChanged)
    Q_PROPERTY(int details READ details NOTIFY widthsChanged)
    Q_PROPERTY(int column READ column NOTIFY widthsChanged)
    // The window's width, for the proportional way; the window sets it.
    Q_PROPERTY(int windowWidth READ windowWidth WRITE setWindowWidth NOTIFY widthsChanged)

public:
    enum Pane { Sidebar, Details, Column };
    enum Mode { Fixed, Automatic, Proportional };

    struct Limits
    {
        int fixed;
        int least;
        int most;
    };
    static Limits limits(Pane pane);
    // The width a pane gets in a way, before any drag. `natural` is its widest row's width (the
    // automatic way; 0 when not known), `windowWidth` the window's (the proportional way).
    static int widthFor(Pane pane, Mode mode, int windowWidth, int natural);
    static Mode modeFromName(const QString &name);

    explicit PaneWidths(QSettings *settings, QObject *parent = nullptr);
    ~PaneWidths() override;

    int sidebar() const { return width(Sidebar); }
    int details() const { return width(Details); }
    int column() const { return width(Column); }
    int width(Pane pane) const;

    int windowWidth() const { return m_windowWidth; }
    void setWindowWidth(int width);

    // A new way: every dragged width is forgotten.
    void setMode(Mode mode);
    Mode mode() const { return m_mode; }

    // The automatic way measures rows in this font. `sidebarRest` and `columnRest` are what a
    // row holds beside its name (margins, icon, marks), in pixels.
    Q_INVOKABLE void setRowFont(const QFont &font, int sidebarRest, int columnRest);
    // The names on the sidebar's rows and on the columns' rows, measured for the automatic way.
    void measure(Pane pane, const QStringList &names);

    // Dragging a divider: `pane` is "sidebar", "details" or "column"; `dx` how far the mouse
    // has moved right since the press. A press with no move changes nothing.
    Q_INVOKABLE void beginDrag(const QString &pane);
    Q_INVOKABLE void dragBy(const QString &pane, qreal dx);
    Q_INVOKABLE void endDrag(const QString &pane);
    // A double-click on a divider: its pane goes back to the width of the way.
    Q_INVOKABLE void reset(const QString &pane);

signals:
    void widthsChanged();

private:
    static bool paneFromName(const QString &name, Pane &pane);
    static int clampTo(Pane pane, int width);
    void keepDragged(Pane pane);

    QSettings *m_settings;
    Mode m_mode = Fixed;
    int m_windowWidth = 1280;
    int m_natural[3] = {0, 0, 0};
    int m_dragged[3] = {0, 0, 0}; // 0: not dragged
    int m_dragStart[3] = {0, 0, 0};
    QFont m_font;
    int m_rest[3] = {0, 0, 0};
    bool m_fontSet = false;
    QStringList m_names[3];
    void remeasure(Pane pane);
};
