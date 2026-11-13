// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include <QAbstractListModel>
#include <QList>
#include <QString>
#include <QStringList>
#include <QtQmlIntegration/qqmlintegration.h>

#include <functional>

class QSettings;

// The Preferences panel (Ctrl+,): every option the person can set, one row each, grouped in
// sections. A row is a switch, a choice among a few values, or a line of text. Each option is
// kept in the settings file the moment it changes and takes effect at once (changed()).
//
// The panel is drawn in QML from this model; the keys reach it through key() while it is shown.
class Preferences : public QAbstractListModel
{
    Q_OBJECT
    QML_ELEMENT
    QML_UNCREATABLE("The browser makes the preferences; QML only shows them.")

    Q_PROPERTY(bool shown READ shown WRITE setShown NOTIFY shownChanged)
    // The row the keys are on.
    Q_PROPERTY(int cursor READ cursor NOTIFY cursorChanged)
    // The text row being edited, or -1.
    Q_PROPERTY(int editing READ editing NOTIFY editingChanged)
    Q_PROPERTY(QStringList sections READ sections CONSTANT)

    // What the window reads to apply them.
    Q_PROPERTY(bool singleClick READ singleClick NOTIFY valuesChanged)
    Q_PROPERTY(int thumbnailSize READ thumbnailSize NOTIFY valuesChanged)
    Q_PROPERTY(bool treeLines READ treeLines NOTIFY valuesChanged)

public:
    enum Kind { Toggle, Choice, Text };
    Q_ENUM(Kind)

    enum Role {
        SectionRole = Qt::UserRole + 1,
        IdRole,
        LabelRole,
        HintRole,
        KindRole,
        ChoicesRole,   // the labels of a choice
        ChoiceRole,    // the index of the value chosen
        OnRole,        // a switch's state
        TextRole,      // a text row's value
    };

    struct Option
    {
        QString section;
        QString id;
        QString label;
        QString hint;
        Kind kind = Toggle;
        QStringList choiceLabels; // a choice's labels ...
        QStringList choiceValues; // ... and the values kept for them
        // Where the value lives. With `key`, the settings file holds it (`fallback` when it has
        // none); otherwise `get` and `set` reach it where it is kept (the browser's own state).
        QString key;
        QString fallback;
        std::function<QString()> get;
        std::function<void(const QString &)> set;
    };

    explicit Preferences(QSettings *settings, QObject *parent = nullptr);
    ~Preferences() override;

    void add(const Option &option);

    // A value, as kept: "true"/"false" for a switch, the chosen value for a choice.
    QString value(const QString &id) const;
    bool isOn(const QString &id) const { return value(id) == QLatin1String("true"); }
    void setValue(const QString &id, const QString &value);

    bool shown() const { return m_shown; }
    void setShown(bool shown);
    int cursor() const { return m_cursor; }
    int editing() const { return m_editing; }
    QStringList sections() const;

    bool singleClick() const { return isOn(QStringLiteral("singleClick")); }
    int thumbnailSize() const;
    bool treeLines() const { return isOn(QStringLiteral("treeLines")); }

    // The panel's controls.
    Q_INVOKABLE void toggle(int row);
    Q_INVOKABLE void choose(int row, int choice);
    Q_INVOKABLE void editText(int row);
    Q_INVOKABLE void setText(int row, const QString &text);
    Q_INVOKABLE void cancelEditing();
    Q_INVOKABLE void setCursor(int row);

    // One key press while the panel is shown: Up and Down move along the rows, Left and Right
    // change a choice, Space or Return turns a switch or edits a text, Escape or Ctrl+, close.
    // Every key is taken, so nothing reaches the files behind.
    bool key(int key, Qt::KeyboardModifiers mods);

    // Something the browser keeps changed: the rows read it again.
    void refresh();

    // QAbstractItemModel
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void shownChanged();
    void cursorChanged();
    void editingChanged();
    void valuesChanged();
    // An option changed: its id and new value.
    void changed(const QString &id, const QString &value);

private:
    int rowOf(const QString &id) const;
    QString valueAt(int row) const;
    void store(int row, const QString &value);
    void step(int row, int by);

    QSettings *m_settings;
    QList<Option> m_options;
    bool m_shown = false;
    int m_cursor = 0;
    int m_editing = -1;
};
