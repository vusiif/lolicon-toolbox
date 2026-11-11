#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonDocument>
#include "DiagnosticReport.h"
#include "MojibakeAnalyzer.h"
using namespace mojibake;
namespace {
class TestDiagnosticReport : public QObject {
    Q_OBJECT
private slots:
    void textAssessments();
    void fileEvidence();
    void namePlanEvidence();
    void clipboardEvidence();
    void exportSafetyAndPrivacy();
};
void TestDiagnosticReport::textAssessments() {
    const auto fixed = MojibakeAnalyzer::analyze("浣犲ソ");
    const auto report = DiagnosticReport::text(fixed, 0);
    QVERIFY(report.ready); QCOMPARE(report.data["assessment"].toString(), QString("review"));
    QVERIFY(report.data["inverse_verified"].toBool()); QVERIFY(!report.data["original_bytes_available"].toBool());
    QVERIFY(report.summary().contains("does not prove"));
    const auto normal = DiagnosticReport::text(MojibakeAnalyzer::analyze("正常文字"), 0); QCOMPARE(normal.data["assessment"].toString(), QString("retained"));
    Result partial; partial.original = "damaged�"; partial.candidates = {{.text = "recovered�", .path = {}, .score = 90, .anomalies = 1, .reversible = false}};
    const auto incomplete = DiagnosticReport::text(partial, 0); QCOMPARE(incomplete.data["assessment"].toString(), QString("partial"));
    QVERIFY(incomplete.data["possible_information_loss"].toBool()); QVERIFY(!incomplete.data["inverse_verified"].toBool());
    partial.candidates = {{.text = "damaged�", .path = {}, .score = 90, .anomalies = 1, .reversible = true}, {.text = "alternative", .path = {}, .score = 87, .anomalies = 0, .reversible = true}};
    const auto ambiguous = DiagnosticReport::text(partial, 0); QVERIFY(ambiguous.data["ambiguous"].toBool()); QCOMPARE(ambiguous.data["assessment"].toString(), QString("review"));
    partial.cancelled = true; partial.candidates.clear(); const auto cancelled = DiagnosticReport::text(partial, -1);
    QCOMPARE(cancelled.data["assessment"].toString(), QString("unavailable")); QVERIFY(cancelled.data["inverse_verified"].isNull()); QVERIFY(cancelled.data["possible_information_loss"].toBool());
    QVERIFY(!DiagnosticReport::text({}, -1).ready);
}
void TestDiagnosticReport::fileEvidence() {
    const auto bytes = ByteEncodingAnalyzer::bom(Encoding::Utf16LE) + *EncodingConverter::encode("你好", Encoding::Utf16LE);
    const auto source = ByteEncodingAnalyzer::analyze(bytes); const auto report = DiagnosticReport::file(source, 0);
    QVERIFY(report.data["original_bytes_available"].toBool());
    QCOMPARE(report.data["evidence"].toObject()["bom"].toString(), QString("UTF-16LE"));
    QCOMPARE(report.data["evidence"].toObject()["source_sha256"].toString(), QString::fromLatin1(source.hash.toHex()));
    QVERIFY(!report.data["possible_information_loss"].toBool());
    const auto aliases = DiagnosticReport::file(ByteEncodingAnalyzer::analyze("ASCII"), 0); QVERIFY(aliases.data["ambiguous"].toBool());
    const auto invalid = DiagnosticReport::file(ByteEncodingAnalyzer::analyze(QByteArray::fromHex("efbbbfe4bd")), -1);
    QCOMPARE(invalid.data["assessment"].toString(), QString("blocked")); QVERIFY(invalid.data["inverse_verified"].isNull());
}
void TestDiagnosticReport::namePlanEvidence() {
    NamePlan plan; plan.root = "C:/example";
    NameEntry entry; entry.relativePath = "浣犲ソ.txt"; entry.selectedName = "你好.txt";
    entry.candidates = {{.text = "你好.txt", .path = {{.encodeAs = Encoding::Gbk, .decodeAs = Encoding::Utf8}}, .score = 98, .anomalies = 0, .reversible = true}}; plan.entries.append(entry);
    auto report = DiagnosticReport::names(plan); QVERIFY(report.data["inverse_verified"].toBool());
    QCOMPARE(report.data["metrics"].toObject()["name_changes"].toInt(), 1);
    QVERIFY(report.data["paths_or_names_included"].toBool()); QVERIFY(!report.data["original_bytes_available"].toBool());
    plan.entries[0].selectedName = "manual.txt"; report = DiagnosticReport::names(plan); QVERIFY(report.data["inverse_verified"].isNull());
    plan.entries[0].selectedName = "CON.txt"; report = DiagnosticReport::names(plan); QCOMPARE(report.data["assessment"].toString(), QString("blocked"));
    plan.cancelled = true; report = DiagnosticReport::names(plan); QCOMPARE(report.data["assessment"].toString(), QString("unavailable"));
    QVERIFY(!report.data["evidence"].toObject()["file_hashes_available"].toBool());
}
void TestDiagnosticReport::clipboardEvidence() {
    ClipboardTexts snapshot; snapshot.capture.hasPlain = true; snapshot.capture.plain = "坏�";
    snapshot.capture.hasHtml = true; snapshot.htmlText = "正常文字"; snapshot.capture.formats = {"text/plain", "text/html"};
    auto report = DiagnosticReport::clipboard(snapshot, 0); QVERIFY(report.data["possible_information_loss"].toBool());
    QVERIFY(report.data["ambiguous"].toBool()); QVERIFY(report.data["inverse_verified"].isNull());
    QCOMPARE(report.data["assessment"].toString(), QString("source_selected"));
    QVERIFY(!report.data["evidence"].toObject()["repair_analysis_performed"].toBool());
    report = DiagnosticReport::clipboard(snapshot, 1); QVERIFY(!report.data["possible_information_loss"].toBool());
}
void TestDiagnosticReport::exportSafetyAndPrivacy() {
    const QTemporaryDir dir; const auto path = dir.filePath("report.json"); QString error;
    QVERIFY(!DiagnosticReport{}.save(path, error)); QVERIFY(!QFile::exists(path));
    const auto report = DiagnosticReport::text(MojibakeAnalyzer::analyze("private-secret-body"), 0);
    QVERIFY(report.save(path, error)); QVERIFY(error.isEmpty());
    QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); const auto bytes = file.readAll(); file.close();
    const auto data = QJsonDocument::fromJson(bytes).object(); QCOMPARE(data["schema_version"].toInt(), 1);
    QVERIFY(!data["text_content_included"].toBool()); QVERIFY(!data["ai_output_included"].toBool()); QVERIFY(!data["api_configuration_included"].toBool());
    QVERIFY(!bytes.contains("private-secret-body")); QVERIFY(data.contains("exported_at_utc"));
    QVERIFY(!report.save(path, error)); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), bytes);
}
}  // namespace
QTEST_GUILESS_MAIN(TestDiagnosticReport)
#include "TestDiagnosticReport.moc"
