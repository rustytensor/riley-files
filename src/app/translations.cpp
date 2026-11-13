// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "translations.h"

#include <QCoreApplication>
#include <QLocale>
#include <QTranslator>

namespace Translations {

namespace {

QString &loadedLanguage()
{
    static QString language;
    return language;
}

} // namespace

QString install()
{
    static const QString loaded = [] {
        auto *translator = new QTranslator(QCoreApplication::instance());
        const QLocale locale;
        const QString name = QStringLiteral("riley-files");
        const QString separator = QStringLiteral("_");
        if (!translator->load(locale, name, separator, QStringLiteral(RILEY_TRANSLATIONS_DIR))
            && !translator->load(locale, name, separator, QStringLiteral(":/i18n"))) {
            delete translator;
            return QString();
        }
        QCoreApplication::installTranslator(translator);
        loadedLanguage() = translator->language();
        return translator->language();
    }();
    return loaded;
}

QString language()
{
    return loadedLanguage();
}

QLocale locale()
{
    const QString loaded = language();
    return loaded.isEmpty() ? QLocale::c() : QLocale(loaded);
}

} // namespace Translations
