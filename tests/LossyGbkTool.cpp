// Manual tool, not a ctest test: the local lossy GBK repair as commands, so an
// agent (or a person) can work on a damaged text with the same checks the app
// uses, and be scored against the original afterwards.
//
//   LossyGbkTool damage <original> <damaged out>   per-byte damage (Qt, .NET): counts survive
//   LossyGbkTool draft <damaged>                    the local draft, damaged spans as ⟦id:...⟧
//   LossyGbkTool options <damaged> <id> [limit]     every character that fits each byte of a span
//   LossyGbkTool apply <damaged> <fills.json> <out> splice {"id": "text"} in, re-solve against the bytes
//   LossyGbkTool score <original> <damaged> <text>  compare a repair with the original
//   LossyGbkTool validate <text> <damaged>          does text, encoded as GBK, decode to damaged? (whole text)
//   LossyGbkTool check <damaged> <fills.json> [span] [--joint]  does each fill reproduce its span's damage? (JSON lines)
//                                                   --joint: next to the other fills, not the local guesses
//                                                   (□ in a fill keeps the local bytes of that character)
//   LossyGbkTool candidates <damaged> [limit] [beam] [span]  likeliest fitting fills of every span (JSON lines)
//   LossyGbkTool partials <damaged> [limit]         every partial character's options, local choice first (JSON lines)
//
// Unit ids are stable for a given damaged file: recover() is deterministic.
// span caps the characters of one span (default 8, as draft/options/apply);
// 0 means whole runs, which check and candidates read with exact neighbours.
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QStringEncoder>
#include <QTextStream>
#include <cmath>
#include <limits>
#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif
#include "LossyGbkRecovery.h"
using namespace mojibake;
namespace {
constexpr int UnitCharacters = 8;  // as MojibakeAi::UnitCharacters
QTextStream out(stdout);
QString readText(const QString& path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { out << "cannot read " << path << Qt::endl; std::exit(2); }
    auto text = QString::fromUtf8(file.readAll());
    if (text.startsWith(QChar(0xfeff))) text.remove(0, 1);
    return text;
}
void writeText(const QString& path, const QString& text) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) { out << "cannot write " << path << Qt::endl; std::exit(2); }
    file.write(text.toUtf8());
}
struct Local { QString damaged; LossyRecovery r; QList<RepairUnit> units; };
Local load(const QString& path, const int span = UnitCharacters) {
    Local l{.damaged = readText(path)};
    l.r = LossyGbkRecovery::recover(l.damaged);
    if (!l.r.success) { out << "local recovery failed" << Qt::endl; std::exit(2); }
    l.units = LossyGbkRecovery::repairUnits(l.r, 0, l.r.text.size(), span > 0 ? span : std::numeric_limits<int>::max());
    return l;
}
QJsonObject describe(const Local& l, const qsizetype k) {
    const auto& u = l.units[k];
    const auto ev = l.r.evidence.mid(u.textBegin, u.textEnd - u.textBegin);
    return {{"id", static_cast<int>(k + 1)}, {"begin", static_cast<int>(u.textBegin)}, {"end", static_cast<int>(u.textEnd)},
            {"lost", static_cast<int>(ev.count(static_cast<char>(CharEvidence::Unknown)))},
            {"partial", static_cast<int>(ev.count(static_cast<char>(CharEvidence::Partial)))},
            {"local", l.r.text.mid(u.textBegin, u.textEnd - u.textBegin)}, {"evidence", QString::fromLatin1(ev)}};
}
void line(const QJsonObject& o) { out << QString::fromUtf8(QJsonDocument(o).toJson(QJsonDocument::Compact)) << '\n'; }
}
int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
#ifdef Q_OS_WIN
    // Text mode would turn each \n into \r\n, and the draft's own \r\n into \r\r\n.
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    out.setEncoding(QStringConverter::Utf8);
    const auto a = app.arguments();
    const QString command = a.value(1);
    if (command == "damage" && a.size() == 4) {
        QStringEncoder gbk("GBK", QStringConverter::Flag::Stateless);
        const QByteArray bytes = gbk(readText(a[2]));
        if (gbk.hasError()) { out << "not GBK encodable" << Qt::endl; return 2; }
        writeText(a[3], LossyGbkRecovery::lossyUtf8(bytes, Utf8Replacement::PerByte));
        out << "written" << Qt::endl;  // never print the original
        return 0;
    }
    if (command == "draft" && a.size() == 3) {
        const auto l = load(a[2]);
        out << QStringLiteral("%1 characters: %2 certain, %3 partial, %4 lost; %5 spans; replacement %6\n")
                   .arg(l.r.text.size()).arg(l.r.exact).arg(l.r.partial).arg(l.r.unknown).arg(l.units.size())
                   .arg(l.r.replacement == Utf8Replacement::PerByte ? "per-byte (counts exact)" : "maximal subpart (counts approximate)");
        out << "glossary: " << LossyGbkRecovery::glossary(l.r, 400).join(u'、') << "\n\n";
        out << LossyGbkRecovery::unitMarkup(l.r, 0, l.r.text.size(), l.units, 5) << Qt::endl;
        return 0;
    }
    if (command == "options" && a.size() >= 4) {
        const auto l = load(a[2]);
        const int id = a[3].toInt(), limit = a.size() > 4 ? a[4].toInt() : 30;
        if (id < 1 || id > l.units.size()) { out << "no such span" << Qt::endl; return 2; }
        const auto& u = l.units[id - 1];
        out << QStringLiteral("span %1: length %2..%3\n").arg(id).arg(u.minLength).arg(u.maxLength);
        for (auto i = u.textBegin; i < u.textEnd; ++i) {
            const auto e = static_cast<CharEvidence>(l.r.evidence[i]);
            out << "  " << (i - u.textBegin + 1) << ": "
                << (e == CharEvidence::Partial ? LossyGbkRecovery::alternatives(l.r, i, limit) : QStringLiteral("(lost: any character)")) << '\n';
        }
        return 0;
    }
    if (command == "apply" && a.size() == 5) {
        const auto l = load(a[2]);
        QFile file(a[3]);
        if (!file.open(QIODevice::ReadOnly)) { out << "cannot read fills" << Qt::endl; return 2; }
        const auto json = QJsonDocument::fromJson(file.readAll()).object();
        QHash<int, QString> fills;
        for (auto it = json.begin(); it != json.end(); ++it) if (it.value().isString()) fills.insert(it.key().toInt(), it.value().toString());
        int wrongLength = 0;
        const auto guide = LossyGbkRecovery::unitGuide(l.r, 0, l.r.text.size(), l.units, fills, &wrongLength);
        const auto result = LossyGbkRecovery::guided(l.damaged, guide, l.r.replacement);
        writeText(a[4], result.text);
        out << QStringLiteral("filled %1 of %2 spans; %3 wrong length; %4 characters contradicted the bytes; result: %5 guided, %6 unknown\n")
                   .arg(fills.size()).arg(l.units.size()).arg(wrongLength).arg(result.guideRejected).arg(result.guided).arg(result.unknown);
        // Spans whose verified text differs from the fill: the bytes overruled it.
        if (result.text.size() == l.r.text.size())
            for (qsizetype k = 0; k < l.units.size(); ++k) {
                const auto& u = l.units[k];
                const auto fill = fills.value(static_cast<int>(k + 1));
                const auto got = result.text.mid(u.textBegin, u.textEnd - u.textBegin);
                if (!fill.isEmpty() && fill.size() == got.size() && fill != got) out << "  span " << (k + 1) << ": wrote " << fill << ", bytes allow " << got << '\n';
            }
        out.flush();
        return 0;
    }
    if (command == "score" && a.size() == 5) {
        const auto truth = readText(a[2]);
        const auto l = load(a[3]);
        const auto text = readText(a[4]);
        if (text.size() != truth.size() || l.r.text.size() != truth.size()) { out << "lengths differ; use per-byte damage" << Qt::endl; return 2; }
        int partial = 0, partialLocal = 0, partialText = 0, lost = 0, lostText = 0, lostBox = 0, exactChanged = 0;
        for (qsizetype i = 0; i < truth.size(); ++i)
            switch (static_cast<CharEvidence>(l.r.evidence[i])) {
            case CharEvidence::Exact: exactChanged += text[i] != truth[i]; break;
            case CharEvidence::Partial: ++partial; partialLocal += l.r.text[i] == truth[i]; partialText += text[i] == truth[i]; break;
            default: ++lost; lostText += text[i] == truth[i]; lostBox += text[i] == QChar(LossyGbkRecovery::UnknownMark); break;
            }
        const auto pct = [](int x, int n) { return QStringLiteral("%1/%2 (%3%)").arg(x).arg(n).arg(n ? 100.0 * x / n : 0, 0, 'f', 1); };
        out << "partial: local " << pct(partialLocal, partial) << ", this text " << pct(partialText, partial) << '\n'
            << "lost: this text " << pct(lostText, lost) << ", still □ " << pct(lostBox, lost) << '\n'
            << "all characters right: " << pct(truth.size() - static_cast<int>(std::ranges::count_if(std::views::iota(qsizetype{0}, truth.size()), [&](qsizetype i) { return text[i] != truth[i]; })), static_cast<int>(truth.size()))
            << "; certain characters changed: " << exactChanged << Qt::endl;
        return 0;
    }
    if (command == "validate" && a.size() == 4) {
        const auto text = readText(a[2]), damaged = readText(a[3]);
        const bool perByte = LossyGbkRecovery::consistent(text, damaged, Utf8Replacement::PerByte);
        const bool subpart = LossyGbkRecovery::consistent(text, damaged, Utf8Replacement::MaximalSubpart);
        out << (perByte && subpart ? "consistent (either replacement)" : perByte ? "consistent (per-byte)" : subpart ? "consistent (maximal subpart)" : "inconsistent") << Qt::endl;
        return perByte || subpart ? 0 : 1;
    }
    if (command == "check" && a.size() >= 4 && a.size() <= 6) {
        // --joint: every fill is spliced in first, so each is checked against the
        // other fills rather than against the local guesses next to it.
        const bool joint = a.contains("--joint");
        const auto rest = a.mid(4).filter(QRegularExpression("^\\d+$"));
        auto l = load(a[2], rest.isEmpty() ? UnitCharacters : rest[0].toInt());
        QFile file(a[3]);
        if (!file.open(QIODevice::ReadOnly)) { out << "cannot read fills" << Qt::endl; return 2; }
        const auto json = QJsonDocument::fromJson(file.readAll()).object();
        if (joint) {
            QStringEncoder gbk("GBK", QStringConverter::Flag::Stateless);
            for (auto it = json.begin(); it != json.end(); ++it) {
                const int id = it.key().toInt();
                if (id < 1 || id > l.units.size()) continue;
                const auto& u = l.units[id - 1];
                const auto fill = it.value().toString();
                if (fill.size() != u.textEnd - u.textBegin) continue;
                qsizetype at = 0;
                for (qsizetype i = 0; i < u.textBegin; ++i) at += l.r.text[i].unicode() < 0x80 ? 1 : 2;
                // Character by character: □ in a fill keeps the reconstruction's own bytes there.
                for (qsizetype k = 0; k < fill.size(); ++k, at += 2) {
                    if (fill[k] == QChar(LossyGbkRecovery::UnknownMark)) continue;
                    const QByteArray bytes = gbk(fill.mid(k, 1));
                    if (gbk.hasError() || bytes.size() != 2) { gbk.resetState(); continue; }
                    l.r.bytes.replace(at, 2, bytes);
                    l.r.text[u.textBegin + k] = fill[k];
                }
            }
        }
        for (auto it = json.begin(); it != json.end(); ++it) {
            const int id = it.key().toInt();
            if (id < 1 || id > l.units.size()) { line({{"id", id}, {"error", "no such span"}}); continue; }
            const auto& u = l.units[id - 1];
            const auto fill = it.value().toString();
            auto o = describe(l, id - 1);
            o.insert("fill", fill);
            if (fill.size() != u.textEnd - u.textBegin) o.insert("error", QStringLiteral("length %1, span has %2").arg(fill.size()).arg(u.textEnd - u.textBegin));
            o.insert("fits", LossyGbkRecovery::fits(l.r, u.textBegin, u.textEnd, fill));
            line(o);
        }
        out.flush();
        return 0;
    }
    if (command == "partials" && (a.size() == 3 || a.size() == 4)) {
        // Every partial character with the characters that keep its surviving byte and reproduce the damage there.
        const auto l = load(a[2]);
        const int limit = a.size() == 4 ? a[3].toInt() : 400;
        for (qsizetype i = 0; i < l.r.text.size(); ++i)
            if (static_cast<CharEvidence>(l.r.evidence[i]) == CharEvidence::Partial)
                line({{"index", static_cast<int>(i)}, {"options", LossyGbkRecovery::alternatives(l.r, i, limit)}});
        out.flush();
        return 0;
    }
    if (command == "candidates" && a.size() >= 3 && a.size() <= 6) {
        const auto l = load(a[2], a.size() > 5 ? a[5].toInt() : UnitCharacters);
        const int limit = a.size() > 3 ? a[3].toInt() : 20, beam = a.size() > 4 ? a[4].toInt() : 128;
        for (qsizetype k = 0; k < l.units.size(); ++k) {
            const auto& u = l.units[k];
            QJsonArray list;
            for (const auto& c : LossyGbkRecovery::candidates(l.r, u.textBegin, u.textEnd, limit, beam))
                list.append(QJsonObject{{"text", c.text}, {"score", std::round(c.score * 100) / 100}});
            auto o = describe(l, k);
            o.insert("candidates", list);
            line(o);
        }
        out.flush();
        return 0;
    }
    out << "usage: see the comment at the top of LossyGbkTool.cpp" << Qt::endl;
    return 2;
}
