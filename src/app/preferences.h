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
// sections, the sections on pages (the options, the search index). A row is a switch, a choice
// among a few values, a line of text, a line that only tells (Info), a button, or an item of a
// list (a folder, a pattern) that can be removed. Each option is kept the moment it changes and
// takes effect at once (changed()).
//
// The panel is drawn in QML from this model, which holds the rows of the page shown; the keys
// reach it through key() while it is shown. The cursor -1 is the row of page tabs above them.
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
    Q_PROPERTY(QStringList sections READ sections NOTIFY pageChanged)
    // The pages, by title, and the one shown.
    Q_PROPERTY(QStringList pages READ pages NOTIFY pagesChanged)
    Q_PROPERTY(int page READ page WRITE setPage NOTIFY pageChanged)

    // What the window reads to apply them.
    Q_PROPERTY(bool singleClick READ singleClick NOTIFY valuesChanged)
    Q_PROPERTY(int thumbnailSize READ thumbnailSize NOTIFY valuesChanged)
    Q_PROPERTY(bool treeLines READ treeLines NOTIFY valuesChanged)

public:
    enum Kind { Toggle, Choice, Text, Info, Button, Item };
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
        TextRole,      // a text row's value; an Info row's text
        PlaceholderRole, // what an empty text row shows
        RemovableRole, // the row has a Remove button
        ButtonRole,    // a button's caption
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
        // Its page (see addPage()), what an empty text shows, and what Remove (the Delete key)
        // does on a row that has it. A button runs `set` with an empty value.
        int page = 0;
        QString placeholder = {};
        std::function<void()> remove = {};
        // A hint that changes (the index's state); otherwise `hint`.
        std::function<QString()> getHint = {};
    };

    explicit Preferences(QSettings *settings, QObject *parent = nullptr);
    ~Preferences() override;

    void add(const Option &option);
    // A page of its own after the ones before; returns its number for Option::page.
    int addPage(const QString &title);
    // Every option of `page` replaced by these (a list that grew or shrank).
    void setPageOptions(int page, const QList<Option> &options);

    // A value, as kept: "true"/"false" for a switch, the chosen value for a choice.
    QString value(const QString &id) const;
    bool isOn(const QString &id) const { return value(id) == QLatin1String("true"); }
    void setValue(const QString &id, const QString &value);

    bool shown() const { return m_shown; }
    void setShown(bool shown);
    int cursor() const { return m_cursor; }
    int editing() const { return m_editing; }
    QStringList sections() const;
    QStringList pages() const { return m_pages; }
    int page() const { return m_page; }
    void setPage(int page);

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
    Q_INVOKABLE void press(int row);
    Q_INVOKABLE void remove(int row);

    // One key press while the panel is shown: Up and Down move along the rows, Left and Right
    // change a choice, Space or Return turns a switch or edits a text, Escape or Ctrl+, close.
    // Every key is taken, so nothing reaches the files behind.
    bool key(int key, Qt::KeyboardModifiers mods);

    // Something the browser keeps changed: the rows read it again.
    void refresh();
    // One option read again.
    void refresh(const QString &id);
    // An option another tab's Preferences changed: read again from the settings file and applied here, as
    // though it had been set here (changed() is sent; nothing is written).
    void follow(const QString &id, const QString &value);

    // QAbstractItemModel
    int rowCount(const QModelIndex &parent = QModelIndex()) const override;
    QVariant data(const QModelIndex &index, int role = Qt::DisplayRole) const override;
    QHash<int, QByteArray> roleNames() const override;

signals:
    void shownChanged();
    void cursorChanged();
    void editingChanged();
    void valuesChanged();
    void pagesChanged();
    void pageChanged();
    // An option changed: its id and new value.
    void changed(const QString &id, const QString &value);

private:
    // Options are found by their place in m_options; rows are the ones of the page shown.
    int optionOf(const QString &id) const;
    int optionAt(int row) const;
    int rowOfOption(int option) const;
    QString valueOf(int option) const;
    void store(int option, const QString &value);
    void step(int row, int by);
    void showRows();

    QSettings *m_settings;
    QList<Option> m_options;
    QList<int> m_rows;
    QStringList m_pages;
    int m_page = 0;
    bool m_shown = false;
    int m_cursor = 0;
    int m_editing = -1;
};
