// Measures mojibake recovery on generated cases rather than hand-picked samples.
// Every sentence in data/mojibake_benchmark.json is encoded with one codec and
// wrongly decoded with another; the analyzer must rank the original first.
// Results are written to mojibake-benchmark.txt in the working directory.
#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include "ByteEncodingAnalyzer.h"
#include "EncodingConverter.h"
#include "MojibakeAnalyzer.h"
using namespace mojibake;
namespace {
struct Sentence { QString language, text; };
struct Tally {
    int total = 0, top1 = 0, top5 = 0;
    QStringList misses;
    double rate() const { return total ? double(top1) / total : 1.0; }
};
struct Mismatch { Encoding source, wrong; int layers = 1; };
QString pairName(const Mismatch& m) {
    return QStringLiteral("%1 read as %2%3").arg(EncodingConverter::name(m.source), EncodingConverter::name(m.wrong),
                                                m.layers > 1 ? QStringLiteral(" x%1").arg(m.layers) : QString());
}
// Text is only encoded with codecs that are really used for its language: Chinese
// stored as EUC-KR hanja or Japanese stored as GBK kana would inflate the
// numbers with histories that do not occur.
bool realistic(Encoding source, const QString& language, const QString& text) {
    if (language == "mixed") {
        // Mixed lines carry their own script: only pair them with that script's codecs.
        const auto has = [&text](char32_t from, char32_t to) {
            const auto ucs = text.toUcs4();
            return std::any_of(ucs.begin(), ucs.end(), [=](char32_t u) { return u >= from && u <= to; });
        };
        const bool kana = has(0x3041, 0x30ff), hangul = has(0xac00, 0xd7a3);
        switch (source) {
        case Encoding::Gbk: case Encoding::Gb18030: case Encoding::Big5: return !kana && !hangul;
        case Encoding::ShiftJis: return kana;
        case Encoding::EucKr: return hangul;
        case Encoding::Latin1: case Encoding::Windows1252: return false;
        default: return true;
        }
    }
    switch (source) {
    case Encoding::Gbk: case Encoding::Gb18030: return language == "zh_Hans" || language == "zh_Hant" || language == "en";
    case Encoding::Big5: return language == "zh_Hant" || language == "en";
    case Encoding::ShiftJis: return language == "ja" || language == "en";
    case Encoding::EucKr: return language == "ko" || language == "en";
    case Encoding::Latin1: case Encoding::Windows1252: return language == "en";
    default: return true;
    }
}
std::optional<QString> damage(QString text, const Mismatch& m) {
    for (int i = 0; i < m.layers; ++i) {
        const auto bytes = EncodingConverter::encode(text, m.source);
        if (!bytes) return {};
        const auto decoded = EncodingConverter::decode(*bytes, m.wrong);
        if (!decoded) return {};
        text = *decoded;
    }
    return text;
}
}
class TestMojibakeBenchmark : public QObject {
    Q_OBJECT
    QList<Sentence> m_sentences;
    QFile m_report;
    QTextStream m_out;
    void line(const QString& text) { m_out << text << '\n'; m_out.flush(); qInfo().noquote() << text; }
    Tally summarize(const QString& title, const QMap<QString, Tally>& tallies) {
        line(QString()); line("## " + title);
        Tally all;
        for (auto it = tallies.cbegin(); it != tallies.cend(); ++it) {
            const auto& t = it.value();
            all.total += t.total; all.top1 += t.top1; all.top5 += t.top5;
            line(QStringLiteral("%1  top1 %2/%3 (%4%)  top5 %5/%3").arg(it.key(), -34).arg(t.top1, 3).arg(t.total, 3)
                     .arg(100.0 * t.rate(), 5, 'f', 1).arg(t.top5, 3));
            for (const auto& miss : t.misses.mid(0, 4)) line("    miss: " + miss);
        }
        line(QStringLiteral("TOTAL  top1 %1/%2 (%3%)  top5 %4/%2").arg(all.top1).arg(all.total).arg(100.0 * all.rate(), 0, 'f', 1).arg(all.top5));
        return all;
    }
    // Regression floors, set a little below the 2026-11-08 results (clean 100%,
    // text 95.4%, files 99.6%; before common-character scoring: 61.3% and 58.9%).
    // Raise them when recovery improves; never lower them to make a change pass.
    static void floor(const Tally& all, double minimum, const char* what) {
        QVERIFY2(all.rate() >= minimum, qPrintable(QStringLiteral("%1: %2% is below the %3% floor").arg(what).arg(100.0 * all.rate(), 0, 'f', 1).arg(100.0 * minimum)));
    }
private slots:
    void initTestCase() {
        QFile file(QString(TEST_DATA_DIR) + "/mojibake_benchmark.json");
        QVERIFY(file.open(QIODevice::ReadOnly));
        const auto root = QJsonDocument::fromJson(file.readAll()).object();
        for (const auto& language : {"zh_Hans", "zh_Hant", "ja", "ko", "en", "mixed"})
            for (const auto& value : root.value(language).toArray()) m_sentences.append({language, value.toString()});
        QVERIFY(m_sentences.size() >= 80);
        m_report.setFileName("mojibake-benchmark.txt");
        QVERIFY(m_report.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text));
        m_out.setDevice(&m_report); m_out.setEncoding(QStringConverter::Utf8);
        line(QStringLiteral("# Mojibake benchmark (%1 sentences)").arg(m_sentences.size()));
    }
    void cleanTextIsKept() {
        // Correct text, including rare characters and accents, must be left alone.
        QMap<QString, Tally> tallies;
        for (const auto& s : m_sentences) {
            auto& t = tallies[s.language];
            const auto result = MojibakeAnalyzer::analyze(s.text);
            ++t.total;
            const bool kept = !result.likelyMojibake && result.candidates.first().text == s.text;
            if (kept) { ++t.top1; ++t.top5; } else t.misses.append(s.text + "  ->  " + result.candidates.first().text);
        }
        const auto all = summarize("Clean text kept unchanged", tallies);
        QCOMPARE(all.top1, all.total); // Never "repair" correct text.
    }
    void textRecovery() {
        const QList<Mismatch> mismatches{
            {Encoding::Utf8, Encoding::Gbk}, {Encoding::Utf8, Encoding::Latin1}, {Encoding::Utf8, Encoding::Windows1252},
            {Encoding::Utf8, Encoding::Big5}, {Encoding::Utf8, Encoding::ShiftJis}, {Encoding::Utf8, Encoding::EucKr},
            {Encoding::Gbk, Encoding::Latin1}, {Encoding::Gbk, Encoding::Windows1252}, {Encoding::Big5, Encoding::Latin1},
            {Encoding::ShiftJis, Encoding::Latin1}, {Encoding::EucKr, Encoding::Latin1},
            {Encoding::Gbk, Encoding::Big5}, {Encoding::Big5, Encoding::Gbk},
            {Encoding::Utf8, Encoding::Gbk, 2}, {Encoding::Utf8, Encoding::Windows1252, 2},
        };
        QMap<QString, Tally> tallies;
        for (const auto& m : mismatches) {
            auto& t = tallies[pairName(m)];
            for (const auto& s : m_sentences) {
                if (!realistic(m.source, s.language, s.text)) continue;
                const auto input = damage(s.text, m);
                if (!input || *input == s.text) continue; // Not representable, or the mismatch changes nothing.
                const auto result = MojibakeAnalyzer::analyze(*input);
                ++t.total;
                int rank = 0;
                for (int i = 0; i < result.candidates.size(); ++i) if (result.candidates[i].text == s.text) { rank = i + 1; break; }
                if (rank == 1) ++t.top1; else t.misses.append(QStringLiteral("%1 -> %2 (rank %3)").arg(s.text, result.candidates.first().text).arg(rank));
                if (rank >= 1) ++t.top5;
            }
        }
        const auto all = summarize("Damaged text recovered as the first candidate", tallies);
        floor(all, 0.93, "text recovery");
        QCOMPARE(all.top5, all.total); // The original must always be among the five candidates.
    }
    void fileDetection() {
        QMap<QString, Tally> tallies;
        const auto check = [&](const QString& key, const QString& text, Encoding e) {
            const auto bytes = EncodingConverter::encode(text, e);
            if (!bytes) return;
            auto& t = tallies[key];
            const auto result = ByteEncodingAnalyzer::analyze(*bytes);
            ++t.total;
            const bool first = !result.candidates.isEmpty() && result.candidates.first().text == text;
            const bool listed = std::any_of(result.candidates.begin(), result.candidates.end(), [&](const auto& c) { return c.text == text; });
            if (first) ++t.top1; else t.misses.append(text.left(24) + "  ->  " + (result.candidates.isEmpty() ? QString("(none)") : result.candidates.first().text.left(24)));
            if (listed) ++t.top5;
        };
        const QList<Encoding> encodings{Encoding::Utf8, Encoding::Gbk, Encoding::Big5, Encoding::ShiftJis, Encoding::EucKr, Encoding::Windows1252};
        for (auto e : encodings) {
            // Single lines are realistic for short notes; paragraphs for ordinary files.
            for (const auto& s : m_sentences)
                if (s.text.size() >= 6 && realistic(e, s.language, s.text)) check(EncodingConverter::name(e) + " line", s.text, e);
            QMap<QString, QStringList> paragraphs;
            for (const auto& s : m_sentences) if (realistic(e, s.language, s.text)) paragraphs[s.language].append(s.text);
            for (const auto& lines : paragraphs) check(EncodingConverter::name(e) + " paragraph", lines.join('\n'), e);
        }
        const auto all = summarize("Text files decoded with the right encoding first", tallies);
        floor(all, 0.97, "file detection");
    }
};
QTEST_GUILESS_MAIN(TestMojibakeBenchmark)
#include "TestMojibakeBenchmark.moc"
