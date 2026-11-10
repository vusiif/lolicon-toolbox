#include "LossyGbkRecovery.h"
#include <QFile>
#include <QHash>
#include <QStringDecoder>
#include <QThreadPool>
#include <QtConcurrentMap>
#include <QtEndian>
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
namespace mojibake {
namespace {
// ---------------------------------------------------------------- UTF-8 rules
// Bytes needed after a lead byte, or -1 if the byte cannot start a sequence.
int trailCount(quint8 lead) {
    if (lead >= 0xc2 && lead <= 0xdf) return 1;
    if (lead >= 0xe0 && lead <= 0xef) return 2;
    if (lead >= 0xf0 && lead <= 0xf4) return 3;
    return -1;
}
// Whether b may follow the bytes already pending (maximal subpart rule).
bool continues(const quint8* pending, int length, quint8 b) {
    if (length == 1) {
        switch (pending[0]) {
        case 0xe0: return b >= 0xa0 && b <= 0xbf;
        case 0xed: return b >= 0x80 && b <= 0x9f;
        case 0xf0: return b >= 0x90 && b <= 0xbf;
        case 0xf4: return b >= 0x80 && b <= 0x8f;
        default: break;
        }
    }
    return b >= 0x80 && b <= 0xbf;
}
// A token of the damaged text: U+FFFD (length 0) or the UTF-8 bytes of one code point.
struct Token { quint8 length = 0; std::array<quint8, 4> bytes{}; };
struct Pending {
    quint8 length = 0;
    std::array<quint8, 4> bytes{};  // At most three stay pending; the fourth completes a sequence.
    quint32 key() const { return length ? quint32(bytes[0]) | quint32(bytes[1]) << 8 | quint32(bytes[2]) << 16 : 0; }
};
// Decoder position while generating bytes: next expected token and pending bytes.
struct Cursor { int token = 0; Pending pending; };
// Feeds one byte through a lossy UTF-8 decoder and checks each emitted token
// against the damaged text. Returns false on contradiction.
// perByte: the decoder writes one U+FFFD per byte of a broken sequence (Qt, .NET)
// instead of one per maximal subpart (WHATWG, Python, ICU).
bool feed(Cursor& c, quint8 b, const Token* tokens, int end, bool perByte) {
    auto& p = c.pending;
    if (p.length) {
        if (continues(p.bytes.data(), p.length, b)) {
            p.bytes[p.length++] = b;
            if (p.length == trailCount(p.bytes[0]) + 1) {
                if (c.token >= end) return false;
                const auto& t = tokens[c.token];
                if (t.length != p.length || std::memcmp(t.bytes.data(), p.bytes.data(), p.length) != 0) return false;
                ++c.token; p = {};
            }
            return true;
        }
        const int replaced = perByte ? p.length : 1;
        for (int k = 0; k < replaced; ++k) {
            if (c.token >= end || tokens[c.token].length != 0) return false;
            ++c.token;
        }
        p = {};
    }
    if (b < 0x80) {
        if (c.token >= end || tokens[c.token].length != 1 || tokens[c.token].bytes[0] != b) return false;
        ++c.token; return true;
    }
    if (trailCount(b) < 0) {
        if (c.token >= end || tokens[c.token].length != 0) return false;
        ++c.token; return true;
    }
    p.length = 1; p.bytes[0] = b; p.bytes[1] = p.bytes[2] = 0;
    return true;
}
// Pending bytes must still be able to become the next token (or fail into U+FFFD).
bool viable(const Cursor& c, const Token* tokens, int end, bool perByte) {
    if (!c.pending.length) return true;
    if (c.token >= end) return false;
    const auto& t = tokens[c.token];
    if (t.length == 0) {
        if (!perByte) return true;
        for (int k = 1; k < c.pending.length; ++k) if (c.token + k >= end || tokens[c.token + k].length != 0) return false;
        return true;
    }
    return t.length > c.pending.length && std::memcmp(t.bytes.data(), c.pending.bytes.data(), c.pending.length) == 0;
}
// ------------------------------------------------------------- static tables
struct Choice { std::array<quint8, 2> bytes{}; quint8 length = 0; char16_t ch = 0; bool wild = false; };
struct Tables {
    std::array<std::array<char16_t, 192>, 128> gbk{};  // [lead - 0x80][trail - 0x40]
    std::vector<quint16> bytesOf = std::vector<quint16>(65536, 0);  // char -> lead << 8 | trail
    std::array<std::vector<Choice>, 256> byLead, byTrail;
    std::vector<Choice> unknownContext;  // frequent characters plus one placeholder per byte-class pair
    // Character model.
    std::vector<qint32> index = std::vector<qint32>(65536, -1);
    std::vector<float> logUnigram, logStart, startProbability, endCount, occurrences, unigramProbability;
    std::vector<quint32> pairOffset; std::vector<quint16> pairSecond; std::vector<float> pairCount;
    bool model = false;
    char16_t gbkChar(quint8 lead, quint8 trail) const {
        return lead >= 0x81 && lead <= 0xfe && trail >= 0x40 && trail != 0x7f && trail <= 0xfe ? gbk[lead - 0x80][trail - 0x40] : 0;
    }
};
bool loadModel(Tables& t) {
    QFile file(":/charlm/zh_charlm.bin");
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QByteArray data = file.readAll();
    const auto* p = reinterpret_cast<const uchar*>(data.constData());
    qsizetype at = 0;
    const auto u32 = [&](quint32& v) { if (at + 4 > data.size()) return false; v = qFromLittleEndian<quint32>(p + at); at += 4; return true; };
    const auto u16 = [&](quint16& v) { if (at + 2 > data.size()) return false; v = qFromLittleEndian<quint16>(p + at); at += 2; return true; };
    quint32 version = 0, total = 0, totalStarts = 0, n = 0, m = 0;
    if (!data.startsWith("LTCL")) return false;
    at = 4;
    if (!u32(version) || version != 1 || !u32(total) || !u32(totalStarts) || !u32(n) || !total || !totalStarts) return false;
    std::vector<char16_t> codes(n);
    t.logUnigram.resize(n); t.logStart.resize(n); t.startProbability.resize(n); t.endCount.resize(n);
    t.occurrences.resize(n); t.unigramProbability.resize(n);
    for (quint32 k = 0; k < n; ++k) {
        quint16 code; quint32 occurrences, ends, starts;
        if (!u16(code) || !u32(occurrences) || !u32(ends) || !u32(starts) || !occurrences) return false;
        codes[k] = code; t.index[code] = qint32(k);
        t.occurrences[k] = float(occurrences); t.endCount[k] = float(ends);
        t.unigramProbability[k] = float(double(occurrences) / total);
        t.logUnigram[k] = float(std::log(double(occurrences) / total));
        t.startProbability[k] = float((starts + 0.5) / totalStarts);
        t.logStart[k] = std::log(t.startProbability[k]);
    }
    if (!u32(m)) return false;
    t.pairOffset.assign(n + 1, 0); t.pairSecond.resize(m); t.pairCount.resize(m);
    quint16 previous = 0;
    for (quint32 k = 0; k < m; ++k) {
        quint16 a, b; quint32 count;
        if (!u16(a) || !u16(b) || !u32(count) || a >= n || b >= n || a < previous) return false;
        previous = a; ++t.pairOffset[a + 1]; t.pairSecond[k] = b; t.pairCount[k] = float(count);
    }
    for (quint32 k = 0; k < n; ++k) t.pairOffset[k + 1] += t.pairOffset[k];
    return true;
}
Tables build() {
    Tables t;
    QStringDecoder decoder("GBK", QStringConverter::Flag::Stateless);
    for (int lead = 0x81; lead <= 0xfe; ++lead)
        for (int trail = 0x40; trail <= 0xfe; ++trail) {
            if (trail == 0x7f) continue;
            const char pair[2] = {char(lead), char(trail)};
            const QString text = decoder.decode(QByteArrayView(pair, 2));
            if (decoder.hasError()) { decoder.resetState(); continue; }
            if (text.size() != 1 || text[0] == QChar(0xfffd) || text[0].unicode() < 0x80) continue;
            const char16_t ch = text[0].unicode();
            // Private-use (user-defined) rows are not text a person typed.
            if (ch >= 0xe000 && ch <= 0xf8ff) continue;
            t.gbk[lead - 0x80][trail - 0x40] = ch;
            if (!t.bytesOf[ch]) t.bytesOf[ch] = quint16(lead << 8 | trail);
            const Choice c{{quint8(lead), quint8(trail)}, 2, ch, false};
            t.byLead[lead].push_back(c); t.byTrail[trail].push_back(c);
        }
    t.model = loadModel(t);
    // Characters tried where no byte of the character survived. Their identity
    // only shapes the context of neighbours (they are displayed as unknown), so
    // a short list of the most frequent characters is enough.
    std::vector<std::pair<float, char16_t>> frequent;
    for (int code = 0x4e00; code <= 0x9fff; ++code)
        if (t.index[code] >= 0 && t.bytesOf[code]) frequent.push_back({t.occurrences[t.index[code]], char16_t(code)});
    std::sort(frequent.begin(), frequent.end(), std::greater<>());
    frequent.resize(std::min<size_t>(frequent.size(), 64));
    for (char16_t ch : u"，。、！？：；“”") frequent.push_back({0, ch});
    for (const auto& [_, ch] : frequent) {
        const auto bytes = t.bytesOf[ch];
        if (bytes) t.unknownContext.push_back({{quint8(bytes >> 8), quint8(bytes)}, 2, ch, false});
    }
    // One placeholder per pair of UTF-8 byte classes that some GBK character
    // occupies, so a rare character never makes the byte pattern unexplainable.
    const std::array<std::pair<int, int>, 13> classes{{{0x80, 0x8f}, {0x90, 0x9f}, {0xa0, 0xbf}, {0xc0, 0xc1}, {0xc2, 0xdf},
        {0xe0, 0xe0}, {0xe1, 0xec}, {0xed, 0xed}, {0xee, 0xef}, {0xf0, 0xf0}, {0xf1, 0xf3}, {0xf4, 0xf4}, {0xf5, 0xff}}};
    for (const auto& [leadFrom, leadTo] : classes)
        for (const auto& [trailFrom, trailTo] : classes) {
            bool found = false;
            for (int lead = std::max(leadFrom, 0x81); lead <= std::min(leadTo, 0xfe) && !found; ++lead)
                for (int trail = trailFrom; trail <= std::min(trailTo, 0xfe) && !found; ++trail)
                    if (const auto ch = t.gbkChar(quint8(lead), quint8(trail))) {
                        t.unknownContext.push_back({{quint8(lead), quint8(trail)}, 2, ch, true});
                        found = true;
                    }
        }
    return t;
}
const Tables& tables() {
    static const Tables t = build();  // Thread-safe one-time initialization.
    return t;
}
// -------------------------------------------------------- character model
constexpr char16_t Boundary = 0, Wild = 1;
double punctuation(char16_t c) {
    switch (c) {
    case u'，': return 0.35; case u'。': return 0.2; case u'、': return 0.04; case u'“': case u'”': return 0.03;
    case u'！': case u'？': case u'：': return 0.02; case u'…': return 0.01; case u'；': case u'—': return 0.005;
    case u'‘': case u'’': case u'（': case u'）': case u'《': case u'》': return 0.003;
    case u'\n': return 0.05; case u' ': return 0.01;
    default: return c < 0x80 ? 0.002 : 0.0;
    }
}
bool isHan(char16_t c) { return (c >= 0x4e00 && c <= 0x9fff) || (c >= 0x3400 && c <= 0x4dbf); }
// Log probability of c after the state prev; writes the next state.
double model(const Tables& t, char16_t prev, char16_t c, char16_t& next) {
    if (const double p = punctuation(c); p > 0) {
        next = Boundary;
        double lp = std::log(p);
        if (prev > Wild && t.model && t.index[prev] >= 0) {
            const auto a = t.index[prev];
            lp += std::log(t.endCount[a] / t.occurrences[a] + 1e-3);
        }
        return lp;
    }
    if (!isHan(c)) { next = Boundary; return std::log(1e-7); }
    next = c;
    if (!t.model) return std::log(1e-4);
    const auto b = t.index[c];
    if (b < 0) return std::log(1e-9);
    if (prev == Wild) return t.logUnigram[b];
    const auto a = prev > Wild ? t.index[prev] : -1;
    if (a < 0) return t.logStart[b];
    const auto* first = t.pairSecond.data() + t.pairOffset[a];
    const auto* last = t.pairSecond.data() + t.pairOffset[a + 1];
    const auto* it = std::lower_bound(first, last, quint16(b));
    const double within = it != last && *it == b ? t.pairCount[it - t.pairSecond.data()] : 0.0;
    const double p = (within + t.endCount[a] * t.startProbability[b]) / t.occurrences[a];
    return std::log(0.95 * p + 0.05 * t.unigramProbability[b]);
}
// ---------------------------------------------------------------- search
// A wider beam or margin did not change accuracy on the prose fixture; the
// character model, not the search, limits quality. These values keep a 400 KB file to a few seconds.
constexpr int Beam = 8;
constexpr double PruneMargin = 12.0;
constexpr double GuideMatch = 9.0, GuideSkip = 3.0, GuideInsert = 1.5;
struct Hyp {
    double score; quint64 key; int parent; Cursor cursor; int guide; char16_t lm;
    Choice step; bool matched;
};
struct Node { int parent; Choice step; bool matched; };
Choice single(quint8 b) { Choice c; c.bytes[0] = b; c.length = 1; c.ch = b; return c; }
Choice pair(quint16 bytes, char16_t ch) { Choice c; c.bytes = {quint8(bytes >> 8), quint8(bytes)}; c.length = 2; c.ch = ch; return c; }
struct SpanResult {
    QString text; QByteArray evidence; int guideRejected = 0; bool success = false; double score = -1e300;
    std::vector<std::pair<int, qsizetype>> pieces;  // (end token, end of text) of each independent span
};
class Search {
public:
    Search(const Tables& t, const std::vector<Token>& tokens, int begin, int end, bool atTextEnd, const QString& guide, bool perByte)
        : t(t), tokens(tokens.data()), begin(begin), end(end), atTextEnd(atTextEnd), perByte(perByte), guide(guide) {}
    SpanResult run(const Cancellation& cancel) {
        constexpr int Ring = 32;
        std::array<std::vector<Hyp>, Ring> buckets;
        std::array<double, Ring> best; best.fill(-1e300);
        const auto progress = [this](const Cursor& c) { return 4 * (c.token - begin) + c.pending.length; };
        buckets[0].push_back({0.0, 0, -1, {begin, {}}, 0, Boundary, {}, false});
        best[0] = 0;
        const int last = 4 * (end - begin) + 3;
        double finalScore = -1e300; int finalNode = -1;
        std::vector<Hyp> kept;
        for (int level = 0; level <= last; ++level) {
            if ((level & 255) == 0 && cancel && cancel->load()) return {};
            auto& bucket = buckets[level % Ring];
            if (bucket.empty()) continue;
            prune(bucket);
            kept.swap(bucket); bucket.clear(); best[level % Ring] = -1e300;
            for (const auto& h : kept) {
                const int node = int(nodes.size());
                nodes.push_back({h.parent, h.step, h.matched});
                const auto& c = h.cursor;
                if (c.token == end && !c.pending.length) {
                    if (h.score > finalScore) { finalScore = h.score; finalNode = node; }
                    continue;
                }
                // At the end of the text a decoder reports pending bytes as replacement characters.
                if (atTextEnd && c.pending.length) {
                    const int replaced = perByte ? c.pending.length : 1;
                    bool flushed = c.token + replaced == end;
                    for (int k = c.token; flushed && k < end; ++k) flushed = tokens[k].length == 0;
                    if (flushed && h.score > finalScore) { finalScore = h.score; finalNode = node; }
                }
                expand(h, node, [&](Hyp&& next) {
                    const int to = progress(next.cursor);
                    if (to <= level || to > last) return;
                    auto& target = buckets[to % Ring];
                    auto& top = best[to % Ring];
                    if (next.score < top - PruneMargin) return;
                    top = std::max(top, next.score);
                    target.push_back(std::move(next));
                });
            }
            kept.clear();
        }
        if (finalNode < 0) return {};
        auto result = assemble(finalNode);
        result.score = finalScore;
        return result;
    }
private:
    const Tables& t;
    const Token* tokens;
    int begin, end;
    bool atTextEnd, perByte;
    const QString& guide;
    std::vector<Node> nodes;
    static void prune(std::vector<Hyp>& bucket) {
        if (bucket.size() <= 1) return;
        // Cheap passes first: drop hopeless entries, then keep a few beams' worth before deduplication.
        double top = -1e300;
        for (const auto& h : bucket) top = std::max(top, h.score);
        std::erase_if(bucket, [top](const Hyp& h) { return h.score < top - PruneMargin; });
        if (bucket.size() > size_t(4 * Beam)) {
            std::nth_element(bucket.begin(), bucket.begin() + 4 * Beam, bucket.end(), [](const Hyp& a, const Hyp& b) { return a.score > b.score; });
            bucket.resize(4 * Beam);
        }
        std::sort(bucket.begin(), bucket.end(), [](const Hyp& a, const Hyp& b) { return a.key != b.key ? a.key < b.key : a.score > b.score; });
        bucket.erase(std::unique(bucket.begin(), bucket.end(), [](const Hyp& a, const Hyp& b) { return a.key == b.key; }), bucket.end());
        if (bucket.size() > size_t(Beam)) {
            std::nth_element(bucket.begin(), bucket.begin() + Beam, bucket.end(), [](const Hyp& a, const Hyp& b) { return a.score > b.score; });
            bucket.resize(Beam);
        }
    }
    template <typename Sink> void tryCandidate(const Hyp& h, int node, const Choice& c, Sink& sink) {
        Cursor cursor = h.cursor;
        for (int k = 0; k < c.length; ++k) if (!feed(cursor, c.bytes[k], tokens, end, perByte)) return;
        if (!viable(cursor, tokens, end, perByte)) return;
        char16_t lm = Boundary;
        double score = h.score;
        if (c.wild) { lm = Wild; score += std::log(2e-5); }
        else score += model(t, h.lm, c.ch, lm);
        const auto push = [&](double s, int g, bool matched) {
            const quint64 key = quint64(cursor.pending.key()) | quint64(lm) << 24 | quint64(quint32(g)) << 40;
            sink(Hyp{s, key, node, cursor, g, lm, c, matched});
        };
        if (guide.isEmpty()) { push(score, 0, false); return; }
        const int g = h.guide, size = int(guide.size());
        if (!c.wild && g < size && guide[g].unicode() == c.ch) { push(score + GuideMatch, g + 1, true); return; }
        if (!c.wild && g + 1 < size && guide[g + 1].unicode() == c.ch) { push(score + GuideMatch - GuideSkip, g + 2, true); return; }
        if (!c.wild && g + 2 < size && guide[g + 2].unicode() == c.ch) { push(score + GuideMatch - 2 * GuideSkip, g + 3, true); return; }
        push(score, std::min(g + 1, size), false);       // substitution
        push(score - GuideInsert, g, false);              // insertion
    }
    template <typename Sink> void expand(const Hyp& h, int node, Sink&& sink) {
        const auto& c = h.cursor;
        if (c.token >= end) return;
        const auto& token = tokens[c.token];
        if (token.length) {
            const quint8 f = token.bytes[c.pending.length];
            if (f < 0x80) tryCandidate(h, node, single(f), sink);
            else for (const auto& candidate : t.byLead[f]) tryCandidate(h, node, candidate, sink);
            return;
        }
        if (c.pending.length && c.token + 1 < end && tokens[c.token + 1].length == 1) {
            const quint8 b = tokens[c.token + 1].bytes[0];
            tryCandidate(h, node, single(b), sink);
        }
        for (const auto& candidate : t.unknownContext) tryCandidate(h, node, candidate, sink);
        if (c.token + 1 < end && tokens[c.token + 1].length) {
            const quint8 f = tokens[c.token + 1].bytes[0];
            for (const auto& candidate : t.byTrail[f]) tryCandidate(h, node, candidate, sink);
            for (const auto& candidate : t.byLead[f]) tryCandidate(h, node, candidate, sink);
        }
        if (c.token + 2 < end && tokens[c.token + 2].length)
            for (const auto& candidate : t.byTrail[tokens[c.token + 2].bytes[0]]) tryCandidate(h, node, candidate, sink);
        for (int k = 0; k < 3 && h.guide + k < guide.size(); ++k) {
            const char16_t ch = guide[h.guide + k].unicode();
            if (const auto bytes = t.bytesOf[ch]) tryCandidate(h, node, pair(bytes, ch), sink);
        }
    }
    SpanResult assemble(int node) {
        std::vector<const Node*> path;
        for (int n = node; n >= 0 && nodes[n].step.length; n = nodes[n].parent) path.push_back(&nodes[n]);
        std::reverse(path.begin(), path.end());
        // Which generated bytes survived: those that form valid UTF-8 sequences.
        std::vector<quint8> bytes;
        for (const auto* n : path) for (int k = 0; k < n->step.length; ++k) bytes.push_back(n->step.bytes[k]);
        std::vector<bool> known(bytes.size(), false);
        for (size_t i = 0; i < bytes.size();) {
            const int need = bytes[i] < 0x80 ? 0 : trailCount(bytes[i]);
            if (need < 0) { ++i; continue; }
            size_t k = 1;
            while (int(k) <= need && i + k < bytes.size() && continues(&bytes[i], int(k), bytes[i + k])) ++k;
            if (int(k) == need + 1) { for (size_t q = 0; q < k; ++q) known[i + q] = true; i += k; }
            else i += std::max<size_t>(1, k);
        }
        SpanResult r; r.success = true;
        size_t at = 0;
        for (const auto* n : path) {
            const auto& s = n->step;
            int survived = 0;
            for (int k = 0; k < s.length; ++k) survived += known[at + k];
            at += s.length;
            CharEvidence e;
            char16_t ch = s.ch;
            if (s.length == 1 || survived == 2) e = CharEvidence::Exact;
            else if (survived == 1) e = CharEvidence::Partial;
            else e = n->matched ? CharEvidence::Guided : CharEvidence::Unknown;
            if (s.wild && e != CharEvidence::Unknown) {
                // A placeholder whose bytes happen to survive is no longer a placeholder.
                ch = s.length == 2 ? t.gbkChar(s.bytes[0], s.bytes[1]) : s.ch;
                if (!ch || e == CharEvidence::Partial) { ch = LossyGbkRecovery::UnknownMark; e = CharEvidence::Unknown; }
            }
            if (e == CharEvidence::Unknown) ch = LossyGbkRecovery::UnknownMark;
            r.text += QChar(ch); r.evidence += char(e);
        }
        if (!guide.isEmpty()) {
            int matched = 0;
            for (const auto* n : path) matched += n->matched;
            r.guideRejected = std::max(0, int(guide.size()) - matched);
        }
        return r;
    }
};
// Tokens of the damaged text plus the QChar offset of each token.
bool tokenize(const QString& text, std::vector<Token>& tokens, std::vector<qsizetype>& offsets) {
    tokens.reserve(text.size()); offsets.reserve(text.size() + 1);
    for (qsizetype i = 0; i < text.size(); ++i) {
        offsets.push_back(i);
        char32_t u = text[i].unicode();
        if (QChar::isHighSurrogate(u)) {
            if (i + 1 >= text.size() || !text[i + 1].isLowSurrogate()) return false;
            u = QChar::surrogateToUcs4(char16_t(u), text[++i].unicode());
        } else if (QChar::isLowSurrogate(u)) return false;
        Token t;
        if (u == 0xfffd) { tokens.push_back(t); continue; }
        if (u < 0x80) { t.length = 1; t.bytes[0] = quint8(u); }
        else if (u < 0x800) { t.length = 2; t.bytes = {quint8(0xc0 | u >> 6), quint8(0x80 | (u & 0x3f))}; }
        else if (u < 0x10000) { t.length = 3; t.bytes = {quint8(0xe0 | u >> 12), quint8(0x80 | (u >> 6 & 0x3f)), quint8(0x80 | (u & 0x3f))}; }
        else { t.length = 4; t.bytes = {quint8(0xf0 | u >> 18), quint8(0x80 | (u >> 12 & 0x3f)), quint8(0x80 | (u >> 6 & 0x3f)), quint8(0x80 | (u & 0x3f))}; }
        tokens.push_back(t);
    }
    offsets.push_back(text.size());
    return true;
}
// ASCII bytes below 0x40 are never GBK trail bytes and end any pending UTF-8
// sequence, so they split the text into spans that can be solved independently.
bool boundary(const Token& t) { return t.length == 1 && t.bytes[0] < 0x40; }
struct Job { int begin, end; bool atTextEnd; };
LossyRecovery combine(const std::vector<qsizetype>& offsets, const std::vector<Job>& jobs, const QList<SpanResult>& results) {
    LossyRecovery r;
    r.success = true;
    for (qsizetype k = 0; k < results.size(); ++k) {
        const auto& s = results[k];
        if (!s.success) { r.success = false; return r; }
        int tokenBegin = jobs[k].begin;
        qsizetype textBegin = 0;
        for (const auto& [tokenEnd, textEnd] : s.pieces) {
            r.spans.append({offsets[tokenBegin], offsets[tokenEnd], r.text.size() + textBegin, r.text.size() + textEnd});
            tokenBegin = tokenEnd; textBegin = textEnd;
        }
        r.text += s.text; r.evidence += s.evidence; r.guideRejected += s.guideRejected;
    }
    r.count();
    return r;
}
}
void LossyRecovery::count() {
    exact = int(evidence.count(char(CharEvidence::Exact)));
    partial = int(evidence.count(char(CharEvidence::Partial)));
    guided = int(evidence.count(char(CharEvidence::Guided)));
    unknown = int(evidence.count(char(CharEvidence::Unknown)));
}
bool LossyGbkRecovery::modelAvailable() { return tables().model; }
bool LossyGbkRecovery::applicable(const QString& damaged) {
    qsizetype replacement = 0, debris = 0, cjk = 0;
    for (auto c : damaged) {
        const auto u = c.unicode();
        if (u == 0xfffd) ++replacement;
        else if (u >= 0x80 && u < 0x800) ++debris;
        else if (u >= 0x3000 && u <= 0x9fff) ++cjk;
    }
    // A stray replacement character inside ordinary Chinese text is a hole, not this pattern.
    return replacement >= 4 && replacement * 8 >= damaged.size() && debris > cjk;
}
LossyRecovery LossyGbkRecovery::recover(const QString& damaged, const Cancellation& cancel, const std::function<void(double)>& progress) {
    std::vector<Token> tokens; std::vector<qsizetype> offsets;
    if (!tokenize(damaged, tokens, offsets)) return {};
    const auto& t = tables();
    // Group independent spans into jobs of a few thousand tokens for the thread pool.
    std::vector<Job> jobs;
    const int n = int(tokens.size());
    for (int start = 0, i = 0; i < n; ++i) {
        // The first job also decides the replacement convention and is solved twice: keep it short.
        const int size = jobs.empty() ? 512 : 2048;
        if ((boundary(tokens[i]) && i + 1 - start >= size) || i == n - 1) { jobs.push_back({start, i + 1, i == n - 1}); start = i + 1; }
    }
    std::atomic<qint64> done{0};
    const auto solve = [&](const Job& job, bool perByte) {
        // Solve each boundary-delimited span separately; the model context restarts there anyway.
        SpanResult combined; combined.success = true; combined.score = 0;
        for (int s = job.begin, i = job.begin; i < job.end; ++i) {
            if (!boundary(tokens[i]) && i != job.end - 1) continue;
            const QString none;
            Search search(t, tokens, s, i + 1, job.atTextEnd && i == job.end - 1, none, perByte);
            auto r = search.run(cancel);
            if (!r.success) { combined.success = false; return combined; }
            combined.text += r.text; combined.evidence += r.evidence; combined.score += r.score;
            combined.pieces.push_back({i + 1, combined.text.size()});
            s = i + 1;
        }
        return combined;
    };
    if (jobs.empty()) return {};
    // Decoders disagree on how many U+FFFD a broken sequence becomes. The first
    // job usually contains both patterns; keep the convention that explains it.
    const auto subpart = solve(jobs.front(), false), perByte = solve(jobs.front(), true);
    if (cancel && cancel->load()) { LossyRecovery r; r.cancelled = true; return r; }
    if (!subpart.success && !perByte.success) return {};
    const bool usePerByte = perByte.success && (!subpart.success || perByte.score > subpart.score);
    QThreadPool pool;
    pool.setMaxThreadCount(std::max(1, QThread::idealThreadCount()));
    const std::vector<Job> rest(jobs.begin() + 1, jobs.end());
    QList<SpanResult> results = QtConcurrent::blockingMapped<QList<SpanResult>>(&pool, rest.begin(), rest.end(), [&](const Job& job) {
        auto r = solve(job, usePerByte);
        const auto total = done.fetch_add(job.end - job.begin) + (job.end - job.begin) + (jobs.front().end - jobs.front().begin);
        if (progress) progress(double(total) / std::max(1, n));
        return r;
    });
    results.prepend(usePerByte ? perByte : subpart);
    if (cancel && cancel->load()) { LossyRecovery r; r.cancelled = true; return r; }
    auto r = combine(offsets, jobs, results);
    r.replacement = usePerByte ? Utf8Replacement::PerByte : Utf8Replacement::MaximalSubpart;
    if (r.success) r.source = damaged;
    return r;
}
LossyRecovery LossyGbkRecovery::guided(const QString& damaged, const QString& guide, Utf8Replacement replacement, const Cancellation& cancel) {
    std::vector<Token> tokens; std::vector<qsizetype> offsets;
    if (!tokenize(damaged, tokens, offsets) || tokens.empty()) return {};
    // Compare like with like: the guide is matched character by character.
    QString flatGuide; flatGuide.reserve(guide.size());
    for (auto c : guide) if (!c.isSurrogate()) flatGuide += c;
    Search search(tables(), tokens, 0, int(tokens.size()), true, flatGuide, replacement == Utf8Replacement::PerByte);
    auto r = search.run(cancel);
    if (cancel && cancel->load()) { LossyRecovery result; result.cancelled = true; return result; }
    r.pieces.push_back({int(tokens.size()), r.text.size()});
    const std::vector<Job> jobs{{0, int(tokens.size()), true}};
    auto result = combine(offsets, jobs, {r});
    result.replacement = replacement;
    if (result.success) result.source = damaged;
    return result;
}
QString LossyGbkRecovery::lossyUtf8(QByteArrayView bytes, Utf8Replacement replacement) {
    QString out;
    const auto* b = reinterpret_cast<const quint8*>(bytes.data());
    const qsizetype n = bytes.size();
    for (qsizetype i = 0; i < n;) {
        if (b[i] < 0x80) { out += QChar(b[i]); ++i; continue; }
        const int need = trailCount(b[i]);
        if (need < 0) { out += QChar(0xfffd); ++i; continue; }
        int k = 1;
        while (k <= need && i + k < n && continues(b + i, k, b[i + k])) ++k;
        if (k == need + 1) out += QString::fromUtf8(reinterpret_cast<const char*>(b + i), k);
        else out += QString(replacement == Utf8Replacement::PerByte ? k : 1, QChar(0xfffd));
        i += k;
    }
    return out;
}bool LossyGbkRecovery::consistent(const QString& candidate, const QString& damaged, Utf8Replacement replacement) {
    QStringEncoder encoder("GBK", QStringConverter::Flag::Stateless);
    const QByteArray bytes = encoder(candidate);
    return !encoder.hasError() && lossyUtf8(bytes, replacement) == damaged;
}
QString LossyGbkRecovery::markup(const LossyRecovery& r, qsizetype textBegin, qsizetype textEnd) {
    QString out;
    for (qsizetype i = textBegin; i < textEnd && i < r.text.size(); ++i) {
        const auto e = CharEvidence(r.evidence.at(i));
        if (e == CharEvidence::Partial) { out += QChar(0x3014); out += r.text[i]; out += QChar(0x3015); }
        else out += r.text[i];
    }
    return out;
}
}
