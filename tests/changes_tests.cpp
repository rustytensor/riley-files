// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Marc Mironescu

// The change helper (riley-files-changes) on real files and real sockets: its permission filter,
// what it does with bad clients, fanotify on a real file system (as root only; the gate runs that
// one test as root), and the index following the changes it reports.

#include "changeclient.h"
#include "changefollower.h"
#include "changeserver.h"
#include "eventsource.h"
#include "fanotifysource.h"
#include "indexdatabase.h"
#include "indexer.h"
#include "indexsettings.h"
#include "permissions.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTest>

#include <algorithm>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <poll.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <thread>
#include <unistd.h>

using namespace Riley;

class ChangesTests : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void helperOnlyRevealsReadablePaths();
    void helperProtocolIsSafe();
    void helperWatchesAFilesystem();
    void indexFollowsChanges();

private:
    std::unique_ptr<QTemporaryDir> m_tmp;
    QString m_root;   // fixtures, mode 0755
    std::string m_socket;
};

namespace {

std::string s(const QString &path)
{
    return QFile::encodeName(path).toStdString();
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

// A client of the helper and everything it has received. When `server` is given the test runs
// the server itself, between reads (so it decides exactly when an event is checked); otherwise
// the server runs on a thread of its own.
struct Peer
{
    ChangeClient client;
    std::vector<HelperMessage> got;
    bool open = true;

    bool connect(const std::string &socket) { return client.connectTo(socket); }

    void pump(ChangeServer *server, int ms)
    {
        if (server) {
            server->runOnce(ms);
        } else {
            pollfd p{client.fd(), POLLIN, 0};
            ::poll(&p, 1, ms);
        }
        if (open)
            open = client.receive(got);
    }

    // Waits until `done` holds for what was received, or the time is over.
    bool waitFor(ChangeServer *server, const std::function<bool()> &done, int timeoutMs = 3000)
    {
        QElapsedTimer timer;
        timer.start();
        while (!done()) {
            if (timer.elapsed() > timeoutMs)
                return false;
            pump(server, 5);
        }
        return true;
    }

    std::size_t count(HelperMessage::Type type) const
    {
        return std::size_t(std::count_if(got.begin(), got.end(), [&](const HelperMessage &m) { return m.type == type; }));
    }

    // Sends a ping and waits for its answer: whatever the server had to send before is in `got`.
    bool sync(ChangeServer *server)
    {
        const std::size_t pongs = count(HelperMessage::Pong);
        return client.ping() && waitFor(server, [&] { return count(HelperMessage::Pong) > pongs; });
    }

    QStringList changes() const
    {
        QStringList out;
        for (const HelperMessage &m : got)
            if (m.type == HelperMessage::Change)
                out.append(QString::fromLatin1(kindName(m.event.kind)) + u' ' + (m.event.folder ? u"folder " : u"file ")
                           + QFile::decodeName(m.event.path.c_str()));
        return out;
    }

    // The replies other than changes, as "ok subscribe <path>", "error <reason> [<path>]", "pong".
    QStringList replies() const
    {
        QStringList out;
        for (const HelperMessage &m : got) {
            QString line;
            switch (m.type) {
            case HelperMessage::Ok:
                line = QStringLiteral("ok ") + QString::fromStdString(m.what) + u' ' + QFile::decodeName(m.path.c_str());
                break;
            case HelperMessage::Error:
                line = QStringLiteral("error ") + QString::fromStdString(m.what);
                if (!m.path.empty())
                    line += u' ' + QFile::decodeName(m.path.c_str());
                break;
            case HelperMessage::Pong:
                line = QStringLiteral("pong");
                break;
            default:
                continue;
            }
            out.append(line);
        }
        return out;
    }
};

struct DiskRow
{
    QString path;
    bool folder = false;
    qint64 size = 0;
    bool operator==(const DiskRow &) const = default;
};

QList<DiskRow> diskRows(const QString &root, const IndexSettings &settings)
{
    QList<DiskRow> rows;
    QStringList folders{root};
    while (!folders.isEmpty()) {
        const QString folder = folders.takeLast();
        const QFileInfoList list = QDir(folder).entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System
                                                              | QDir::NoDotAndDotDot);
        for (const QFileInfo &info : list) {
            const QString path = folder + u'/' + info.fileName();
            if (settings.isExcluded(info.fileName(), path))
                continue;
            const bool isDir = info.isDir() && !info.isSymLink();
            rows.append({path, isDir, isDir ? 0 : info.size()});
            if (isDir)
                folders.append(path);
        }
    }
    std::sort(rows.begin(), rows.end(), [](const DiskRow &a, const DiskRow &b) { return a.path < b.path; });
    return rows;
}

QList<DiskRow> indexRows(IndexDatabase &db)
{
    QList<DiskRow> rows;
    for (const IndexEntry &entry : db.entries())
        rows.append({entry.path, entry.folder, entry.size});
    return rows;
}

QString describe(const QList<DiskRow> &rows)
{
    QStringList out;
    for (const DiskRow &row : rows)
        out.append(row.path + (row.folder ? u"/" : u"") + u' ' + QString::number(row.size));
    return out.join(u", ");
}

// Someone who is neither the test's user nor root, and in none of its groups.
Credentials stranger()
{
    Credentials who;
    who.uid = ::getuid() == 65534 ? 65533 : 65534;
    who.gid = ::getgid() == 65534 ? 65533 : 65534;
    return who;
}

} // namespace

void ChangesTests::init()
{
    m_tmp = std::make_unique<QTemporaryDir>();
    QVERIFY(m_tmp->isValid());
    QCOMPARE(::chmod(QFile::encodeName(m_tmp->path()).constData(), 0755), 0);
    m_root = m_tmp->path() + QStringLiteral("/root");
    QVERIFY(QDir().mkpath(m_root));
    m_socket = s(m_tmp->path() + QStringLiteral("/changes.sock"));
    const QString home = m_tmp->path() + QStringLiteral("/home");
    QVERIFY(QDir().mkpath(home));
    qputenv("HOME", QFile::encodeName(home));
    qputenv("XDG_DATA_HOME", QFile::encodeName(m_tmp->path() + QStringLiteral("/data")));
    qputenv("XDG_CONFIG_HOME", QFile::encodeName(m_tmp->path() + QStringLiteral("/config")));
    qputenv("XDG_CACHE_HOME", QFile::encodeName(m_tmp->path() + QStringLiteral("/cache")));
}

// For a subscriber with uid U: a change under another user's 0700 folder is not handed on, under
// a 0755 folder it is, and a path made unreadable between the event and the check is not.
void ChangesTests::helperOnlyRevealsReadablePaths()
{
    if (::getuid() == 0)
        QSKIP("run as an ordinary user: root may read every folder");
    const QString priv = m_root + QStringLiteral("/private");
    const QString pub = m_root + QStringLiteral("/public");
    const QString pub2 = m_root + QStringLiteral("/public2");
    QVERIFY(writeFile(priv + QStringLiteral("/a.txt"), "a"));
    QVERIFY(writeFile(pub + QStringLiteral("/a.txt"), "a"));
    QVERIFY(QDir().mkpath(pub2));
    QCOMPARE(::chmod(s(m_root).c_str(), 0755), 0);
    QCOMPARE(::chmod(s(priv).c_str(), 0700), 0);
    QCOMPARE(::chmod(s(pub).c_str(), 0755), 0);
    QCOMPARE(::chmod(s(pub2).c_str(), 0755), 0);

    // The filter itself. The fixtures belong to the test's user; U is someone else.
    const Credentials u = stranger();
    Credentials me;
    me.uid = ::getuid();
    me.gid = ::getgid();
    QVERIFY(!mayReveal(s(priv + QStringLiteral("/a.txt")), u));
    QVERIFY(mayReveal(s(pub + QStringLiteral("/a.txt")), u));
    QVERIFY(mayReveal(s(priv + QStringLiteral("/a.txt")), me));
    QVERIFY(!mayWatch(s(priv), u));
    QVERIFY(mayWatch(s(pub), u));
    // A group that may read: 0750 with the folder's group among U's groups.
    const QString team = m_root + QStringLiteral("/team");
    QVERIFY(writeFile(team + QStringLiteral("/t.txt"), "t"));
    QCOMPARE(::chmod(s(team).c_str(), 0750), 0);
    struct stat st;
    QCOMPARE(::stat(s(team).c_str(), &st), 0);
    Credentials member = u;
    member.groups = {st.st_gid};
    QVERIFY(!mayReveal(s(team + QStringLiteral("/t.txt")), u));
    QVERIFY(mayReveal(s(team + QStringLiteral("/t.txt")), member));
    // A link on the way is never followed.
    QVERIFY(QFile::link(pub, m_root + QStringLiteral("/link")));
    QVERIFY(!mayReveal(s(m_root + QStringLiteral("/link/a.txt")), u));

    // Through the server and the protocol, the client being U.
    TestEventSource source;
    ChangeServer server(source, m_socket, ChangeServer::Limits{}, [u](int, Credentials &out) {
        out = u;
        return true;
    });
    std::string error;
    QVERIFY2(server.listen(error), error.c_str());
    Peer peer;
    QVERIFY(peer.connect(m_socket));
    QVERIFY(peer.client.subscribe(s(m_root)));
    QVERIFY(peer.waitFor(&server, [&] { return peer.count(HelperMessage::Ok) == 1; }));
    QCOMPARE(peer.replies(), QStringList{QStringLiteral("ok subscribe ") + m_root});

    source.push(ChangeKind::Created, s(priv + QStringLiteral("/a.txt")));
    source.push(ChangeKind::Created, s(pub + QStringLiteral("/a.txt")));
    source.push(ChangeKind::Written, s(priv + QStringLiteral("/a.txt")));
    QVERIFY(peer.sync(&server));
    QCOMPARE(peer.changes(), QStringList{QStringLiteral("created file ") + pub + QStringLiteral("/a.txt")});

    // The event is taken while public/ is open to U; before it is checked, public/ is closed.
    peer.got.clear();
    QVERIFY(writeFile(pub + QStringLiteral("/b.txt"), "b"));
    source.push(ChangeKind::Created, s(pub + QStringLiteral("/b.txt")));
    QCOMPARE(::chmod(s(pub).c_str(), 0700), 0);
    source.push(ChangeKind::Created, s(pub2 + QStringLiteral("/c.txt")));
    QVERIFY(peer.sync(&server));
    QCOMPARE(peer.changes(), QStringList{QStringLiteral("created file ") + pub2 + QStringLiteral("/c.txt")});
    ::chmod(s(pub).c_str(), 0755);
}

// Lines over 64 KiB, malformed messages, unknown commands and a client that stops reading are
// ignored or dropped; the helper keeps serving the others; a folder the peer (by SO_PEERCRED)
// cannot read is never subscribed.
void ChangesTests::helperProtocolIsSafe()
{
    if (::getuid() == 0)
        QSKIP("run as an ordinary user: root may read every folder");
    const QString watch = m_root + QStringLiteral("/watch");
    const QString flood = m_root + QStringLiteral("/flood");
    const QString locked = m_root + QStringLiteral("/locked");
    const QString closed = m_root + QStringLiteral("/closed");
    QVERIFY(QDir().mkpath(watch));
    QVERIFY(QDir().mkpath(flood));
    QVERIFY(QDir().mkpath(locked));
    QVERIFY(QDir().mkpath(closed + QStringLiteral("/inner")));
    QVERIFY(QFile::link(locked, m_root + QStringLiteral("/link")));
    QCOMPARE(::chmod(s(locked).c_str(), 0000), 0);
    QCOMPARE(::chmod(s(closed).c_str(), 0600), 0);

    TestEventSource source;
    ChangeServer server(source, m_socket);
    std::string error;
    QVERIFY2(server.listen(error), error.c_str());
    struct stat socketStat;
    QCOMPARE(::stat(m_socket.c_str(), &socketStat), 0);
    QCOMPARE(int(socketStat.st_mode & 0777), 0666); // anyone may connect; what each gets is filtered

    // A, a well-behaved subscriber.
    Peer a;
    QVERIFY(a.connect(m_socket));
    QVERIFY(a.client.subscribe(s(watch)));
    QVERIFY(a.waitFor(&server, [&] { return a.count(HelperMessage::Ok) == 1; }));

    // B sends a line of 70 KiB, then a ping: the line is thrown away with one error.
    Peer b;
    QVERIFY(b.connect(m_socket));
    QVERIFY(b.client.sendLine("subscribe " + std::string(70 * 1024, 'x')));
    QVERIFY(b.client.sendLine(std::string(5000, '/')));
    QVERIFY(b.sync(&server));
    QCOMPARE(b.replies(), (QStringList{QStringLiteral("error too-long"), QStringLiteral("error too-long"),
                                       QStringLiteral("pong")}));
    QVERIFY(b.open);

    // C sends malformed lines and unknown commands; each gets one error, nothing else happens.
    Peer c;
    QVERIFY(c.connect(m_socket));
    const std::vector<std::string> bad = {
        "subscribe relative/path",
        "subscribe /bad%zzescape",
        "subscribe " + s(watch) + " uid=0",
        "",
        "subscribe  " + s(watch),
        "subscribe",
        "ping now",
        "subscribe /a/../" + s(locked).substr(1),
        std::string("sub\0scribe /", 12),
        "frobnicate /",
        "SUBSCRIBE " + s(watch),
    };
    for (const std::string &line : bad)
        QVERIFY(c.client.sendLine(line));
    QVERIFY(c.sync(&server));
    const QStringList expectedC = {
        QStringLiteral("error malformed"),       QStringLiteral("error malformed"), QStringLiteral("error malformed"),
        QStringLiteral("error malformed"),       QStringLiteral("error malformed"), QStringLiteral("error malformed"),
        QStringLiteral("error malformed"),       QStringLiteral("error malformed"), QStringLiteral("error unknown-command"),
        QStringLiteral("error unknown-command"), QStringLiteral("error unknown-command"), QStringLiteral("pong"),
    };
    QCOMPARE(c.replies(), expectedC);

    // Folders the peer cannot read, whatever it claims: the server knows it only by SO_PEERCRED.
    c.got.clear();
    QStringList refusedFolders = {locked, closed + QStringLiteral("/inner"), m_root + QStringLiteral("/link"),
                                  m_root + QStringLiteral("/missing")};
    if (::access("/root", R_OK | X_OK) != 0)
        refusedFolders.append(QStringLiteral("/root"));
    for (const QString &folder : refusedFolders)
        QVERIFY(c.client.subscribe(s(folder)));
    QVERIFY(c.sync(&server));
    QStringList expectedRefusals;
    for (const QString &folder : refusedFolders)
        expectedRefusals.append(QStringLiteral("error not-readable ") + folder);
    expectedRefusals.append(QStringLiteral("pong"));
    QCOMPARE(c.replies(), expectedRefusals);
    QVERIFY(source.watched().size() == 1); // only A's folder was ever watched
    // Events in a refused folder never reach C.
    source.push(ChangeKind::Created, s(locked + QStringLiteral("/x")));
    QVERIFY(c.sync(&server));
    QVERIFY(c.changes().isEmpty());

    // D connects without ever sending a thing; E subscribes to flood/ and stops reading.
    Peer d;
    QVERIFY(d.connect(m_socket));
    Peer e;
    QVERIFY(e.connect(m_socket));
    QVERIFY(e.client.subscribe(s(flood)));
    QVERIFY(e.waitFor(&server, [&] { return e.count(HelperMessage::Ok) == 1; }));
    QTRY_COMPARE_WITH_TIMEOUT((server.runOnce(5), server.clientCount()), std::size_t(5), 2000);
    const std::string longName(200, 'n');
    for (int i = 0; i < 20000; ++i)
        source.push(ChangeKind::Created, s(flood) + '/' + longName + std::to_string(i));
    QElapsedTimer timer;
    timer.start();
    while (server.droppedCount() == 0 && timer.elapsed() < 10000)
        server.runOnce(5);
    QCOMPARE(server.droppedCount(), std::size_t(1));
    QCOMPARE(server.clientCount(), std::size_t(4));
    // E finds its connection closed once it reads what had reached it.
    QVERIFY(e.waitFor(&server, [&] { return !e.open; }, 10000));
    QVERIFY(e.changes().size() < 20000);

    // The helper goes on serving the others, and new clients.
    source.push(ChangeKind::Created, s(watch + QStringLiteral("/after.txt")));
    QVERIFY(a.waitFor(&server, [&] { return !a.changes().isEmpty(); }));
    QCOMPARE(a.changes(), QStringList{QStringLiteral("created file ") + watch + QStringLiteral("/after.txt")});
    Peer f;
    QVERIFY(f.connect(m_socket));
    QVERIFY(f.client.subscribe(s(watch)));
    QVERIFY(f.waitFor(&server, [&] { return f.count(HelperMessage::Ok) == 1; }));
    source.push(ChangeKind::Deleted, s(watch + QStringLiteral("/after.txt")));
    QVERIFY(f.waitFor(&server, [&] { return !f.changes().isEmpty(); }));
    QCOMPARE(f.changes(), QStringList{QStringLiteral("deleted file ") + watch + QStringLiteral("/after.txt")});
    QVERIFY(b.sync(&server));
    QVERIFY(d.client.ping());
    QVERIFY(d.waitFor(&server, [&] { return d.count(HelperMessage::Pong) == 1; }));

    ::chmod(s(locked).c_str(), 0755);
    ::chmod(s(closed).c_str(), 0755);
}

// As root: fanotify on a test file system (a tmpfs mounted for the test); creating, renaming and
// deleting files there gives the matching events with full paths within 1 s each.
void ChangesTests::helperWatchesAFilesystem()
{
    if (::getuid() != 0)
        QSKIP("needs root (fanotify); the gate runs this test as root");

    FanotifySource source;
    std::string error;
    QVERIFY2(source.isValid(&error), error.c_str());

    const QString mountPoint = m_tmp->path() + QStringLiteral("/fs");
    QVERIFY(QDir().mkpath(mountPoint));
    const bool mounted = ::mount("riley-test", s(mountPoint).c_str(), "tmpfs", 0, "size=8m,mode=0755") == 0;
    if (!mounted)
        qInfo("no tmpfs (%s): watching the temporary folder's own file system", std::strerror(errno));
    struct Unmount
    {
        bool on;
        std::string at;
        ~Unmount()
        {
            if (on)
                ::umount2(at.c_str(), MNT_DETACH);
        }
    } unmount{mounted, s(mountPoint)};

    const QString dir = mountPoint + QStringLiteral("/watched");
    const QString other = mountPoint + QStringLiteral("/other");
    QVERIFY(QDir().mkpath(dir));
    QVERIFY(QDir().mkpath(other));

    ChangeServer server(source, m_socket);
    QVERIFY2(server.listen(error), error.c_str());
    Peer peer;
    QVERIFY(peer.connect(m_socket));
    QVERIFY(peer.client.subscribe(s(dir)));
    QVERIFY(peer.waitFor(&server, [&] { return peer.count(HelperMessage::Ok) == 1; }));
    QCOMPARE(peer.replies(), QStringList{QStringLiteral("ok subscribe ") + dir});

    auto expect = [&](const QStringList &wanted) {
        QElapsedTimer timer;
        timer.start();
        const bool ok = peer.waitFor(&server, [&] {
            const QStringList got = peer.changes();
            return std::all_of(wanted.begin(), wanted.end(), [&](const QString &w) { return got.contains(w); });
        }, 1000);
        qInfo("%s: %lld ms", qPrintable(wanted.join(u", ")), timer.elapsed());
        return ok;
    };
    const QString a = dir + QStringLiteral("/a.txt");
    const QString b = dir + QStringLiteral("/b.txt");

    QVERIFY(writeFile(a, "alpha"));
    QVERIFY2(expect({QStringLiteral("created file ") + a}), qPrintable(peer.changes().join(u'\n')));
    QVERIFY(QFile::rename(a, b));
    QVERIFY2(expect({QStringLiteral("moved-from file ") + a, QStringLiteral("moved-to file ") + b}),
             qPrintable(peer.changes().join(u'\n')));
    QVERIFY(QFile::remove(b));
    QVERIFY2(expect({QStringLiteral("deleted file ") + b}), qPrintable(peer.changes().join(u'\n')));
    QVERIFY(QDir().mkdir(dir + QStringLiteral("/sub")));
    QVERIFY2(expect({QStringLiteral("created folder ") + dir + QStringLiteral("/sub")}),
             qPrintable(peer.changes().join(u'\n')));
    QVERIFY(writeFile(dir + QStringLiteral("/sub/c.txt"), "c"));
    QVERIFY2(expect({QStringLiteral("created file ") + dir + QStringLiteral("/sub/c.txt")}),
             qPrintable(peer.changes().join(u'\n')));

    // Changes on the same file system outside the subscribed folder are not handed on.
    QVERIFY(writeFile(other + QStringLiteral("/x.txt"), "x"));
    QTest::qWait(200);
    QVERIFY(peer.sync(&server));
    for (const QString &line : peer.changes())
        QVERIFY2(line.contains(dir + u'/'), qPrintable(line));
}

// Changes fed through the helper's protocol from a test event source: create x.txt, rename it to
// y.txt, delete y.txt, create folder Z with 3 files -> after each the index matches the disk
// within 3 s.
void ChangesTests::indexFollowsChanges()
{
    QVERIFY(writeFile(m_root + QStringLiteral("/a/one.txt"), "1"));
    QVERIFY(writeFile(m_root + QStringLiteral("/b.txt"), "bb"));
    IndexSettings settings = IndexSettings::defaults();
    settings.folders = {m_root};

    IndexDatabase db;
    QVERIFY2(db.open(IndexDatabase::defaultFile()), qPrintable(db.errorString()));
    Indexer indexer(db, settings);
    indexer.start();
    QCOMPARE(indexRows(db), diskRows(m_root, settings));

    TestEventSource source;
    ChangeServer server(source, m_socket);
    std::string error;
    QVERIFY2(server.listen(error), error.c_str());
    std::thread thread([&] { server.run(); });
    struct Join
    {
        ChangeServer &server;
        std::thread &thread;
        ~Join()
        {
            server.stop();
            thread.join();
        }
    } join{server, thread};

    ChangeFollower follower(indexer, QFile::decodeName(m_socket.c_str()));
    follower.start();
    QTRY_VERIFY_WITH_TIMEOUT(follower.isSubscribed(), 3000);
    QCOMPARE(source.watched(), std::vector<std::string>{s(m_root)});

    auto matches = [&](const char *step) {
        QElapsedTimer timer;
        timer.start();
        while (timer.elapsed() < 3000 && indexRows(db) != diskRows(m_root, settings))
            QTest::qWait(20);
        const bool ok = indexRows(db) == diskRows(m_root, settings);
        qInfo("%s: index matched the disk after %lld ms", step, timer.elapsed());
        if (!ok)
            qWarning("disk: %s\nindex: %s", qPrintable(describe(diskRows(m_root, settings))),
                     qPrintable(describe(indexRows(db))));
        return ok;
    };
    const QString x = m_root + QStringLiteral("/x.txt");
    const QString y = m_root + QStringLiteral("/y.txt");

    QVERIFY(writeFile(x, "xxx"));
    source.push(ChangeKind::Created, s(x));
    source.push(ChangeKind::Written, s(x));
    QVERIFY(matches("create x.txt"));
    QVERIFY(db.nameQuery(QStringLiteral("x.txt")).contains(x));

    QVERIFY(QFile::rename(x, y));
    source.push(ChangeKind::MovedFrom, s(x));
    source.push(ChangeKind::MovedTo, s(y));
    QVERIFY(matches("rename to y.txt"));

    QVERIFY(QFile::remove(y));
    source.push(ChangeKind::Deleted, s(y));
    QVERIFY(matches("delete y.txt"));

    const QString z = m_root + QStringLiteral("/Z");
    QVERIFY(QDir().mkdir(z));
    source.push(ChangeKind::Created, s(z), true);
    for (int i = 1; i <= 3; ++i) {
        const QString file = z + QStringLiteral("/f%1.txt").arg(i);
        QVERIFY(writeFile(file, QByteArray(i, 'z')));
        source.push(ChangeKind::Created, s(file));
    }
    QVERIFY(matches("folder Z with 3 files"));
    QCOMPARE(diskRows(m_root, settings).size(), 7);
    QVERIFY(follower.changesSeen() >= 9);
}

QTEST_GUILESS_MAIN(ChangesTests)
#include "changes_tests.moc"
