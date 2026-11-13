// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#pragma once

#include <QLocale>
#include <QString>

// The program's words in the person's language. The language comes from the system (LANGUAGE,
// then LC_ALL, LC_MESSAGES and LANG, through QLocale); English needs no catalog.
namespace Translations {

// Loads the catalog for the system's language and gives it to the application, once per run; a
// second call does nothing. Must run before any window is made, so every word on it is
// translated. The catalog in the installed place comes first, then the one built into the
// program. Returns the language loaded ("de", "fr"), or empty when the words stay English.
QString install();

// The language install() loaded, or empty while the words are English.
QString language();

// The locale that goes with the words: the loaded language's, or the plain C locale with English
// month and day names, so a date never mixes two languages.
QLocale locale();

} // namespace Translations
