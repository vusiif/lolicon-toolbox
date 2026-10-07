#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include <QElapsedTimer>
#include "ByteEncodingAnalyzer.h"
#include "RecoveryOperation.h"
using namespace mojibake;
class TestFileRecovery : public QObject {
    Q_OBJECT
private slots:
    void unicodeBytes();
    void byteAnalysis();
    void damagedAndBinary();
    void saveSafety();
    void performance();
};
void TestFileRecovery::unicodeBytes() {
    const QString leadingBom = QString(QChar(0xfeff)) + "A";
    for (auto e : {Encoding::Utf8, Encoding::Utf16LE, Encoding::Utf16BE, Encoding::Utf32LE, Encoding::Utf32BE}) {
        const auto content = EncodingConverter::encode(leadingBom, e); QVERIFY(content);
        QCOMPARE(*EncodingConverter::decode(*content, e), leadingBom);
        const auto fileBytes = ByteEncodingAnalyzer::bom(e) + *content;
        const auto result = ByteEncodingAnalyzer::analyze(fileBytes);
        QVERIFY(!result.candidates.isEmpty()); QCOMPARE(result.candidates.first().text, leadingBom);
        QCOMPARE(*RecoveryOperation::output(result.candidates.first().text, e, true, Newlines::Preserve), fileBytes);
    }
    const QString text = "你好";
    for (const auto& pair : {std::pair{Encoding::Utf16LE, "604f7d59"}, std::pair{Encoding::Utf16BE, "4f60597d"},
                            std::pair{Encoding::Utf32LE, "604f00007d590000"}, std::pair{Encoding::Utf32BE, "00004f600000597d"}}) {
        const auto bytes = EncodingConverter::encode(text, pair.first); QVERIFY(bytes);
        QCOMPARE(*bytes, QByteArray::fromHex(pair.second));
        QCOMPARE(*EncodingConverter::decode(*bytes, pair.first), text);
        const auto result = ByteEncodingAnalyzer::analyze(ByteEncodingAnalyzer::bom(pair.first) + *bytes);
        QCOMPARE(result.bomEncoding, std::optional<Encoding>(pair.first));
        QVERIFY(!result.candidates.isEmpty()); QCOMPARE(result.candidates.first().text, text);
        QCOMPARE(result.candidates.first().encoding, pair.first);
    }
    QVERIFY(!EncodingConverter::decode(QByteArray::fromHex("00d8"), Encoding::Utf16LE));
    QVERIFY(!EncodingConverter::decode(QByteArray::fromHex("41"), Encoding::Utf16LE));
    QVERIFY(!EncodingConverter::decode(QByteArray::fromHex("00d80000"), Encoding::Utf32LE));
    QVERIFY(!EncodingConverter::decode(QByteArray::fromHex("00001100"), Encoding::Utf32LE));
    QVERIFY(!EncodingConverter::decode(QByteArray::fromHex("000000"), Encoding::Utf32BE));
    QCOMPARE(*EncodingConverter::decode(*EncodingConverter::encode("😀", Encoding::Utf16LE), Encoding::Utf16LE), QString("😀"));
}
void TestFileRecovery::byteAnalysis() {
    for (auto e : EncodingConverter::supported()) {
        const auto bytes = EncodingConverter::encode("text\r\nline\rnext\n", e); QVERIFY(bytes);
        const auto result = ByteEncodingAnalyzer::analyze(*bytes, e);
        QVERIFY(!result.candidates.isEmpty()); QCOMPARE(result.candidates.first().text, QString("text\r\nline\rnext\n"));
    }
    const auto utf8 = ByteEncodingAnalyzer::analyze(QString("正常中文 😀").toUtf8());
    QCOMPARE(utf8.candidates.first().text, QString("正常中文 😀")); QCOMPARE(utf8.candidates.first().encoding, Encoding::Utf8);
    const auto ascii = ByteEncodingAnalyzer::analyze("plain ASCII");
    QVERIFY(ascii.candidates.first().aliases.size() > 1);
    const auto gbk = ByteEncodingAnalyzer::analyze(*EncodingConverter::encode("你好世界", Encoding::Gbk), Encoding::Gbk);
    QCOMPARE(gbk.candidates.first().text, QString("你好世界"));
    const auto savedMojibake = ByteEncodingAnalyzer::analyze(QString("浣犲ソ").toUtf8());
    QCOMPARE(savedMojibake.candidates.first().text, QString("你好"));
    QCOMPARE(savedMojibake.candidates.first().encoding, Encoding::Utf8);
    QVERIFY(!savedMojibake.candidates.first().repairPath.isEmpty());
    const auto noBom = ByteEncodingAnalyzer::analyze(*EncodingConverter::encode("Hello\n世界", Encoding::Utf16LE));
    QVERIFY(!noBom.candidates.isEmpty());
    QCOMPARE(noBom.candidates.first().text, QString("Hello\n世界"));
    const auto empty = ByteEncodingAnalyzer::analyze(""); QVERIFY(!empty.candidates.isEmpty()); QVERIFY(empty.candidates.first().text.isEmpty());
}
void TestFileRecovery::damagedAndBinary() {
    QVERIFY(ByteEncodingAnalyzer::analyze(QByteArray::fromHex("efbbbfe4bd")).invalidBom);
    QVERIFY(ByteEncodingAnalyzer::analyze(QByteArray::fromHex("fffe000041")).invalidBom);
    QVERIFY(ByteEncodingAnalyzer::analyze(QByteArray::fromHex("89504e470d0a1a0a000000")).probablyBinary);
    QVERIFY(ByteEncodingAnalyzer::analyze(QByteArray(64, '\0')).probablyBinary);
    QVERIFY(ByteEncodingAnalyzer::analyze("MZ is also valid text.").candidates.size() > 0);
    QCOMPARE(ByteEncodingAnalyzer::analyze(QString("正常文本�").toUtf8()).candidates.first().text, QString("正常文本�"));
    auto cancel = std::make_shared<std::atomic_bool>(true);
    QVERIFY(ByteEncodingAnalyzer::analyze("text", {}, cancel).cancelled);
    QVERIFY(ByteEncodingAnalyzer::analyze(QByteArray(ByteEncodingAnalyzer::MaxFileBytes + 1, 'a')).tooLarge);
    QVERIFY(ByteEncodingAnalyzer::analyze(QByteArray::fromHex("fffe604f"), Encoding::Utf8).invalidBom);
}
void TestFileRecovery::saveSafety() {
    QTemporaryDir dir; const auto source = dir.filePath("source.txt"), destination = dir.filePath("copy.txt");
    const QByteArray original = QString("你好\r\nNBSP \rthird\n").toUtf8();
    { QFile f(source); QVERIFY(f.open(QIODevice::WriteOnly)); QCOMPARE(f.write(original), original.size()); }
    const auto snapshot = ByteEncodingAnalyzer::load(source);
    QVERIFY(!snapshot.candidates.isEmpty());
    QCOMPARE(snapshot.candidates.first().text.toUtf8(), original);
    QVERIFY(RecoveryOperation::saveCopy(snapshot, 0, destination, Encoding::Utf16LE, true, Newlines::Preserve).success);
    { QFile f(destination); QVERIFY(f.open(QIODevice::ReadOnly)); QCOMPARE(f.readAll(), ByteEncodingAnalyzer::bom(Encoding::Utf16LE) + *EncodingConverter::encode(QString::fromUtf8(original), Encoding::Utf16LE)); }
    QVERIFY(!RecoveryOperation::saveCopy(snapshot, 0, destination, Encoding::Utf8, false, Newlines::LF).success);
    QVERIFY(!RecoveryOperation::saveCopy(snapshot, 0, source, Encoding::Utf8, false, Newlines::LF).success);
    { QFile f(source); QVERIFY(f.open(QIODevice::ReadOnly)); QCOMPARE(f.readAll(), original); }
    const auto impossible = dir.filePath("cannot-latin1.txt");
    QVERIFY(!RecoveryOperation::saveCopy(snapshot, 0, impossible, Encoding::Latin1, false, Newlines::Preserve).success);
    QVERIFY(!QFile::exists(impossible));
    QCOMPARE(*RecoveryOperation::output("a\r\nb\rc\n", Encoding::Utf8, false, Newlines::LF), QByteArray("a\nb\nc\n"));
    QCOMPARE(*RecoveryOperation::output("a\r\nb\rc\n", Encoding::Utf8, false, Newlines::CRLF), QByteArray("a\r\nb\r\nc\r\n"));
    { QFile f(source); QVERIFY(f.open(QIODevice::Append)); f.write("changed"); }
    const auto stale = dir.filePath("stale.txt");
    QVERIFY(!RecoveryOperation::saveCopy(snapshot, 0, stale, Encoding::Utf8, false, Newlines::Preserve).success);
    QVERIFY(!QFile::exists(stale));
}
void TestFileRecovery::performance() {
    for (int size : {1024, 100 * 1024, 1024 * 1024, 10 * 1024 * 1024}) {
        QElapsedTimer timer; timer.start();
        const auto result = ByteEncodingAnalyzer::analyze(QByteArray(size, 'a'));
        QVERIFY(!result.candidates.isEmpty()); QCOMPARE(result.candidates.first().text.size(), size);
        QVERIFY(result.candidates.size() <= 5);
        qInfo() << "file bytes" << size << "ms" << timer.elapsed();
    }
}
QTEST_GUILESS_MAIN(TestFileRecovery)
#include "TestFileRecovery.moc"
