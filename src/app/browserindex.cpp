// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

// The search index in the window: the Preferences' Index page (what is indexed, Pause, Resume,
// Rebuild, how far it is), the indexer service told of each change, and the index the search
// asks before it walks the folders or runs ripgrep.

#include "browser.h"

#include "indexdatabase.h"

#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QPointer>
#include <QProcess>
#include <QSettings>
#include <QStandardPaths>
#include <QTimer>

#include <algorithm>

using Option = Preferences::Option;
using Riley::IndexDatabase;
using Riley::IndexerClient;
using Riley::IndexSettings;
using Riley::IndexStatus;
using namespace Qt::StringLiterals;

namespace {

const QStringList onOff{u"false"_s, u"true"_s};

// The index as the search asks it: a database opened read-only for each question, so any of
// the search's threads may ask.
class IndexSource : public Riley::SearchIndex
{
public:
    explicit IndexSource(QString file)
        : m_file(std::move(file))
    {
    }

    bool names(const QString &folder, const QString &query, bool showHidden, int limit, QList<Name> &found) override
    {
        IndexDatabase db;
        if (!openDone(db) || !db.hasFolder(QDir::cleanPath(folder)))
            return false;
        const QList<Riley::NameMatch> matches = db.nameMatches(query, QDir::cleanPath(folder), showHidden, limit);
        for (const Riley::NameMatch &match : matches)
            found.append({match.path, match.folder});
        return true;
    }

    QStringList contentFolders(const QString &folder) override
    {
        IndexDatabase db;
        QStringList folders;
        if (!openDone(db))
            return folders;
        const QString clean = QDir::cleanPath(folder);
        const QStringList indexed = db.contentFolders();
        for (const QString &f : indexed) {
            if ((Riley::isAtOrBelow(f, clean) || Riley::isAtOrBelow(clean, f)) && db.hasFolder(f))
                folders.append(f);
        }
        return folders;
    }

    bool contents(const QString &folder, const QString &query, bool showHidden, int limit, QStringList &files) override
    {
        IndexDatabase db;
        if (!openDone(db))
            return false;
        files = db.contentsQuery(query, QDir::cleanPath(folder), showHidden, limit);
        return true;
    }

private:
    // Only a finished index answers: during its first scan or a rebuild the folders are walked.
    bool openDone(IndexDatabase &db) const
    {
        return QFileInfo::exists(m_file) && db.open(m_file, IndexDatabase::ReadOnly)
               && db.state() == QLatin1String("done");
    }

    QString m_file;
};

QString cleanFolder(const QString &typed)
{
    QString path = typed.trimmed();
    if (path == u'~')
        path = QDir::homePath();
    else if (path.startsWith(u"~/"_s))
        path = QDir::homePath() + path.mid(1);
    if (!QDir::isAbsolutePath(path))
        return {};
    return QDir::cleanPath(path);
}

// A folder as the index settings keep it, cleaned and absolute, the way their roots() say it.
QString absoluteFolder(const QString &kept)
{
    QString path = kept.trimmed();
    if (path == u'~' || path.startsWith(u"~/"_s))
        path = QDir::homePath() + path.mid(1);
    return QDir::cleanPath(QFileInfo(path).absoluteFilePath());
}

QStringList without(const QStringList &folders, const QString &folder)
{
    QStringList kept;
    for (const QString &f : folders)
        if (absoluteFolder(f) != folder)
            kept.append(f);
    return kept;
}

} // namespace

bool Browser::startIndexer(const QString &settingsFile)
{
    QSettings settings(settingsFile.isEmpty() ? defaultSettingsFile() : settingsFile, QSettings::IniFormat);
    if (!IndexSettings::load(settings).enabled)
        return false;
    if (IndexerClient::send(u"ping"_s, nullptr, 0))
        return false; // running already
    const QString systemctl = QStandardPaths::findExecutable(u"systemctl"_s);
    if (systemctl.isEmpty())
        return false;
    return QProcess::startDetached(systemctl, {u"--user"_s, u"start"_s, u"riley-files-indexer.service"_s});
}

void Browser::addIndexPage()
{
    m_index = IndexSettings::load(*m_settings);
    m_indexPage = m_prefs->addPage(tr("Index"));
    m_indexPool = std::make_unique<QThreadPool>();
    m_indexPool->setMaxThreadCount(1);
    m_search->setIndex(std::make_shared<IndexSource>(IndexDatabase::defaultFile()));

    // While the page is shown, how far the index is is read again every half second.
    m_indexTimer = new QTimer(this);
    m_indexTimer->setInterval(500);
    connect(m_indexTimer, &QTimer::timeout, this, &Browser::refreshIndexStatus);
    const auto follow = [this] {
        const bool shown = m_prefs->shown() && m_prefs->page() == m_indexPage;
        if (shown && !m_indexTimer->isActive()) {
            refreshIndexStatus();
            m_indexTimer->start();
        } else if (!shown) {
            m_indexTimer->stop();
        }
    };
    connect(m_prefs.get(), &Preferences::shownChanged, this, follow);
    connect(m_prefs.get(), &Preferences::pageChanged, this, follow);
    showIndexOptions();
}

void Browser::showIndexOptions()
{
    const QString index = tr("Index");
    const QString folders = tr("Folders");
    const QString leftOut = tr("Left out");
    const QString contents = tr("Contents");
    QList<Option> options;

    Option status{index, u"indexStatus"_s, tr("Status"), {}, Preferences::Info, {}, {}, {}, {}, {}, {}};
    status.get = [this] { return indexStatusText(); };
    status.getHint = [this] { return indexStateText(); };
    options.append(status);

    Option enabled{index, u"indexEnabled"_s, tr("Index files for search"),
                   tr("Off, search walks the folders each time"), Preferences::Toggle, {}, onOff, {}, {}, {}, {}};
    enabled.get = [this] { return m_index.enabled ? u"true"_s : u"false"_s; };
    enabled.set = [this](const QString &v) {
        m_index.enabled = v == QLatin1String("true");
        saveIndexSettings();
    };
    options.append(enabled);

    Option pause{index, u"indexPause"_s, tr("Pause indexing"), tr("Changes are kept until it resumes"),
                 Preferences::Button, {tr("Pause")}, {}, {}, {}, {}, {}};
    pause.set = [this](const QString &) { indexCommand(u"pause"_s); };
    options.append(pause);
    Option resume{index, u"indexResume"_s, tr("Resume indexing"), tr("The changes kept meanwhile are applied"),
                  Preferences::Button, {tr("Resume")}, {}, {}, {}, {}, {}};
    resume.set = [this](const QString &) { indexCommand(u"resume"_s); };
    options.append(resume);
    Option rebuild{index, u"indexRebuild"_s, tr("Rebuild the index"), tr("Every folder is read again from nothing"),
                   Preferences::Button, {tr("Rebuild")}, {}, {}, {}, {}, {}};
    rebuild.set = [this](const QString &) { indexCommand(u"rebuild"_s); };
    options.append(rebuild);

    // Each folder indexed, its switch for its files' contents too, and Remove.
    for (const QString &folder : m_index.roots()) {
        Option row{folders, u"indexFolder:"_s + folder, folder, {}, Preferences::Toggle, {}, onOff, {}, {}, {}, {}};
        const auto wanted = [this, folder] {
            return std::any_of(m_index.contentFolders.cbegin(), m_index.contentFolders.cend(),
                               [&folder](const QString &f) { return absoluteFolder(f) == folder; });
        };
        row.get = [wanted] { return wanted() ? u"true"_s : u"false"_s; };
        row.getHint = [wanted] {
            return wanted() ? tr("Names and the contents of its files") : tr("Names only; switch on for contents too");
        };
        row.set = [this, folder](const QString &v) {
            m_index.contentFolders = without(m_index.contentFolders, folder);
            if (v == QLatin1String("true"))
                m_index.contentFolders.append(folder);
            saveIndexSettings();
        };
        row.remove = [this, folder] {
            m_index.folders = without(m_index.folders, folder);
            m_index.contentFolders = without(m_index.contentFolders, folder);
            saveIndexSettings();
            // Made again after this returns: the row clicked is one of them.
            QTimer::singleShot(0, this, &Browser::showIndexOptions);
        };
        options.append(row);
    }
    Option addFolder{folders, u"indexAddFolder"_s, tr("Add a folder"), tr("Type its path, then Return"),
                     Preferences::Text, {}, {}, {}, {}, {}, {}};
    addFolder.placeholder = tr("/path/to/folder");
    addFolder.get = [] { return QString(); };
    addFolder.set = [this](const QString &typed) {
        const QString folder = cleanFolder(typed);
        if (folder.isEmpty() || !QFileInfo(folder).isDir()) {
            m_ops->say(tr("“%1” is not a folder.").arg(typed));
            return;
        }
        if (!m_index.roots().contains(folder)) {
            m_index.folders.append(folder);
            saveIndexSettings();
        }
        // Made again after this returns: the row being changed is one of them.
        QTimer::singleShot(0, this, &Browser::showIndexOptions);
    };
    options.append(addFolder);

    for (const QString &pattern : std::as_const(m_index.excludes)) {
        Option row{leftOut, u"indexExclude:"_s + pattern, pattern, {}, Preferences::Item, {}, {}, {}, {}, {}, {}};
        row.remove = [this, pattern] {
            m_index.excludes.removeAll(pattern);
            saveIndexSettings();
            // Made again after this returns: the row clicked is one of them.
            QTimer::singleShot(0, this, &Browser::showIndexOptions);
        };
        options.append(row);
    }
    Option addExclude{leftOut, u"indexAddExclude"_s, tr("Leave out names like"),
                      tr("A pattern such as *.log; one with a / is matched against the whole path"),
                      Preferences::Text, {}, {}, {}, {}, {}, {}};
    addExclude.placeholder = u"*.log"_s;
    addExclude.get = [] { return QString(); };
    addExclude.set = [this](const QString &typed) {
        const QString pattern = typed.trimmed();
        if (!pattern.isEmpty() && !m_index.excludes.contains(pattern)) {
            m_index.excludes.append(pattern);
            saveIndexSettings();
        }
        QTimer::singleShot(0, this, &Browser::showIndexOptions);
    };
    options.append(addExclude);

    Option limit{contents, u"indexLimit"_s, tr("Size limit for contents"), tr("Larger files are indexed by name only"),
                 Preferences::Choice,
                 {tr("256 KB"), tr("1 MB"), tr("4 MB"), tr("16 MB"), tr("64 MB")},
                 {QString::number(256 * 1024), QString::number(1024 * 1024), QString::number(4 * 1024 * 1024),
                  QString::number(16 * 1024 * 1024), QString::number(64 * 1024 * 1024)},
                 {}, {}, {}, {}};
    limit.get = [this] { return QString::number(m_index.contentLimit); };
    limit.set = [this](const QString &v) {
        m_index.contentLimit = v.toLongLong();
        saveIndexSettings();
    };
    options.append(limit);
    Option textOnly{contents, u"indexTextOnly"_s, tr("Text files only"),
                    tr("The contents of other files are not read"), Preferences::Toggle, {}, onOff, {}, {}, {}, {}};
    textOnly.get = [this] { return m_index.textOnly ? u"true"_s : u"false"_s; };
    textOnly.set = [this](const QString &v) {
        m_index.textOnly = v == QLatin1String("true");
        saveIndexSettings();
    };
    options.append(textOnly);

    m_prefs->setPageOptions(m_indexPage, options);
}

void Browser::saveIndexSettings()
{
    m_index.save(*m_settings);
    m_settings->sync();
    // The service reads the file again; with indexing turned on and no service yet, it starts.
    if (!IndexerClient::send(u"reload"_s, nullptr, 0) && m_index.enabled)
        startIndexer(m_settings->fileName());
    QTimer::singleShot(300, this, &Browser::refreshIndexStatus);
}

void Browser::indexCommand(const QString &command)
{
    if (!IndexerClient::send(command, nullptr, 0)) {
        m_ops->say(tr("The indexer is not running."));
        if (m_index.enabled)
            startIndexer(m_settings->fileName());
    }
    refreshIndexStatus();
    QTimer::singleShot(300, this, &Browser::refreshIndexStatus);
}

void Browser::refreshIndexStatus()
{
    if (m_indexStatusAsked || !m_indexPool)
        return;
    m_indexStatusAsked = true;
    const QString file = IndexDatabase::defaultFile();
    QPointer<Browser> self(this);
    m_indexPool->start([self, file] {
        const IndexStatus status = IndexStatus::read(file);
        if (self)
            QMetaObject::invokeMethod(self.data(), [self, status] {
                if (self)
                    self->setIndexStatus(status);
            }, Qt::QueuedConnection);
    });
}

void Browser::setIndexStatus(const IndexStatus &status)
{
    m_indexStatusAsked = false;
    m_indexStatus = status;
    m_indexStatusRead = true;
    m_prefs->refresh(u"indexStatus"_s);
}

QString Browser::indexStatusText() const
{
    if (!m_indexStatusRead)
        return {};
    if (!m_indexStatus.exists)
        return tr("No index yet");
    const QString size = QLocale().toString(double(m_indexStatus.bytes) / (1024.0 * 1024.0), 'f', 1);
    if (m_indexStatus.files == 1)
        return tr("1 file indexed, %1 MB").arg(size);
    return tr("%1 files indexed, %2 MB").arg(QLocale().toString(m_indexStatus.files), size);
}

QString Browser::indexStateText() const
{
    if (!m_index.enabled)
        return tr("Turned off");
    if (!m_indexStatusRead)
        return tr("Reading…");
    if (!m_indexStatus.exists)
        return tr("The indexer has not started yet");
    if (m_indexStatus.paused)
        return tr("Paused");
    if (m_indexStatus.state == QLatin1String("done"))
        return tr("Up to date");
    return tr("Indexing…");
}
