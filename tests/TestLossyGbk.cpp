#include <QTest>
#include <QFile>
#include <QElapsedTimer>
#include <QStringConverter>
#include "LossyGbkRecovery.h"
#include "MojibakeAnalyzer.h"
#include "ByteEncodingAnalyzer.h"
using namespace mojibake;
namespace {
QString readFixture(const QString& name) {
    QFile file(QStringLiteral(TEST_DATA_DIR "/") + name);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return QString::fromUtf8(file.readAll());
}
QByteArray gbk(const QString& text) {
    QStringEncoder encoder("GBK", QStringConverter::Flag::Stateless);
    return encoder(text);
}
// GBK text read as UTF-8 with replacement characters, the damage seen in real files.
QString damage(const QString& text) { return LossyGbkRecovery::lossyUtf8(gbk(text)); }
// Longest common subsequence over the longer length: a fair score when a
// recovery inserts or drops a character where the byte count is ambiguous.
double positional(const QString& a, const QString& b) {
    std::vector row(b.size() + 1, 0);
    for (qsizetype i = 1; i <= a.size(); ++i) {
        int diagonal = 0;
        for (qsizetype j = 1; j <= b.size(); ++j) {
            const int up = row[j];
            row[j] = a[i - 1] == b[j - 1] ? diagonal + 1 : std::max(row[j], row[j - 1]);
            diagonal = up;
        }
    }
    return static_cast<double>(row[b.size()]) / static_cast<double>(std::max<qsizetype>(1, std::max(a.size(), b.size())));
}
class TestLossyGbk : public QObject {
    Q_OBJECT
private slots:
    void decoderFollowsMaximalSubparts();
    void modelLoads();
    void detectsPattern();
    void recoversProse();
    void recoversEssay();
    void guideIsVerified();
    void glossaryIsByteProven();
    void repairUnitsCoverDamage();
    void detectsReplacementConvention();
    void analyzerOffersCandidate();
    void fileAnalysis();
    void largeInput();
};
void TestLossyGbk::decoderFollowsMaximalSubparts() {
    // Unicode 6.0+ recommended practice: one U+FFFD per maximal subpart.
    QCOMPARE(LossyGbkRecovery::lossyUtf8(QByteArray::fromHex("e3ba41")), QString(QChar(0xfffd)) + "A");
    QCOMPARE(LossyGbkRecovery::lossyUtf8(QByteArray::fromHex("b6aa")), QString(2, QChar(0xfffd)));
    QCOMPARE(LossyGbkRecovery::lossyUtf8(QByteArray::fromHex("cfa2")), QString(QChar(0x03e2)));
    QCOMPARE(LossyGbkRecovery::lossyUtf8(QByteArray::fromHex("f1b9b220")), QString(QChar(0xfffd)) + " ");
    QCOMPARE(LossyGbkRecovery::lossyUtf8(QByteArray::fromHex("e080")), QString(2, QChar(0xfffd)));
    // Qt writes one U+FFFD per byte of a broken sequence instead.
    const auto bytes = gbk("信息丢失，你好！");
    QStringDecoder utf8(QStringConverter::Utf8, QStringConverter::Flag::Stateless);
    QCOMPARE(LossyGbkRecovery::lossyUtf8(bytes, Utf8Replacement::PerByte), QString(utf8(bytes)));
    QVERIFY(LossyGbkRecovery::lossyUtf8(bytes) != QString(utf8(bytes)));
}
void TestLossyGbk::modelLoads() { QVERIFY(LossyGbkRecovery::modelAvailable()); }
void TestLossyGbk::detectsPattern() {
    QVERIFY(LossyGbkRecovery::applicable(damage(readFixture("lossy_gbk_prose.txt"))));
    QVERIFY(!LossyGbkRecovery::applicable("正常的中文文本。"));
    QVERIFY(!LossyGbkRecovery::applicable("请小组负责人在群内汇报，方便指导�师掌握情况�祝顺利"));
    QVERIFY(!LossyGbkRecovery::applicable("café naïve Straße"));
}
void TestLossyGbk::recoversProse() {
    const auto truth = readFixture("lossy_gbk_prose.txt");
    QVERIFY(!truth.isEmpty());
    const auto damaged = damage(truth);
    QElapsedTimer timer; timer.start();
    const auto r = LossyGbkRecovery::recover(damaged);
    qInfo("prose: %lld chars, %lld ms, exact %d partial %d unknown %d", truth.size(), timer.elapsed(), r.exact, r.partial, r.unknown);
    QVERIFY(r.success);
    QCOMPARE(r.evidence.size(), r.text.size());
    QVERIFY(qAbs(r.text.size() - truth.size()) * 50 <= truth.size());
    qInfo("similarity %.2f", positional(r.text, truth));
    QVERIFY(positional(r.text, truth) > 0.35);
    if (r.text.size() != truth.size()) return;
    int exactRight = 0, partialRight = 0;
    for (qsizetype i = 0; i < truth.size(); ++i) {
        const auto e = static_cast<CharEvidence>(r.evidence[i]);
        if (e == CharEvidence::Exact) exactRight += r.text[i] == truth[i];
        if (e == CharEvidence::Partial) partialRight += r.text[i] == truth[i];
        if (e == CharEvidence::Unknown) QCOMPARE(r.text[i], QChar(LossyGbkRecovery::UnknownMark));
    }
    qInfo("exact correct %d/%d, partial correct %d/%d", exactRight, r.exact, partialRight, r.partial);
    // Byte-backed characters are right; the model gets at least a third of half-known ones.
    QVERIFY(exactRight >= r.exact * 95 / 100);
    QVERIFY(partialRight * 3 >= r.partial);
    // Unknown characters contradict no surviving byte: the spans line up with the damage.
    QVERIFY(!r.spans.isEmpty());
    QCOMPARE(r.spans.last().damagedEnd, damaged.size());
    QCOMPARE(r.spans.last().textEnd, r.text.size());
}
// A formal essay damaged the way a .NET or Qt reader damages it (one U+FFFD per byte).
void TestLossyGbk::recoversEssay() {
    // Benchmark output for both fixtures and conventions; the assertions below use the essay.
    for (const auto convention : {Utf8Replacement::PerByte, Utf8Replacement::MaximalSubpart})
        for (const auto* name : {"lossy_gbk_essay.txt", "lossy_gbk_prose.txt"}) {
            const auto truth = readFixture(name);
            const auto r = LossyGbkRecovery::recover(LossyGbkRecovery::lossyUtf8(gbk(truth), convention));
            int partial = 0;
            if (r.text.size() == truth.size())
                for (qsizetype i = 0; i < truth.size(); ++i) partial += static_cast<CharEvidence>(r.evidence[i]) == CharEvidence::Partial && r.text[i] == truth[i];
            qInfo("%s %s: similarity %.3f, partial correct %d/%d", name, convention == Utf8Replacement::PerByte ? "per-byte" : "subpart",
                  positional(r.text, truth), partial, r.partial);
        }
    const auto truth = readFixture("lossy_gbk_essay.txt");
    QVERIFY(!truth.isEmpty());
    const auto damaged = LossyGbkRecovery::lossyUtf8(gbk(truth), Utf8Replacement::PerByte);
    const auto r = LossyGbkRecovery::recover(damaged);
    QVERIFY(r.success);
    QCOMPARE(r.replacement, Utf8Replacement::PerByte);
    QCOMPARE(r.text.size(), truth.size());
    QCOMPARE(LossyGbkRecovery::lossyUtf8(r.bytes, Utf8Replacement::PerByte), damaged);  // the bytes behind the text explain the damage
    int exactRight = 0, partialRight = 0, inTopFive = 0;
    QString ideal;  // what an AI that picks well from the listed alternatives would answer
    for (qsizetype i = 0; i < truth.size(); ++i) {
        const auto e = static_cast<CharEvidence>(r.evidence[i]);
        if (e == CharEvidence::Exact) exactRight += r.text[i] == truth[i];
        if (e != CharEvidence::Partial) { ideal += truth[i]; continue; }
        partialRight += r.text[i] == truth[i];
        const auto options = LossyGbkRecovery::alternatives(r, i, 5);
        QVERIFY(options.contains(r.text[i]));
        inTopFive += options.contains(truth[i]);
        ideal += options.contains(truth[i]) ? truth[i] : options[0];
    }
    qInfo("essay: exact %d/%d, partial %d/%d, original among five alternatives %d/%d, unknown %d", exactRight, r.exact,
          partialRight, r.partial, inTopFive, r.partial, r.unknown);
    QCOMPARE(exactRight, r.exact);
    QVERIFY(partialRight * 2 >= r.partial);
    QVERIFY(inTopFive * 4 >= r.partial * 3);
    // The verified guide keeps every surviving byte and takes what the guide got right.
    const auto refined = LossyGbkRecovery::guided(damaged, ideal, Utf8Replacement::PerByte);
    QVERIFY(refined.success);
    QCOMPARE(LossyGbkRecovery::lossyUtf8(refined.bytes, Utf8Replacement::PerByte), damaged);
    qInfo("refined with an ideal guide: similarity %.3f, rejected %d", positional(refined.text, truth), refined.guideRejected);
    QVERIFY(positional(refined.text, truth) > 0.9);
    // Maximal-subpart damage (how 笔记_3.md was made): one U+FFFD hides one to three bytes, so the
    // number of lost characters is itself a guess, and a guide must be able to correct it.
    const auto subpartDamage = damage(truth);
    const auto subpart = LossyGbkRecovery::recover(subpartDamage);
    QCOMPARE(subpart.replacement, Utf8Replacement::MaximalSubpart);
    const auto subpartRefined = LossyGbkRecovery::guided(subpartDamage, truth, Utf8Replacement::MaximalSubpart);
    qInfo("subpart: local similarity %.3f, %lld vs %lld characters; with the original as guide %.3f, rejected %d",
          positional(subpart.text, truth), subpart.text.size(), truth.size(), positional(subpartRefined.text, truth), subpartRefined.guideRejected);
    QCOMPARE(LossyGbkRecovery::lossyUtf8(subpartRefined.bytes), subpartDamage);
    QVERIFY(positional(subpartRefined.text, truth) > 0.9);
}
void TestLossyGbk::guideIsVerified() {
    const QString truth = "警察到达现场的时候，房间里的灯还开着。死者倒在书桌旁边，手里握着一支钢笔。";
    const auto damaged = damage(truth);
    // A correct guide is accepted in full, and the result reproduces the damage.
    const auto right = LossyGbkRecovery::guided(damaged, truth, Utf8Replacement::MaximalSubpart);
    QVERIFY(right.success);
    QCOMPARE(right.text, truth);
    QCOMPARE(right.guideRejected, 0);
    QVERIFY(right.guided > 0);
    QVERIFY(LossyGbkRecovery::consistent(right.text, damaged));
    // A guide that contradicts surviving bytes cannot override them.
    QString wrong = truth; wrong.replace("钢笔", "铅笔").replace("房间", "屋子");
    const auto checked = LossyGbkRecovery::guided(damaged, wrong, Utf8Replacement::MaximalSubpart);
    QVERIFY(checked.success);
    QVERIFY(checked.guideRejected > 0);
    QCOMPARE(checked.text.size(), truth.size());
    for (qsizetype i = 0; i < checked.text.size(); ++i)
        if (static_cast<CharEvidence>(checked.evidence[i]) == CharEvidence::Exact) QCOMPARE(checked.text[i], truth[i]);
    QVERIFY(!checked.text.contains("铅笔"));
    // Markup copied back by the model: a group counts as its first option, □ holds its place.
    QString echoed = truth;
    echoed.replace("钢笔", "〔钢|铅〕笔").replace("房间", "□间");
    const auto copied = LossyGbkRecovery::guided(damaged, echoed, Utf8Replacement::MaximalSubpart);
    QVERIFY(copied.success);
    QCOMPARE(copied.text.size(), truth.size());
    QVERIFY(copied.text.contains("钢笔"));
    QCOMPARE(copied.guideRejected, 0);
    const auto room = truth.indexOf("房");
    QVERIFY(static_cast<CharEvidence>(copied.evidence[room]) != CharEvidence::Guided);
    QVERIFY(positional(checked.text, truth) > positional(LossyGbkRecovery::recover(damaged).text, truth));
}
void TestLossyGbk::glossaryIsByteProven() {
    const auto truth = readFixture("lossy_gbk_essay.txt");
    const auto r = LossyGbkRecovery::recover(damage(truth));
    QVERIFY(r.success);
    const auto terms = LossyGbkRecovery::glossary(r, 400);
    qInfo().noquote() << "essay glossary:" << terms.join(u'、');
    QVERIFY(!terms.isEmpty());
    qsizetype length = 0;
    for (const auto& term : terms) {
        QVERIFY2(truth.contains(term), qPrintable(term));  // only surviving bytes go in
        QVERIFY(term.size() >= 2);
        length += term.size();
    }
    QVERIFY(length <= 400);
    QCOMPARE(QSet<QString>(terms.begin(), terms.end()).size(), terms.size());
    QVERIFY(LossyGbkRecovery::glossary(r, 0).isEmpty());
    // Nothing recurs in a single sentence.
    QVERIFY(LossyGbkRecovery::glossary(LossyGbkRecovery::recover(damage("今天天气很好，我们去公园散步吧。")), 400).isEmpty());
}
void TestLossyGbk::repairUnitsCoverDamage() {
    const auto truth = readFixture("lossy_gbk_essay.txt");
    const auto damaged = LossyGbkRecovery::lossyUtf8(gbk(truth), Utf8Replacement::PerByte);
    const auto r = LossyGbkRecovery::recover(damaged);
    QCOMPARE(r.text.size(), truth.size());  // per-byte damage keeps the character count
    const qsizetype begin = 0, end = std::min<qsizetype>(r.text.size(), 900);
    const auto units = LossyGbkRecovery::repairUnits(r, begin, end, 8);
    QVERIFY(!units.isEmpty());
    // Every damaged character is in exactly one unit, every certain one in none.
    QByteArray covered(end - begin, 0);
    for (const auto& u : units) {
        QVERIFY(u.textBegin >= begin && u.textEnd <= end && u.textEnd > u.textBegin && u.textEnd - u.textBegin <= 8);
        QCOMPARE(u.minLength, static_cast<int>(u.textEnd - u.textBegin));  // exact count: no slack
        QCOMPARE(u.maxLength, u.minLength);
        for (auto i = u.textBegin; i < u.textEnd; ++i) ++covered[i - begin];
    }
    for (qsizetype i = begin; i < end; ++i)
        QCOMPARE(covered[i - begin], static_cast<char>(static_cast<CharEvidence>(r.evidence[i]) == CharEvidence::Exact ? 0 : 1));
    const auto draft = LossyGbkRecovery::unitMarkup(r, begin, end, units, 5);
    QCOMPARE(draft.count(QChar(0x27e6)), units.size());
    QVERIFY(draft.startsWith(units.first().textBegin == 0 ? QStringLiteral("⟦1:") : r.text.left(1)));
    // Right answers spliced in give the original; guided() accepts them in full.
    QHash<int, QString> fills;
    for (qsizetype k = 0; k < units.size(); ++k) fills.insert(static_cast<int>(k + 1), truth.mid(units[k].textBegin, units[k].textEnd - units[k].textBegin));
    int rejected = 0;
    const auto guide = LossyGbkRecovery::unitGuide(r, begin, end, units, fills, &rejected);
    QCOMPARE(guide, truth.mid(begin, end - begin));
    QCOMPARE(rejected, 0);
    // A fill of the wrong length is not used: its span keeps the local markup.
    fills[1] += u'多';
    const auto shorter = LossyGbkRecovery::unitGuide(r, begin, end, units, fills, &rejected);
    QCOMPARE(rejected, 1);
    QVERIFY(shorter.startsWith(LossyGbkRecovery::markup(r, begin, units.first().textEnd)));
    // Maximal-subpart damage hides the count: lost characters get a range.
    const auto subpart = LossyGbkRecovery::recover(damage(truth));
    const auto ranged = LossyGbkRecovery::repairUnits(subpart, 0, std::min<qsizetype>(subpart.text.size(), 900), 8);
    QVERIFY(std::ranges::any_of(ranged, [](const RepairUnit& u) { return u.maxLength > u.minLength; }));
    QVERIFY(std::ranges::all_of(ranged, [](const RepairUnit& u) { return u.minLength >= 1; }));
}
void TestLossyGbk::detectsReplacementConvention() {
    const auto truth = readFixture("lossy_gbk_prose.txt").left(300);
    QStringDecoder utf8(QStringConverter::Utf8, QStringConverter::Flag::Stateless);
    const QString qtDamage = utf8(gbk(truth));
    const auto r = LossyGbkRecovery::recover(qtDamage);
    QVERIFY(r.success);
    QCOMPARE(r.replacement, Utf8Replacement::PerByte);
    QCOMPARE(r.text.size(), truth.size());
    QCOMPARE(LossyGbkRecovery::recover(damage(truth)).replacement, Utf8Replacement::MaximalSubpart);
}
void TestLossyGbk::analyzerOffersCandidate() {
    const auto truth = readFixture("lossy_gbk_prose.txt").left(200);
    const auto result = MojibakeAnalyzer::analyze(damage(truth));
    QVERIFY(result.likelyMojibake);
    QVERIFY(result.possibleLoss);
    const auto& top = result.candidates.first();
    QVERIFY(!top.path.isEmpty() && top.path.last().lossy);
    QCOMPARE(top.evidence.size(), top.text.size());
    QVERIFY(!top.reversible);
    QVERIFY(positional(top.text, truth) > 0.35);
}
void TestLossyGbk::fileAnalysis() {
    const auto truth = readFixture("lossy_gbk_prose.txt");
    const auto bytes = damage(truth).toUtf8();
    const auto result = ByteEncodingAnalyzer::analyze(bytes);
    QVERIFY(!result.candidates.isEmpty());
    const auto& top = result.candidates.first();
    for (const auto& c : result.candidates) qInfo("%s score %.1f path %lld lossy %d %s", qPrintable(EncodingConverter::name(c.encoding)), c.score, c.repairPath.size(), !c.repairPath.isEmpty() && c.repairPath.last().lossy, qPrintable(c.text.left(40) + " / " + (c.repairPath.isEmpty() ? QString() : EncodingConverter::name(c.repairPath[0].encodeAs) + ">" + EncodingConverter::name(c.repairPath[0].decodeAs) + (c.repairPath.size() > 1 ? ", " + EncodingConverter::name(c.repairPath[1].encodeAs) + ">" + EncodingConverter::name(c.repairPath[1].decodeAs) + QString(c.repairPath[1].partial ? " partial" : "") : QString()))));
    QVERIFY(!top.repairPath.isEmpty() && top.repairPath.last().lossy);
    QCOMPARE(top.evidence.size(), top.text.size());
}
void TestLossyGbk::largeInput() {
    auto truth = readFixture("lossy_gbk_prose.txt");
    QString big;
    while (big.size() < 60000) big += truth;
    const auto damaged = damage(big);
    QElapsedTimer timer; timer.start();
    const auto r = LossyGbkRecovery::recover(damaged);
    qInfo("large: %lld chars in %lld ms", big.size(), timer.elapsed());
    QVERIFY(r.success);
    auto cancel = std::make_shared<std::atomic_bool>(true);
    QVERIFY(LossyGbkRecovery::recover(damaged, cancel).cancelled);
    // Optional manual benchmark on a real damaged file.
    if (const auto path = qEnvironmentVariable("LOSSY_GBK_SAMPLE"); !path.isEmpty()) {
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto text = QString::fromUtf8(file.readAll());
        timer.restart();
        const auto sample = LossyGbkRecovery::recover(text);
        qInfo("sample: %lld chars in %lld ms, exact %d partial %d unknown %d, applicable %d", text.size(), timer.elapsed(),
              sample.exact, sample.partial, sample.unknown, LossyGbkRecovery::applicable(text));
        QVERIFY(sample.success);
        if (const auto markup = qEnvironmentVariable("LOSSY_GBK_MARKUP"); !markup.isEmpty()) {
            QFile out(markup);  // the draft the AI receives for the first chunk
            QVERIFY(out.open(QIODevice::WriteOnly));
            out.write(LossyGbkRecovery::markup(sample, 0, std::min<qsizetype>(sample.text.size(), 900), 5).toUtf8());
        }
        timer.restart();
        const auto analyzed = ByteEncodingAnalyzer::load(path);
        qInfo("file analysis: %lld ms, %lld candidates, top lossy %d", timer.elapsed(), analyzed.candidates.size(),
              !analyzed.candidates.isEmpty() && !analyzed.candidates.first().repairPath.isEmpty() && analyzed.candidates.first().repairPath.last().lossy);
        timer.restart();
        const auto pasted = MojibakeAnalyzer::analyze(text);
        qInfo("text analysis: %lld ms, top lossy %d", timer.elapsed(),
              !pasted.candidates.isEmpty() && !pasted.candidates.first().path.isEmpty() && pasted.candidates.first().path.last().lossy);
    }
}
}  // namespace
#if defined(_MSC_VER) && defined(_DEBUG)
// Debug CRT assertions go to stderr instead of a dialog nobody sees under ctest.
#include <crtdbg.h>
static const int crtReport = [] { _CrtSetReportMode(_CRT_ASSERT, _CRTDBG_MODE_FILE); _CrtSetReportFile(_CRT_ASSERT, _CRTDBG_FILE_STDERR);
    _CrtSetReportMode(_CRT_ERROR, _CRTDBG_MODE_FILE); _CrtSetReportFile(_CRT_ERROR, _CRTDBG_FILE_STDERR); return 0; }();
#endif
QTEST_GUILESS_MAIN(TestLossyGbk)
#include "TestLossyGbk.moc"
