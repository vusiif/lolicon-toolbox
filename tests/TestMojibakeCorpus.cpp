// Measures mojibake repair on whole documents with known originals.
// data/corpus/<document>/ holds ground_truth.txt, manifest.json and, per damage
// case, cases/<id>.txt (the damaged text as a user sees it, saved as UTF-8) and
// usually raw/<id>.bin (the bytes before the last wrong decode).
//
// Each case is repaired the ways the app offers: pasting the text, opening the
// damaged text file, opening the raw file, and, for GBK read as UTF-8 with
// replacement characters, the lossy reconstruction the AI refines. Scores are
// character error rates (edit distance / original length), written to
// mojibake-corpus.txt in the working directory.
//
// data/corpus_baseline.json records the error rate of every measurement; a
// measurement that gets worse fails. When recovery improves, regenerate it with
// MOJIBAKE_CORPUS_BASELINE_OUT=<path> and review the diff; never raise an entry
// to make a change pass.
//
// MOJIBAKE_CORPUS_GUIDES=<dir> scores external repairs (an AI's text, for
// example) of the lossy cases: <dir>/<document>/<case id>.txt is checked with
// LossyGbkRecovery::guided like an AI reply, and reported without a baseline.
#include <QTest>
#include <QDir>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include "ByteEncodingAnalyzer.h"
#include "LossyGbkRecovery.h"
#include "MojibakeAnalyzer.h"
using namespace mojibake;
namespace {
struct Case {
    QString document, id, group;
    QString damaged;            // cases/<id>.txt decoded as UTF-8
    QByteArray damagedBytes;    // the same file as bytes
    QByteArray raw;             // raw/<id>.bin, empty when the case has none
    bool preserved = false;     // the declared operations kept every byte
    bool lossyGbk = false;      // GBK bytes decoded as UTF-8 with replacement characters
    QString key(const char* path) const { return document + '/' + id + '/' + path; }
};
// The generator writes a BOM when the original had one; the analyzers may keep
// or drop it, which is not what is measured here.
QString normalized(QString text) {
    if (text.startsWith(QChar(0xfeff))) text.remove(0, 1);
    return text;
}
QByteArray readAll(const QString& path) {
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
// Levenshtein distance over the original's length. The shared prefix and suffix
// are skipped first: most repairs differ from the original in a few places only.
double errorRate(const QString& text, const QString& truth) {
    qsizetype head = 0;
    while (head < text.size() && head < truth.size() && text[head] == truth[head]) ++head;
    qsizetype tail = 0;
    while (tail < text.size() - head && tail < truth.size() - head && text[text.size() - 1 - tail] == truth[truth.size() - 1 - tail]) ++tail;
    const QStringView a = QStringView(text).mid(head, text.size() - head - tail), b = QStringView(truth).mid(head, truth.size() - head - tail);
    std::vector<int> row(b.size() + 1);
    for (qsizetype j = 0; j <= b.size(); ++j) row[j] = static_cast<int>(j);
    for (qsizetype i = 1; i <= a.size(); ++i) {
        int diagonal = row[0];
        row[0] = static_cast<int>(i);
        for (qsizetype j = 1; j <= b.size(); ++j) {
            const int up = row[j];
            row[j] = std::min({row[j] + 1, row[j - 1] + 1, diagonal + (a[i - 1] != b[j - 1])});
            diagonal = up;
        }
    }
    return static_cast<double>(row[b.size()]) / static_cast<double>(std::max<qsizetype>(1, truth.size()));
}
QString percent(const double rate) { return QStringLiteral("%1%").arg(100.0 * rate, 6, 'f', 2); }
class TestMojibakeCorpus : public QObject {
    Q_OBJECT
    QMap<QString, QString> m_truth;  // document -> original text
    QList<Case> m_cases;
    QJsonObject m_baseline, m_measured;
    QStringList m_regressions;
    QFile m_report;
    QTextStream m_out;
    void line(const QString& text) { m_out << text << '\n'; m_out.flush(); qInfo().noquote() << text; }
    // Records a measurement and compares it with the baseline.
    void measure(const QString& key, const double rate) {
        m_measured.insert(key, std::round(rate * 1e5) / 1e5);
        if (!m_baseline.contains(key)) { line("    (no baseline for " + key + ")"); return; }
        if (const auto base = m_baseline.value(key).toDouble(); rate > base + 0.0005)
            m_regressions.append(QStringLiteral("%1: %2 -> %3").arg(key, percent(base), percent(rate)));
    }
    void row(const Case& c, const QString& outcome, const double rate, const qint64 ms) {
        line(QStringLiteral("%1  %2  %3  CER %4  %5 ms").arg(c.document.left(8), -8).arg(c.id, -24).arg(outcome, -18).arg(percent(rate)).arg(ms, 6));
    }
    template <typename List> static int rankOf(const List& candidates, const QString& truth) {
        for (int i = 0; i < candidates.size(); ++i) if (normalized(candidates[i].text) == truth) return i + 1;
        return 0;
    }
    static QString outcome(const int rank, const bool lossy) {
        if (rank == 1) return QStringLiteral("exact");
        if (rank > 1) return QStringLiteral("exact at rank %1").arg(rank);
        return lossy ? QStringLiteral("lossy rebuild") : QStringLiteral("wrong");
    }
private slots:
    void initTestCase() {
        const QDir root(QStringLiteral(TEST_DATA_DIR "/corpus"));
        for (const auto& document : root.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
            const QDir dir(root.filePath(document));
            const auto truth = readAll(dir.filePath("ground_truth.txt"));
            const auto manifest = QJsonDocument::fromJson(readAll(dir.filePath("manifest.json"))).object();
            QVERIFY2(!truth.isEmpty() && !manifest.isEmpty(), qPrintable(document));
            m_truth.insert(document, normalized(QString::fromUtf8(truth)));
            for (const auto& value : manifest.value("cases").toArray()) {
                const auto o = value.toObject();
                if (o.value("status").toString() != "ok") continue;  // The generator could not apply the damage.
                Case c{.document = document, .id = o.value("id").toString(), .group = o.value("group").toString()};
                c.damagedBytes = readAll(dir.filePath(o.value("text_file").toString()));
                QVERIFY2(!c.damagedBytes.isEmpty(), qPrintable(c.key("text")));
                c.damaged = QString::fromUtf8(c.damagedBytes);
                if (const auto raw = o.value("raw_file").toString(); !raw.isEmpty()) c.raw = readAll(dir.filePath(raw));
                c.preserved = o.value("information_preserved_through_declared_operations").toBool();
                const auto steps = o.value("steps").toArray();
                const auto last = steps.isEmpty() ? QJsonObject() : steps.last().toObject();
                c.lossyGbk = steps.size() == 1 && last.value("operation") == "encode_then_decode" && last.value("encode") == "gbk"
                          && last.value("decode") == "utf-8" && last.value("decode_errors") == "replace";
                m_cases.append(c);
            }
        }
        QVERIFY(m_truth.size() >= 4);
        QVERIFY(m_cases.size() >= 60);
        m_baseline = QJsonDocument::fromJson(readAll(QStringLiteral(TEST_DATA_DIR "/corpus_baseline.json"))).object();
        m_report.setFileName("mojibake-corpus.txt");
        QVERIFY(m_report.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text));
        m_out.setDevice(&m_report); m_out.setEncoding(QStringConverter::Utf8);
        line(QStringLiteral("# Mojibake corpus (%1 documents, %2 cases)").arg(m_truth.size()).arg(m_cases.size()));
        if (m_baseline.isEmpty()) line("No baseline: every measurement is reported only.");
    }
    // The damaged text pasted into the mojibake page.
    void pastedText() {
        line(QString()); line("## Pasted damaged text (MojibakeAnalyzer)");
        for (const auto& c : m_cases) {
            const auto& truth = m_truth[c.document];
            QElapsedTimer timer; timer.start();
            const auto result = MojibakeAnalyzer::analyze(c.damaged);
            const auto ms = timer.elapsed();
            const auto top = result.candidates.isEmpty() ? c.damaged : normalized(result.candidates.first().text);
            const bool lossy = !result.candidates.isEmpty() && result.candidates.first().lossy;
            const auto rate = errorRate(top, truth);
            row(c, outcome(rankOf(result.candidates, truth), lossy), rate, ms);
            measure(c.key("paste"), rate);
        }
    }
    // The damaged text file opened in the text file page.
    void openedTextFile() {
        line(QString()); line("## Opened damaged text file (ByteEncodingAnalyzer)");
        for (const auto& c : m_cases) {
            const auto& truth = m_truth[c.document];
            QElapsedTimer timer; timer.start();
            const auto result = ByteEncodingAnalyzer::analyze(c.damagedBytes);
            const auto ms = timer.elapsed();
            const auto top = result.candidates.isEmpty() ? c.damaged : normalized(result.candidates.first().text);
            const bool lossy = !result.candidates.isEmpty() && result.candidates.first().lossy;
            const auto rate = errorRate(top, truth);
            row(c, outcome(rankOf(result.candidates, truth), lossy), rate, ms);
            measure(c.key("file"), rate);
        }
    }
    // The bytes before the last wrong decode, as an unopened file still holds them.
    void openedRawFile() {
        line(QString()); line("## Opened raw file (ByteEncodingAnalyzer)");
        for (const auto& c : m_cases) {
            if (c.raw.isEmpty()) continue;
            const auto& truth = m_truth[c.document];
            QElapsedTimer timer; timer.start();
            const auto result = ByteEncodingAnalyzer::analyze(c.raw);
            const auto ms = timer.elapsed();
            const auto top = result.candidates.isEmpty() ? QString() : normalized(result.candidates.first().text);
            const bool lossy = !result.candidates.isEmpty() && result.candidates.first().lossy;
            const auto rate = errorRate(top, truth);
            auto label = outcome(rankOf(result.candidates, truth), lossy);
            if (result.candidates.isEmpty())
                label = result.probablyBinary ? QStringLiteral("refused: binary") : result.error.isEmpty() ? QStringLiteral("no candidate") : result.error.left(18);
            row(c, label, rate, ms);
            measure(c.key("raw"), rate);
        }
    }
    // The local reconstruction the AI refines, and how far a guide can take it.
    void lossyGbk() {
        line(QString()); line("## Lossy GBK reconstruction (LossyGbkRecovery)");
        line("local: the search alone. top-5: the best an AI choosing among the offered alternatives can do.");
        line("truth guide: guided() with the original as the guide, the ceiling for any AI reply.");
        const auto guides = qEnvironmentVariable("MOJIBAKE_CORPUS_GUIDES");
        for (const auto& c : m_cases) {
            if (!c.lossyGbk) continue;
            const auto& truth = m_truth[c.document];
            QElapsedTimer timer; timer.start();
            const auto r = LossyGbkRecovery::recover(c.damaged);
            const auto ms = timer.elapsed();
            QVERIFY2(r.success, qPrintable(c.key("lossy")));
            const auto rate = errorRate(normalized(r.text), truth);
            line(QStringLiteral("%1  %2  local        CER %3  exact %4 partial %5 unknown %6, %7 vs %8 characters, %9 ms")
                     .arg(c.document.left(8), -8).arg(c.id, -24).arg(percent(rate)).arg(r.exact).arg(r.partial).arg(r.unknown)
                     .arg(r.text.size()).arg(truth.size()).arg(ms));
            // □ stands for lost bytes, so the text itself cannot reproduce the damage; the bytes behind it must.
            QVERIFY2(LossyGbkRecovery::lossyUtf8(r.bytes, r.replacement) == c.damaged, qPrintable(c.key("lossy")));
            measure(c.key("lossy"), rate);
            // Partial characters are only comparable position by position when the lengths agree.
            const auto offset = r.text.startsWith(QChar(0xfeff)) ? 1 : 0;
            if (r.text.size() - offset == truth.size()) {
                int right = 0, listed = 0;
                QString ideal = r.text;
                for (qsizetype i = offset; i < r.text.size(); ++i) {
                    if (static_cast<CharEvidence>(r.evidence[i]) != CharEvidence::Partial) continue;
                    const auto original = truth[i - offset];
                    const auto options = LossyGbkRecovery::alternatives(r, i, 5);
                    right += r.text[i] == original;
                    listed += options.contains(original);
                    if (options.contains(original)) ideal[i] = original;
                }
                const auto top5 = LossyGbkRecovery::guided(c.damaged, ideal, r.replacement);
                line(QStringLiteral("%1  %2  top-5        CER %3  partial right %4/%5, original listed %6/%5")
                         .arg(QString(), -8).arg(QString(), -24).arg(percent(errorRate(normalized(top5.text), truth))).arg(right).arg(r.partial).arg(listed));
            }
            const auto ceiling = LossyGbkRecovery::guided(c.damaged, truth, r.replacement);
            line(QStringLiteral("%1  %2  truth guide  CER %3  guided %4 unknown %5 rejected %6")
                     .arg(QString(), -8).arg(QString(), -24).arg(percent(errorRate(normalized(ceiling.text), truth)))
                     .arg(ceiling.guided).arg(ceiling.unknown).arg(ceiling.guideRejected));
            if (guides.isEmpty()) continue;
            const auto guide = readAll(QDir(guides).filePath(c.document + '/' + c.id + ".txt"));
            if (guide.isEmpty()) { line("    (no external guide)"); continue; }
            const auto text = QString::fromUtf8(guide);
            const auto checked = LossyGbkRecovery::guided(c.damaged, text, r.replacement);
            line(QStringLiteral("%1  %2  external     CER %3  as given %4, consistent as given %5; verified: guided %6 unknown %7 rejected %8")
                     .arg(QString(), -8).arg(QString(), -24).arg(percent(errorRate(normalized(checked.text), truth)))
                     .arg(percent(errorRate(normalized(text), truth))).arg(LossyGbkRecovery::consistent(text, c.damaged, r.replacement))
                     .arg(checked.guided).arg(checked.unknown).arg(checked.guideRejected));
        }
    }
    void cleanupTestCase() {
        if (const auto path = qEnvironmentVariable("MOJIBAKE_CORPUS_BASELINE_OUT"); !path.isEmpty()) {
            QFile out(path);
            QVERIFY(out.open(QIODevice::WriteOnly | QIODevice::Truncate));
            out.write(QJsonDocument(m_measured).toJson(QJsonDocument::Indented));
            line("Baseline written to " + path);
        }
        line(QString());
        line(m_regressions.isEmpty() ? QStringLiteral("No measurement is worse than the baseline.")
                                     : QStringLiteral("Worse than the baseline:\n  ") + m_regressions.join("\n  "));
        QVERIFY2(m_regressions.isEmpty(), qPrintable(m_regressions.join("; ")));
    }
};
}  // namespace
QTEST_GUILESS_MAIN(TestMojibakeCorpus)
#include "TestMojibakeCorpus.moc"
