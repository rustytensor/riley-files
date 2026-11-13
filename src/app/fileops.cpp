// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "fileops.h"

#include "filejobs.h"
#include "filenames.h"
#include "trashjobs.h"

#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QLocale>
#include <QMimeData>
#include <QUrl>

#include <algorithm>

using Riley::FileJob;

namespace {

// A job shows its progress once it has run this long, so quick ones do not flash a strip.
constexpr int progressDelayMs = 400;
// A job this big shows it at once: it will take a while.
constexpr qint64 bigJobFiles = 1000;
constexpr qint64 bigJobBytes = qint64(256) * 1024 * 1024;
// Messages go away on their own after this long.
constexpr int messageMs = 8000;

const char *const kdeCutType = "application/x-kde-cutselection";

QString nameOf(const QString &path)
{
    const QString name = QFileInfo(path).fileName();
    return name.isEmpty() ? path : name;
}

QString quoted(const QString &text)
{
    return QStringLiteral("“%1”").arg(text);
}

QString folderTitle(const QString &folder)
{
    if (folder == QDir::homePath())
        return QStringLiteral("Home");
    return nameOf(folder);
}

QString count(qint64 n)
{
    return QLocale::system().toString(n);
}

QString items(qint64 n, const char *one, const char *many)
{
    return n == 1 ? QStringLiteral("1 %1").arg(QLatin1String(one))
                  : QStringLiteral("%1 %2").arg(count(n), QLatin1String(many));
}

QString bytes(qint64 n)
{
    return QLocale::system().formattedDataSize(n, 1, QLocale::DataSizeTraditionalFormat);
}

// "3 bytes, modified 12/11/2026 14:03", or "folder with 4 items, modified ..."
QString describe(const QString &path)
{
    const QFileInfo info(path);
    QString what;
    if (info.isSymLink())
        what = QStringLiteral("link to %1").arg(info.symLinkTarget());
    else if (info.isDir())
        what = QStringLiteral("folder with %1")
                   .arg(items(QDir(path).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden
                                                   | QDir::System).size(),
                              "item", "items"));
    else
        what = bytes(info.size());
    const QDateTime modified = info.lastModified();
    return QStringLiteral("%1, modified %2").arg(what, QLocale::system().toString(modified, QLocale::ShortFormat));
}

} // namespace

FileOps::FileOps(QObject *parent)
    : QObject(parent)
    , m_history(&m_queue)
{
    connect(&m_history, &Riley::UndoHistory::changed, this, &FileOps::historyChanged);
    connect(&m_history, &Riley::UndoHistory::refused, this, &FileOps::say);

    connect(&m_queue, &Riley::JobQueue::jobStarted, this, [this](FileJob *job) {
        m_running = job;
        m_runningFor.start();
        m_progressTimer.start();
        updateProgress();
    });
    connect(&m_queue, &Riley::JobQueue::jobFinished, this, &FileOps::jobEnded);

    m_progressTimer.setSingleShot(true);
    m_progressTimer.setInterval(progressDelayMs);
    connect(&m_progressTimer, &QTimer::timeout, this, &FileOps::updateProgress);

    m_messageTimer.setSingleShot(true);
    m_messageTimer.setInterval(messageMs);
    connect(&m_messageTimer, &QTimer::timeout, this, &FileOps::dismissMessage);

    // Finished jobs are let go once the queue is idle; the history keeps their steps.
    m_cleanup.setSingleShot(true);
    m_cleanup.setInterval(0);
    connect(&m_cleanup, &QTimer::timeout, this, [this] {
        if (m_queue.activeCount() == 0)
            m_queue.forgetFinished();
    });
}

FileOps::~FileOps()
{
    m_queue.stopAll();
}

QMimeData *FileOps::mimeFor(const QStringList &paths, bool cut)
{
    auto *data = new QMimeData;
    QList<QUrl> urls;
    QByteArray gnome = cut ? QByteArrayLiteral("cut") : QByteArrayLiteral("copy");
    for (const QString &path : paths) {
        const QUrl url = QUrl::fromLocalFile(path);
        urls.append(url);
        gnome += '\n' + url.toEncoded();
    }
    data->setUrls(urls); // text/uri-list
    data->setData(QString::fromLatin1(gnomeFilesType), gnome);
    data->setData(QString::fromLatin1(kdeCutType), cut ? QByteArrayLiteral("1") : QByteArrayLiteral("0"));
    data->setText(paths.join(QLatin1Char('\n')));
    return data;
}

QStringList FileOps::pathsIn(const QMimeData *data, bool *cut)
{
    *cut = false;
    QStringList paths;
    if (!data)
        return paths;
    const auto addUrl = [&paths](const QUrl &url) {
        if (url.isLocalFile() && !url.toLocalFile().isEmpty())
            paths.append(QDir::cleanPath(url.toLocalFile()));
    };
    if (data->hasFormat(QString::fromLatin1(gnomeFilesType))) {
        const QList<QByteArray> lines = data->data(QString::fromLatin1(gnomeFilesType)).split('\n');
        for (qsizetype i = 0; i < lines.size(); ++i) {
            const QByteArray line = lines.at(i).trimmed();
            if (i == 0 && (line == "cut" || line == "copy")) {
                *cut = line == "cut";
                continue;
            }
            if (!line.isEmpty())
                addUrl(QUrl::fromEncoded(line));
        }
        return paths;
    }
    if (data->hasUrls()) {
        const QList<QUrl> urls = data->urls();
        for (const QUrl &url : urls)
            addUrl(url);
        *cut = data->data(QString::fromLatin1(kdeCutType)) == "1";
        return paths;
    }
    // Plain text: absolute paths or file:// URLs, one per line, as a terminal would copy them.
    const QStringList lines = data->text().split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &raw : lines) {
        const QString line = raw.trimmed();
        if (line.startsWith(QLatin1String("file://")))
            addUrl(QUrl(line));
        else if (QDir::isAbsolutePath(line) && Riley::pathExists(line))
            paths.append(QDir::cleanPath(line));
        else
            return {}; // not a list of files
    }
    return paths;
}

void FileOps::copy(const QStringList &paths, bool cut)
{
    if (paths.isEmpty())
        return;
    QGuiApplication::clipboard()->setMimeData(mimeFor(paths, cut));
}

bool FileOps::paste(const QString &folder)
{
    bool cut = false;
    const QStringList paths = pathsIn(QGuiApplication::clipboard()->mimeData(), &cut);
    if (paths.isEmpty() || folder.isEmpty())
        return false;
    FileJob *job = nullptr;
    if (cut) {
        job = new Riley::MoveJob(paths, folder);
        m_cutPaths = paths;
    } else {
        job = new Riley::CopyJob(paths, folder);
    }
    connect(job, &FileJob::finished, this, [this, job, cut] {
        const QStringList targets = static_cast<Riley::TransferJob *>(job)->targets();
        const auto first = std::find_if(targets.cbegin(), targets.cend(), [](const QString &t) { return !t.isEmpty(); });
        if (first != targets.cend())
            emit land(*first, false);
        if (cut) {
            // The files are not where the clipboard says any more.
            bool stillCut = false;
            const QStringList held = pathsIn(QGuiApplication::clipboard()->mimeData(), &stillCut);
            if (stillCut && held == m_cutPaths && job->state() == FileJob::State::Done)
                QGuiApplication::clipboard()->clear();
            m_cutPaths.clear();
        }
    });
    run(job);
    return true;
}

void FileOps::trash(const QStringList &paths)
{
    if (paths.isEmpty())
        return;
    run(new Riley::TrashJob(paths));
}

void FileOps::newFolder(const QString &folder)
{
    if (folder.isEmpty())
        return;
    auto *job = new Riley::NewFolderJob(folder);
    connect(job, &FileJob::finished, this, [this, job] {
        const QString made = job->createdPath();
        if (!made.isEmpty())
            emit land(made, true);
    });
    run(job);
}

void FileOps::undo()
{
    FileJob *job = m_history.undo();
    if (!job) {
        say(QStringLiteral("Nothing to undo."));
        return;
    }
    watch(job);
    landAfter(job);
}

void FileOps::redo()
{
    FileJob *job = m_history.redo();
    if (!job) {
        say(QStringLiteral("Nothing to redo."));
        return;
    }
    watch(job);
    landAfter(job);
}

// The cursor goes to where the last thing put back now is.
void FileOps::landAfter(FileJob *job)
{
    connect(job, &FileJob::finished, this, [this, job] {
        const QList<Riley::JobStep> steps = job->steps();
        if (!steps.isEmpty())
            emit land(steps.last().to, false);
    });
}

void FileOps::startRename(const QString &path)
{
    if (path.isEmpty() || path == m_editingPath)
        return;
    m_editingPath = path;
    emit editingChanged();
}

QString FileOps::editingName() const
{
    return m_editingPath.isEmpty() ? QString() : QFileInfo(m_editingPath).fileName();
}

void FileOps::commitRename(const QString &name)
{
    if (!editing())
        return;
    const QString path = m_editingPath;
    const QString old = QFileInfo(path).fileName();
    if (name == old) {
        cancelRename();
        return;
    }
    if (name.isEmpty() || name.trimmed().isEmpty()) {
        say(QStringLiteral("A name cannot be empty."));
        return;
    }
    if (!Riley::isValidFileName(name)) {
        say(name.contains(QLatin1Char('/')) ? QStringLiteral("A name cannot hold “/”.")
                                            : QStringLiteral("%1 cannot be used as a name.").arg(quoted(name)));
        return;
    }
    const QString folder = QFileInfo(path).absolutePath();
    const QString target = QDir(folder).filePath(name);
    // A name that differs only in case is the same file on some drives; the job tells.
    if (Riley::pathExists(target) && name.compare(old, Qt::CaseInsensitive) != 0) {
        say(QStringLiteral("%1 is already taken in %2. Choose another name.")
                .arg(quoted(name), quoted(folderTitle(folder))));
        return;
    }
    m_editingPath.clear();
    emit editingChanged();

    auto *job = new Riley::RenameJob(path, name);
    connect(job, &FileJob::finished, this, [this, job] {
        if (job->failureCount() == 0)
            emit land(job->newPath(), false);
    });
    run(job);
}

void FileOps::cancelRename()
{
    if (!editing())
        return;
    m_editingPath.clear();
    emit editingChanged();
}

void FileOps::run(FileJob *job)
{
    watch(job);
    m_queue.start(job);
}

void FileOps::watch(FileJob *job)
{
    connect(job, &FileJob::progressChanged, this, &FileOps::updateProgress);
    connect(job, &FileJob::stateChanged, this, [this, job](FileJob::State state) {
        if (state != FileJob::State::Asking && m_asking == job) {
            m_asking = nullptr;
            emit clashChanged();
        }
        updateProgress();
    });
    connect(job, &FileJob::clashAsked, this, [this, job] { clashAsked(job); });
    connect(job, &FileJob::finished, this, [this, job] { reportFailures(job); });
}

void FileOps::jobEnded(FileJob *job)
{
    if (m_asking == job) {
        m_asking = nullptr;
        emit clashChanged();
    }
    if (m_running == job)
        m_running = nullptr;
    updateProgress();
    if (m_queue.activeCount() == 0)
        m_cleanup.start();
}

void FileOps::reportFailures(FileJob *job)
{
    const QList<Riley::JobFailure> failures = job->failures();
    // An undo's failures come through the history's refused().
    if (failures.isEmpty() || job->kind() == FileJob::Kind::Undo)
        return;
    if (failures.size() == 1)
        say(failures.first().message);
    else
        say(QStringLiteral("%1 (and %2 more that could not be done)")
                .arg(failures.first().message, count(failures.size() - 1)));
}

FileJob *FileOps::runningJob() const
{
    return m_running && !m_running->isFinished() ? m_running.data() : nullptr;
}

void FileOps::updateProgress()
{
    const FileJob *job = runningJob();
    bool show = false;
    if (job) {
        show = m_showProgress || m_runningFor.elapsed() >= progressDelayMs || job->filesTotal() >= bigJobFiles
               || job->bytesTotal() >= bigJobBytes;
    }
    m_showProgress = show;
    emit progressChanged();
}

QString FileOps::progressTitle() const
{
    const FileJob *job = runningJob();
    if (!job)
        return {};
    const qint64 files = job->filesTotal();
    QString what = files > 0 ? items(files, "item", "items") : QStringLiteral("items");
    if (const auto *transfer = qobject_cast<const Riley::TransferJob *>(job)) {
        const QString into = quoted(folderTitle(transfer->destination()));
        if (transfer->sources().size() == 1)
            what = quoted(nameOf(transfer->sources().first()));
        return job->kind() == FileJob::Kind::Copy ? QStringLiteral("Copying %1 to %2").arg(what, into)
                                                   : QStringLiteral("Moving %1 to %2").arg(what, into);
    }
    switch (job->kind()) {
    case FileJob::Kind::Trash:
        return QStringLiteral("Moving %1 to the trash").arg(what);
    case FileJob::Kind::Restore:
        return QStringLiteral("Restoring %1 from the trash").arg(what);
    case FileJob::Kind::Delete:
        return QStringLiteral("Deleting %1").arg(what);
    case FileJob::Kind::Undo:
        return static_cast<const Riley::UndoJob *>(job)->isRedo() ? QStringLiteral("Redoing")
                                                                    : QStringLiteral("Undoing");
    case FileJob::Kind::Rename:
        return QStringLiteral("Renaming");
    case FileJob::Kind::NewFolder:
        return QStringLiteral("Making a folder");
    case FileJob::Kind::Copy:
    case FileJob::Kind::Move:
        break;
    }
    return {};
}

QString FileOps::progressDetail() const
{
    const FileJob *job = runningJob();
    if (!job)
        return {};
    if (job->stopRequested())
        return QStringLiteral("Stopping…");
    QString text = QStringLiteral("%1 of %2").arg(count(job->filesDone()), count(job->filesTotal()));
    if (job->bytesTotal() > 0)
        text += QStringLiteral("  ·  %1 of %2").arg(bytes(job->bytesDone()), bytes(job->bytesTotal()));
    return text;
}

double FileOps::progressFraction() const
{
    const FileJob *job = runningJob();
    if (!job)
        return 0.0;
    if (job->bytesTotal() > 0)
        return std::clamp(double(job->bytesDone()) / double(job->bytesTotal()), 0.0, 1.0);
    if (job->filesTotal() > 0)
        return std::clamp(double(job->filesDone()) / double(job->filesTotal()), 0.0, 1.0);
    return 0.0;
}

bool FileOps::stopping() const
{
    const FileJob *job = runningJob();
    return job && job->stopRequested();
}

void FileOps::stop()
{
    m_queue.stopAll();
    updateProgress();
}

void FileOps::clashAsked(FileJob *job)
{
    m_asking = job;
    m_clashButton = KeepBothButton;
    m_clashForAll = false;
    emit clashChanged();
}

QString FileOps::clashTitle() const
{
    if (!m_asking)
        return {};
    const QString target = m_asking->clashTarget();
    const QString kind = QFileInfo(target).isDir() && !QFileInfo(target).isSymLink() ? QStringLiteral("A folder")
                                                                                      : QStringLiteral("A file");
    return QStringLiteral("%1 named %2 is already in %3")
        .arg(kind, quoted(nameOf(target)), quoted(folderTitle(QFileInfo(target).absolutePath())));
}

QString FileOps::clashOld() const
{
    return m_asking ? describe(m_asking->clashTarget()) : QString();
}

QString FileOps::clashNew() const
{
    return m_asking ? describe(m_asking->clashSource()) : QString();
}

void FileOps::setClashButton(int button)
{
    button = std::clamp(button, int(SkipButton), int(ReplaceButton));
    if (button == m_clashButton)
        return;
    m_clashButton = button;
    emit clashChanged();
}

void FileOps::setClashForAll(bool on)
{
    if (on == m_clashForAll)
        return;
    m_clashForAll = on;
    emit clashChanged();
}

bool FileOps::clashOffersForAll() const
{
    const auto *transfer = qobject_cast<const Riley::TransferJob *>(m_asking.data());
    return transfer && transfer->sources().size() > 1;
}

void FileOps::answerClash(int button)
{
    if (!m_asking)
        return;
    if (button < 0)
        button = m_clashButton;
    const FileJob::ClashChoice choice = button == ReplaceButton    ? FileJob::ClashChoice::Replace
                                        : button == KeepBothButton ? FileJob::ClashChoice::KeepBoth
                                                                   : FileJob::ClashChoice::Skip;
    FileJob *job = m_asking;
    const bool forAll = m_clashForAll && clashOffersForAll();
    m_asking = nullptr;
    m_clashButton = KeepBothButton;
    m_clashForAll = false;
    emit clashChanged();
    job->answerClash(choice, forAll);
}

bool FileOps::clashKey(int key, Qt::KeyboardModifiers mods)
{
    if (!asking())
        return false;
    switch (key) {
    case Qt::Key_Left:
    case Qt::Key_Backtab:
        setClashButton(m_clashButton - 1);
        break;
    case Qt::Key_Tab:
        if (mods & Qt::ShiftModifier)
            setClashButton(m_clashButton - 1);
        else
            setClashButton(m_clashButton + 1);
        break;
    case Qt::Key_Right:
        setClashButton(m_clashButton + 1);
        break;
    case Qt::Key_Return:
    case Qt::Key_Enter:
    case Qt::Key_Space:
        answerClash();
        break;
    case Qt::Key_S:
    case Qt::Key_Escape:
        // Leaving the question is the answer that changes nothing.
        answerClash(SkipButton);
        break;
    case Qt::Key_K:
        answerClash(KeepBothButton);
        break;
    case Qt::Key_R:
        answerClash(ReplaceButton);
        break;
    case Qt::Key_A:
        if (clashOffersForAll())
            setClashForAll(!m_clashForAll);
        break;
    default:
        break;
    }
    return true;
}

void FileOps::say(const QString &message)
{
    m_message = message;
    m_messageTimer.start();
    emit messageChanged();
}

void FileOps::dismissMessage()
{
    if (m_message.isEmpty())
        return;
    m_message.clear();
    m_messageTimer.stop();
    emit messageChanged();
}
