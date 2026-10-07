#include <QtTest>
#include <QTemporaryDir>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <thread>
#include "FilenameRecovery.h"
using namespace mojibake;
namespace {
bool write(const QString& path, const QByteArray& bytes) {
    QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size();
}
QByteArray read(const QString& path) { QFile f(path); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll(); }
int find(const NamePlan& plan, const QString& path) {
    for (int i = 0; i < plan.entries.size(); ++i) if (plan.entries[i].relativePath == path) return i;
    return -1;
}
}
class TestFilenameRecovery : public QObject {
    Q_OBJECT
private slots:
    void nestedCopy();
    void invalidNamesAndConflicts();
    void destinationSafety();
    void sourceChanges();
    void limitsCancellationAndLinks();
    void cancellationRetainsPartialCopy();
};
void TestFilenameRecovery::nestedCopy() {
    QTemporaryDir dir; const auto root = dir.filePath("source"), destination = dir.filePath("copy");
    QVERIFY(QDir().mkpath(root + "/浣犲ソ/empty"));
    const auto binary = QByteArray::fromHex("89504e470d0a1a0a00ff00000102");
    QVERIFY(write(root + "/浣犲ソ/浣犲ソ.txt", "unchanged\r\n"));
    QVERIFY(write(root + "/normal.bin", binary)); QVERIFY(write(root + "/.keep", "hidden"));
    auto plan = FilenameRecovery::scan(root); QVERIFY2(plan.error.isEmpty(), qPrintable(plan.error)); QCOMPARE(plan.entries.size(), 5);
    const int folder = find(plan, "浣犲ソ"), file = find(plan, "浣犲ソ/浣犲ソ.txt"); QVERIFY(folder >= 0); QVERIFY(file >= 0);
    QVERIFY(std::any_of(plan.entries[folder].candidates.begin(), plan.entries[folder].candidates.end(), [](const auto& c) { return c.text == "你好"; }));
    QVERIFY(std::any_of(plan.entries[file].candidates.begin(), plan.entries[file].candidates.end(), [](const auto& c) { return c.text == "你好.txt"; }));
    QCOMPARE(plan.entries[folder].selectedName, QString("浣犲ソ")); // Analysis never applies a suggestion.
    plan.entries[folder].selectedName = "你好"; plan.entries[file].selectedName = "你好.txt";
    const auto validation = FilenameRecovery::validate(plan); QVERIFY(validation.valid()); QCOMPARE(validation.paths[file], QString("你好/你好.txt"));
    auto progress = std::make_shared<NameProgress>();
    const auto result = FilenameRecovery::copy(plan, destination, {}, progress); QVERIFY2(result.success, qPrintable(result.error));
    QCOMPARE(read(destination + "/你好/你好.txt"), QByteArray("unchanged\r\n")); QCOMPARE(read(destination + "/normal.bin"), binary);
    QCOMPARE(read(destination + "/.keep"), QByteArray("hidden")); QVERIFY(QFileInfo(destination + "/你好/empty").isDir());
    QCOMPARE(read(root + "/浣犲ソ/浣犲ソ.txt"), QByteArray("unchanged\r\n")); QVERIFY(!QFileInfo::exists(root + "/你好"));
    QCOMPARE(progress->entries.load(), plan.entries.size()); QCOMPARE(progress->bytes.load(), plan.totalBytes);
}
void TestFilenameRecovery::invalidNamesAndConflicts() {
    for (const auto& name : {QString(), QString("."), QString(".."), QString("../escape"), QString("dir\\escape"), QString("A:B"),
                            QString("NUL.txt"), QString("CoN.log"), QString("COM1"), QString("LPT².txt"), QString("CON .txt"),
                            QString("trailing."), QString("trailing "), QString(256, 'a'), QString("bad?")})
        QVERIFY2(!FilenameRecovery::invalidName(name).isEmpty(), qPrintable(name));
    for (const auto& name : {QString("你好.txt"), QString("COM10.txt"), QString("normal 😀"), QString(".env")}) QVERIFY(FilenameRecovery::invalidName(name).isEmpty());
    NamePlan plan; plan.entries = {{"a.txt", "same.txt"}, {"b.txt", "SAME.TXT"}, {"folder", "folder", -1, true}, {"folder/a.txt", "same.txt", 2}};
    auto validation = FilenameRecovery::validate(plan); QVERIFY(!validation.valid()); QVERIFY(!validation.errors[0].isEmpty()); QVERIFY(!validation.errors[1].isEmpty()); QVERIFY(validation.errors[3].isEmpty());
    plan.entries[1].selectedName = "different.txt"; QVERIFY(FilenameRecovery::validate(plan).valid());
    plan.entries[2].selectedName = "same.txt"; QVERIFY(!FilenameRecovery::validate(plan).valid()); // File/folder collision.
    plan.entries[2].selectedName = "folder"; plan.entries[3].parent = 3; QVERIFY(!FilenameRecovery::validate(plan).valid());
}
void TestFilenameRecovery::destinationSafety() {
    QTemporaryDir dir; const auto source = dir.filePath("source"); QVERIFY(QDir().mkpath(source)); QVERIFY(write(source + "/a", "original"));
    const auto plan = FilenameRecovery::scan(source); QVERIFY(plan.error.isEmpty());
    for (const auto& destination : {source, source + "/nested", dir.path()}) {
        const auto result = FilenameRecovery::copy(plan, destination); QVERIFY(!result.success); QVERIFY(!result.outputCreated);
    }
    QVERIFY(QDir().mkdir(dir.filePath("existing"))); QVERIFY(write(dir.filePath("existing/sentinel"), "keep"));
    QVERIFY(!FilenameRecovery::copy(plan, dir.filePath("existing")).success); QCOMPARE(read(dir.filePath("existing/sentinel")), QByteArray("keep"));
    QVERIFY(write(dir.filePath("existing-file"), "keep-file")); QVERIFY(!FilenameRecovery::copy(plan, dir.filePath("existing-file")).success);
    QCOMPARE(read(dir.filePath("existing-file")), QByteArray("keep-file"));
    auto bad = plan; bad.entries[0].selectedName = "../escape"; QVERIFY(!FilenameRecovery::copy(bad, dir.filePath("bad")).success); QVERIFY(!QFileInfo::exists(dir.filePath("bad")));
    QCOMPARE(read(source + "/a"), QByteArray("original"));
}
void TestFilenameRecovery::sourceChanges() {
    QTemporaryDir dir; const auto source = dir.filePath("source"); QVERIFY(QDir().mkdir(source)); QVERIFY(write(source + "/a", "before"));
    const auto plan = FilenameRecovery::scan(source); QVERIFY(plan.error.isEmpty());
    QVERIFY(write(source + "/a", "after!")); // Same size, different hash.
    QVERIFY(!FilenameRecovery::copy(plan, dir.filePath("changed")).success); QVERIFY(!QFileInfo::exists(dir.filePath("changed")));
    QVERIFY(write(source + "/a", "before")); QVERIFY(write(source + "/added", "new"));
    QVERIFY(!FilenameRecovery::copy(plan, dir.filePath("added")).success); QVERIFY(!QFileInfo::exists(dir.filePath("added")));
    QVERIFY(QFile::remove(source + "/added")); QVERIFY(QFile::remove(source + "/a"));
    QVERIFY(!FilenameRecovery::copy(plan, dir.filePath("removed")).success); QVERIFY(!QFileInfo::exists(dir.filePath("removed")));
}
void TestFilenameRecovery::limitsCancellationAndLinks() {
    QTemporaryDir dir; const auto source = dir.filePath("source"); QVERIFY(QDir().mkdir(source)); QVERIFY(write(source + "/a", "data"));
    const auto plan = FilenameRecovery::scan(source);
    auto cancel = std::make_shared<std::atomic_bool>(true);
    QVERIFY(FilenameRecovery::scan(source, cancel).cancelled);
    const auto cancelledCopy = FilenameRecovery::copy(plan, dir.filePath("cancelled"), cancel); QVERIFY(cancelledCopy.cancelled); QVERIFY(!cancelledCopy.outputCreated);
    QVERIFY(QFile::link(source + "/a", source + "/shortcut.lnk")); QVERIFY(QFileInfo(source + "/shortcut.lnk").isSymLink());
    QVERIFY(!FilenameRecovery::scan(source).error.isEmpty()); QVERIFY(!FilenameRecovery::copy(plan, dir.filePath("link-copy")).success);
    QVERIFY(QFile::remove(source + "/shortcut.lnk"));
    const auto large = source + "/too-large"; { QFile f(large); QVERIFY(f.open(QIODevice::WriteOnly)); QVERIFY(f.resize(FilenameRecovery::MaxTotalBytes + 1)); }
    QVERIFY(!FilenameRecovery::scan(source).error.isEmpty()); QVERIFY(QFile::remove(large));
    QString nested = source; for (int i = 0; i <= FilenameRecovery::MaxDepth; ++i) { nested += "/d"; QVERIFY(QDir().mkdir(nested)); }
    QVERIFY(!FilenameRecovery::scan(source).error.isEmpty());
    const auto many = dir.filePath("many"); QVERIFY(QDir().mkdir(many));
    for (int i = 0; i <= FilenameRecovery::MaxEntries; ++i) QVERIFY(write(many + '/' + QString::number(i), {}));
    QVERIFY(!FilenameRecovery::scan(many).error.isEmpty());
}
void TestFilenameRecovery::cancellationRetainsPartialCopy() {
    QTemporaryDir dir; const auto source = dir.filePath("source"), destination = dir.filePath("partial");
    QVERIFY(QDir().mkdir(source));
    constexpr qint64 size = 64 * 1024 * 1024;
    { QFile file(source + "/large.bin"); QVERIFY(file.open(QIODevice::WriteOnly)); QVERIFY(file.resize(size)); }
    const auto plan = FilenameRecovery::scan(source); QVERIFY(plan.error.isEmpty());
    auto cancel = std::make_shared<std::atomic_bool>(false); auto progress = std::make_shared<NameProgress>();
    // Wait for actual writes, rather than cancelling during the preflight scan.
    std::jthread monitor([&](std::stop_token stop) {
        while (!stop.stop_requested()) {
            if (progress->bytes.load() > 0) { cancel->store(true); return; }
            QThread::msleep(1);
        }
    });
    const auto result = FilenameRecovery::copy(plan, destination, cancel, progress); monitor.request_stop(); monitor.join();
    QVERIFY(!result.success); QVERIFY(result.cancelled); QVERIFY(result.outputCreated); QVERIFY(QFileInfo(destination).isDir());
    QVERIFY(QFileInfo::exists(destination + "/large.bin")); QVERIFY(QFileInfo(destination + "/large.bin").size() <= size);
    QCOMPARE(QFileInfo(source + "/large.bin").size(), size);
    QCOMPARE(FilenameRecovery::scan(source).entries.first().hash, plan.entries.first().hash);
    QVERIFY(!FilenameRecovery::copy(plan, destination).success); // Partial outputs cannot be reused or overwritten.
}
QTEST_GUILESS_MAIN(TestFilenameRecovery)
#include "TestFilenameRecovery.moc"
