// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

#include "gitlog.h"

#include <QDir>
#include <QFileInfo>
#include <QMutex>
#include <QMutexLocker>
#include <QProcess>
#include <QProcessEnvironment>

namespace Riley {

namespace {

// How many answers are kept, over every repository.
constexpr int KeptAnswers = 32;
// How many runs are remembered for tests.
constexpr int KeptRuns = 10000;

const QChar CommitMark(0x1e); // starts each commit's line in the log
const QChar FieldMark(0x1f);  // between the fields of that line

QMutex &recordLock()
{
    static QMutex lock;
    return lock;
}

QList<GitLog::Run> &recordedRuns()
{
    static QList<GitLog::Run> runs;
    return runs;
}

QSet<Qt::HANDLE> &recordedThreads()
{
    static QSet<Qt::HANDLE> threads;
    return threads;
}

QMutex &cacheLock()
{
    static QMutex lock;
    return lock;
}

QHash<QString, GitLog::Info> &cache()
{
    static QHash<QString, GitLog::Info> answers;
    return answers;
}

QProcessEnvironment gitEnvironment()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    // The repository is the one the folder is in, whatever the program was started with.
    for (const char *name : {"GIT_DIR", "GIT_WORK_TREE", "GIT_INDEX_FILE", "GIT_OBJECT_DIRECTORY",
                             "GIT_ALTERNATE_OBJECT_DIRECTORIES", "GIT_NAMESPACE", "GIT_PREFIX"})
        env.remove(QString::fromLatin1(name));
    // Paths given are names, never patterns; nothing waits for a password; no lock is taken.
    env.insert(QStringLiteral("GIT_LITERAL_PATHSPECS"), QStringLiteral("1"));
    env.insert(QStringLiteral("GIT_TERMINAL_PROMPT"), QStringLiteral("0"));
    env.insert(QStringLiteral("GIT_OPTIONAL_LOCKS"), QStringLiteral("0"));
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C"));
    return env;
}

// Runs git with these arguments, as a list, and gives what it wrote; false when it could not be
// run, failed, or `stop` said to give up.
bool runGit(const QStringList &arguments, const std::function<bool()> &stop, QByteArray &out)
{
    const QString program = QStringLiteral("git");
    {
        QMutexLocker locker(&recordLock());
        recordedThreads().insert(QThread::currentThreadId());
        if (recordedRuns().size() < KeptRuns)
            recordedRuns().append({program, arguments});
    }
    QProcess git;
    git.setProcessEnvironment(gitEnvironment());
    git.setStandardInputFile(QProcess::nullDevice());
    git.setProcessChannelMode(QProcess::SeparateChannels);
    git.setReadChannel(QProcess::StandardOutput);
    git.start(program, arguments, QIODevice::ReadOnly);
    if (!git.waitForStarted(5000))
        return false;
    out.clear();
    while (git.state() != QProcess::NotRunning) {
        if (stop && stop()) {
            git.kill();
            git.waitForFinished(1000);
            return false;
        }
        git.waitForReadyRead(100);
        out += git.readAllStandardOutput();
        git.readAllStandardError(); // dropped, so the pipe never fills
    }
    out += git.readAllStandardOutput();
    return git.exitStatus() == QProcess::NormalExit && git.exitCode() == 0;
}

GitCommit commitFrom(const QString &line, QString *id)
{
    const QStringList fields = line.mid(1).split(FieldMark);
    GitCommit commit;
    if (fields.size() < 4)
        return commit;
    if (id)
        *id = fields.at(0);
    bool ok = false;
    const qint64 seconds = fields.at(1).toLongLong(&ok);
    if (ok)
        commit.when = QDateTime::fromSecsSinceEpoch(seconds);
    commit.author = fields.at(2);
    commit.subject = fields.mid(3).join(FieldMark);
    return commit;
}

// Reads the log of the folder (`prefix` its path in the repository, "" at the top) and gives
// every path below the folder the newest commit that touched it.
bool readLog(GitFolderInfo &info, const QString &prefix, const std::function<bool()> &stop)
{
    QString pathspec = prefix;
    while (pathspec.endsWith(QLatin1Char('/')))
        pathspec.chop(1);
    if (pathspec.isEmpty())
        pathspec = QStringLiteral(".");
    QByteArray out;
    const QStringList arguments{
        QStringLiteral("-C"), info.root,
        QStringLiteral("-c"), QStringLiteral("log.showSignature=false"),
        QStringLiteral("log"), QStringLiteral("-z"), QStringLiteral("--name-only"),
        QStringLiteral("--no-color"), QStringLiteral("--encoding=UTF-8"),
        QStringLiteral("--format=%x1e%H%x1f%at%x1f%an%x1f%s"),
        QStringLiteral("--"), pathspec,
    };
    if (!runGit(arguments, stop, out))
        return false;

    const QString base = info.folder.endsWith(QLatin1Char('/')) ? info.folder : info.folder + QLatin1Char('/');
    GitCommit current;
    bool firstName = false;
    qsizetype start = 0;
    const qsizetype size = out.size();
    while (start < size) {
        qsizetype end = out.indexOf('\0', start);
        if (end < 0)
            end = size;
        QString token = QString::fromUtf8(out.constData() + start, end - start);
        start = end + 1;
        if (token.startsWith(CommitMark)) {
            current = commitFrom(token, nullptr);
            if (!info.latest.isValid())
                info.latest = current;
            firstName = true;
            continue;
        }
        // git puts a line break between a commit's line and its first name.
        if (firstName && token.startsWith(QLatin1Char('\n')))
            token.remove(0, 1);
        firstName = false;
        if (token.isEmpty() || !current.isValid() || !token.startsWith(prefix))
            continue;
        const QString relative = token.mid(prefix.size());
        // The file, and each folder it is in below the folder asked about.
        qsizetype slash = relative.indexOf(QLatin1Char('/'));
        while (true) {
            const QString part = slash < 0 ? relative : relative.left(slash);
            const QString key = base + part;
            if (!info.byPath.contains(key))
                info.byPath.insert(key, current);
            if (slash < 0)
                break;
            slash = relative.indexOf(QLatin1Char('/'), slash + 1);
        }
        if (stop && (info.byPath.size() & 1023) == 0 && stop())
            return false;
    }
    return true;
}

} // namespace

QString GitLog::repositoryOf(const QString &folder)
{
    if (folder.isEmpty())
        return {};
    QString at = QDir::cleanPath(QFileInfo(folder).absoluteFilePath());
    while (true) {
        if (QFileInfo::exists(at + QStringLiteral("/.git")))
            return at;
        if (at == QLatin1String("/") || at.isEmpty())
            return {};
        const qsizetype slash = at.lastIndexOf(QLatin1Char('/'));
        at = slash <= 0 ? QStringLiteral("/") : at.left(slash);
    }
}

GitLog::Info GitLog::read(const QString &folder, const std::function<bool()> &stop)
{
    auto info = std::make_shared<GitFolderInfo>();
    info->folder = QDir::cleanPath(folder);
    // Most folders are in no repository: that is known without running git.
    if (folder.isEmpty() || repositoryOf(info->folder).isEmpty() || !QFileInfo(info->folder).isDir())
        return info;

    QByteArray out;
    QString prefix;
    if (runGit({QStringLiteral("-C"), info->folder, QStringLiteral("rev-parse"), QStringLiteral("--show-toplevel"),
                QStringLiteral("--show-prefix"), QStringLiteral("HEAD"), QStringLiteral("--symbolic-full-name"),
                QStringLiteral("HEAD")},
               stop, out)) {
        const QStringList lines = QString::fromUtf8(out).split(QLatin1Char('\n'));
        if (lines.size() < 4)
            return info;
        info->root = lines.at(0);
        prefix = lines.at(1);
        info->head = lines.at(2);
        const QString ref = lines.at(3);
        if (ref.startsWith(QLatin1String("refs/heads/")))
            info->branch = ref.mid(11);
        else
            info->branch = info->head.left(7);
    } else {
        // A repository with no commit yet, or none at all.
        if (stop && stop())
            return std::make_shared<GitFolderInfo>();
        if (!runGit({QStringLiteral("-C"), info->folder, QStringLiteral("rev-parse"),
                     QStringLiteral("--show-toplevel")},
                    stop, out))
            return info;
        info->root = QString::fromUtf8(out).trimmed();
        if (runGit({QStringLiteral("-C"), info->root, QStringLiteral("symbolic-ref"), QStringLiteral("-q"),
                    QStringLiteral("--short"), QStringLiteral("HEAD")},
                   stop, out))
            info->branch = QString::fromUtf8(out).trimmed();
        info->inRepository = !info->root.isEmpty();
        return info;
    }
    if (info->root.isEmpty())
        return info;
    info->inRepository = true;

    const QString key = info->root + QLatin1Char('\n') + info->head + QLatin1Char('\n') + info->folder;
    {
        QMutexLocker locker(&cacheLock());
        const Info kept = cache().value(key);
        if (kept) {
            if (kept->branch == info->branch)
                return kept;
            auto renamed = std::make_shared<GitFolderInfo>(*kept);
            renamed->branch = info->branch;
            return renamed;
        }
    }

    if (!readLog(*info, prefix, stop))
        return std::make_shared<GitFolderInfo>();

    QMutexLocker locker(&cacheLock());
    if (cache().size() >= KeptAnswers)
        cache().clear();
    cache().insert(key, info);
    return info;
}

void GitLog::clearCache()
{
    QMutexLocker locker(&cacheLock());
    cache().clear();
}

QList<GitLog::Run> GitLog::runs()
{
    QMutexLocker locker(&recordLock());
    return recordedRuns();
}

QSet<Qt::HANDLE> GitLog::threads()
{
    QMutexLocker locker(&recordLock());
    return recordedThreads();
}

} // namespace Riley
