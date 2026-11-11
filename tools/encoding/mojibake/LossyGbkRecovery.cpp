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
#include <numeric>
#include <ranges>
namespace mojibake {
namespace {
// ---------------------------------------------------------------- UTF-8 rules
// Bytes needed after a lead byte, or -1 if the byte cannot start a sequence.
int trailCount(const quint8 lead) {
    if (lead >= 0xc2 && lead <= 0xdf) return 1;
    if (lead >= 0xe0 && lead <= 0xef) return 2;
    if (lead >= 0xf0 && lead <= 0xf4) return 3;
    return -1;
}
// Whether b may follow the bytes already pending (maximal subpart rule).
bool continues(const quint8* pending, const int length, const quint8 b) {
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
    [[nodiscard]] quint32 key() const { return length ? static_cast<quint32>(bytes[0]) | static_cast<quint32>(bytes[1]) << 8 | static_cast<quint32>(bytes[2]) << 16 : 0; }
};
// Decoder position while generating bytes: next expected token and pending bytes.
struct Cursor { int token = 0; Pending pending; };
// Feeds one byte through a lossy UTF-8 decoder and checks each emitted token
// against the damaged text. Returns false on contradiction.
// perByte: the decoder writes one U+FFFD per byte of a broken sequence (Qt, .NET)
// instead of one per maximal subpart (WHATWG, Python, ICU).
bool feed(Cursor& c, const quint8 b, const Token* tokens, const int end, const bool perByte) {
    auto& p = c.pending;
    if (p.length) {
        if (continues(p.bytes.data(), p.length, b)) {
            p.bytes[p.length++] = b;
            if (p.length == trailCount(p.bytes[0]) + 1) {
                if (c.token >= end) return false;
                if (const auto& t = tokens[c.token]; t.length != p.length || std::memcmp(t.bytes.data(), p.bytes.data(), p.length) != 0) return false;
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
bool viable(const Cursor& c, const Token* tokens, const int end, const bool perByte) {
    if (!c.pending.length) return true;
    if (c.token >= end) return false;
    const auto& [length, bytes] = tokens[c.token];
    if (length == 0) {
        if (!perByte) return true;
        for (int k = 1; k < c.pending.length; ++k) if (c.token + k >= end || tokens[c.token + k].length != 0) return false;
        return true;
    }
    return length > c.pending.length && std::memcmp(bytes.data(), c.pending.bytes.data(), c.pending.length) == 0;
}
// ------------------------------------------------------------- static tables
struct Choice { std::array<quint8, 2> bytes{}; quint8 length = 0; char16_t ch = 0; bool wild = false; };
struct Tables {
    std::array<std::array<char16_t, 192>, 128> gbk{};  // [lead - 0x80][trail - 0x40]
    std::vector<quint16> bytesOf = std::vector<quint16>(65536, 0);  // char -> lead << 8 | trail
    std::array<std::vector<Choice>, 256> byLead, byTrail;
    std::vector<Choice> unknownContext;  // frequent characters plus one placeholder per byte-class pair
    // Word model: a prefix tree of jieba words. A search state is a tree node,
    // the part of a word read so far; the root is a word boundary.
    std::vector<char16_t> nodeChar;
    std::vector<quint32> firstChild;   // children of n: [firstChild[n], firstChild[n] + childCount[n]), sorted by character
    std::vector<quint16> childCount;
    std::vector<quint32> byMass;       // the same ranges, holding child nodes from the most to the least frequent
    std::vector<float> logMass;        // log of the frequency of all words below a node
    std::vector<float> logEnd;         // log probability that the word ends at a node
    std::vector<quint8> depth;
    std::vector<quint32> rootChild = std::vector<quint32>(65536, 0);
    double oovStart = 0;               // a character that starts no dictionary word
    bool model = false;
    [[nodiscard]] char16_t gbkChar(const quint8 lead, const quint8 trail) const {
        return lead >= 0x81 && lead <= 0xfe && trail >= 0x40 && trail != 0x7f && trail <= 0xfe ? gbk[lead - 0x80][trail - 0x40] : 0;
    }
    [[nodiscard]] quint32 child(const quint32 node, const char16_t c) const {
        if (!node) return rootChild[c];
        const auto first = nodeChar.begin() + firstChild[node], last = first + childCount[node];
        const auto it = std::lower_bound(first, last, c);
        return it != last && *it == c ? static_cast<quint32>(it - nodeChar.begin()) : 0;
    }
};
// Share of a character's use as a word of its own beyond what the dictionary
// lists, so that a character which only appears inside longer words can still
// stand alone. A word may also end where the dictionary has no word, at a
// cost: names and new terms are missing from any dictionary.
constexpr double SingleCharacter = 0.05, UnlistedEnd = 1e-5;
bool loadModel(Tables& t) {
    QFile file(":/charlm/zh_wordlm.bin");
    if (!file.open(QIODevice::ReadOnly)) return false;
    const QByteArray data = file.readAll();
    const auto* p = reinterpret_cast<const uchar*>(data.constData());
    if (data.size() < 12 || !data.startsWith("LTWT") || qFromLittleEndian<quint32>(p + 4) != 1) return false;
    const auto n = qFromLittleEndian<quint32>(p + 8);
    if (n < 2 || n >= 1u << 20 || data.size() != 12 + static_cast<qsizetype>(n) * 8) return false;
    t.nodeChar.resize(n); t.firstChild.resize(n); t.childCount.resize(n); t.depth.assign(n, 0);
    std::vector<double> frequency(n), mass(n);
    quint64 next = 1;
    for (quint32 k = 0; k < n; ++k) {
        const auto* r = p + 12 + static_cast<qsizetype>(k) * 8;
        t.nodeChar[k] = qFromLittleEndian<quint16>(r);
        frequency[k] = qFromLittleEndian<quint32>(r + 2);
        t.childCount[k] = qFromLittleEndian<quint16>(r + 6);
        t.firstChild[k] = static_cast<quint32>(next);
        next += t.childCount[k];
        if (next > n) return false;
        for (quint32 c = 0; c < t.childCount[k]; ++c) {
            const quint32 child = t.firstChild[k] + c;
            t.depth[child] = static_cast<quint8>(std::min(255, t.depth[k] + 1));
        }
    }
    if (next != n) return false;
    for (quint32 k = n; k-- > 0;) {
        mass[k] += frequency[k];
        for (quint32 c = 0; c < t.childCount[k]; ++c) mass[k] += mass[t.firstChild[k] + c];
    }
    // Children of one node are written sorted by character; anything else is a corrupt file.
    for (quint32 k = 0; k < n; ++k)
        for (quint32 c = 1; c < t.childCount[k]; ++c)
            if (t.nodeChar[t.firstChild[k] + c - 1] >= t.nodeChar[t.firstChild[k] + c]) return false;
    t.logMass.resize(n); t.logEnd.resize(n);
    double rootMass = 0;
    for (quint32 k = 1; k < n; ++k) {
        if (t.depth[k] == 1) { frequency[k] += SingleCharacter * mass[k]; mass[k] *= 1 + SingleCharacter; rootMass += mass[k]; }
        if (mass[k] <= 0) return false;
        t.logMass[k] = static_cast<float>(std::log(mass[k]));
        t.logEnd[k] = static_cast<float>(std::log(std::max(frequency[k] / mass[k], UnlistedEnd)));
    }
    t.logMass[0] = static_cast<float>(std::log(rootMass));
    t.oovStart = std::log(0.5) - t.logMass[0];
    t.byMass.resize(n);
    for (quint32 k = 0; k < n; ++k) {
        const auto first = t.byMass.begin() + t.firstChild[k], last = first + t.childCount[k];
        std::iota(first, last, t.firstChild[k]);
        std::sort(first, last, [&](const quint32 a, const quint32 b) { return mass[a] > mass[b]; });
    }
    for (quint32 c = 0; c < t.childCount[0]; ++c) t.rootChild[t.nodeChar[t.firstChild[0] + c]] = t.firstChild[0] + c;
    return true;
}
Tables build() {
    Tables t;
    QStringDecoder decoder("GBK", QStringConverter::Flag::Stateless);
    for (int lead = 0x81; lead <= 0xfe; ++lead)
        for (int trail = 0x40; trail <= 0xfe; ++trail) {
            if (trail == 0x7f) continue;
            const char pair[2] = {static_cast<char>(lead), static_cast<char>(trail)};
            const QString text = decoder.decode(QByteArrayView(pair, 2));
            if (decoder.hasError()) { decoder.resetState(); continue; }
            if (text.size() != 1 || text[0] == QChar(0xfffd) || text[0].unicode() < 0x80) continue;
            const char16_t ch = text[0].unicode();
            // Private-use (user-defined) rows are not text a person typed.
            if (ch >= 0xe000 && ch <= 0xf8ff) continue;
            t.gbk[lead - 0x80][trail - 0x40] = ch;
            if (!t.bytesOf[ch]) t.bytesOf[ch] = static_cast<quint16>(lead << 8 | trail);
            const Choice c{.bytes = {static_cast<quint8>(lead), static_cast<quint8>(trail)}, .length = 2, .ch = ch, .wild = false};
            t.byLead[lead].push_back(c); t.byTrail[trail].push_back(c);
        }
    t.model = loadModel(t);
    // Characters tried at a word boundary where no byte of the character
    // survived. Alone they are displayed as unknown and only shape the context
    // of their neighbours, so the most frequent word starts are enough; inside
    // a word the search also tries the likeliest continuations (expand).
    std::vector<char16_t> frequent;
    if (t.model)
        for (quint32 c = 0; c < t.childCount[0] && frequent.size() < 64; ++c)
            if (const char16_t ch = t.nodeChar[t.byMass[t.firstChild[0] + c]]; t.bytesOf[ch]) frequent.push_back(ch);
    for (char16_t ch : u"，。、！？：；“”") frequent.push_back(ch);
    for (const auto ch : frequent)
        if (const auto bytes = t.bytesOf[ch])
            t.unknownContext.push_back({.bytes = {static_cast<quint8>(bytes >> 8), static_cast<quint8>(bytes)}, .length = 2, .ch = ch, .wild = false});
    // One placeholder per pair of UTF-8 byte classes that some GBK character
    // occupies, so a rare character never makes the byte pattern unexplainable.
    constexpr std::array<std::pair<int, int>, 13> classes{{{0x80, 0x8f}, {0x90, 0x9f}, {0xa0, 0xbf}, {0xc0, 0xc1}, {0xc2, 0xdf},
        {0xe0, 0xe0}, {0xe1, 0xec}, {0xed, 0xed}, {0xee, 0xef}, {0xf0, 0xf0}, {0xf1, 0xf3}, {0xf4, 0xf4}, {0xf5, 0xff}}};
    for (const auto& [leadFrom, leadTo] : classes)
        for (const auto& [trailFrom, trailTo] : classes) {
            bool found = false;
            for (int lead = std::max(leadFrom, 0x81); lead <= std::min(leadTo, 0xfe) && !found; ++lead)
                for (int trail = trailFrom; trail <= std::min(trailTo, 0xfe) && !found; ++trail)
                    if (const auto ch = t.gbkChar(static_cast<quint8>(lead), static_cast<quint8>(trail))) {
                        t.unknownContext.push_back({.bytes = {static_cast<quint8>(lead), static_cast<quint8>(trail)}, .length = 2, .ch = ch, .wild = true});
                        found = true;
                    }
        }
    return t;
}
const Tables& tables() {
    static const Tables t = build();  // Thread-safe one-time initialization.
    return t;
}
// ------------------------------------------------------------- word model
constexpr quint32 Root = 0;  // word boundary
double punctuation(const char16_t c) {
    switch (c) {
    case u'，': return 0.35; case u'。': return 0.2; case u'、': return 0.04; case u'“': case u'”': return 0.03;
    case u'！': case u'？': case u'：': return 0.02; case u'…': return 0.01; case u'；': case u'—': return 0.005;
    case u'‘': case u'’': case u'（': case u'）': case u'《': case u'》': return 0.003;
    case u'\n': return 0.05; case u' ': return 0.01;
    default: return c < 0x80 ? 0.002 : 0.0;
    }
}
bool isHan(const char16_t c) { return (c >= 0x4e00 && c <= 0x9fff) || (c >= 0x3400 && c <= 0x4dbf); }
double endWord(const Tables& t, const quint32 state) { return state == Root ? 0.0 : t.logEnd[state]; }
// Word unigram model over the prefix tree: calls next(log probability, next
// state) for each way c can follow state, continuing the current word or
// ending it and starting a new one.
template <typename Next> void transitions(const Tables& t, const quint32 state, const char16_t c, Next&& next) {
    const double end = endWord(t, state);
    if (const double p = punctuation(c); p > 0) { next(end + std::log(p), Root); return; }
    if (!isHan(c)) { next(end + std::log(1e-7), Root); return; }
    if (!t.model) { next(std::log(1e-4), Root); return; }
    if (state != Root)
        if (const auto m = t.child(state, c)) next(static_cast<double>(t.logMass[m]) - t.logMass[state], m);
    if (const auto r = t.rootChild[c]) next(end + t.logMass[r] - t.logMass[Root], r);
    else next(end + t.oovStart, Root);
}
// ---------------------------------------------------------------- search
// A wider beam or margin did not change accuracy on the prose fixture; the
// model, not the search, limits quality. These values keep a 400 KB file to a few seconds.
constexpr int Beam = 8;
constexpr double PruneMargin = 12.0;
constexpr double GuideMatch = 9.0, GuideSkip = 3.0, GuideInsert = 1.5;
// Likeliest continuations of the current word tried where no byte survived.
constexpr int Continuations = 16;
struct Hyp {
    double score = 0; quint64 key = 0; int parent = -1; Cursor cursor; int guide = 0; quint32 lm = 0;
    Choice step; bool matched = false;
};
struct Node { int parent; Choice step; bool matched; };
Choice single(const quint8 b) { Choice c; c.bytes[0] = b; c.length = 1; c.ch = b; return c; }
Choice pair(const quint16 bytes, const char16_t ch) { Choice c; c.bytes = {static_cast<quint8>(bytes >> 8), static_cast<quint8>(bytes)}; c.length = 2; c.ch = ch; return c; }
struct SpanResult {
    QString text; QByteArray evidence, bytes; int guideRejected = 0; bool success = false; double score = -1e300;
    std::vector<std::pair<int, qsizetype>> pieces;  // (end token, end of text) of each independent span
};
class Search {
public:
    Search(const Tables& t, const std::vector<Token>& tokens, const int begin, const int end, const bool atTextEnd, const QString& guide, const bool perByte)
        : t(t), tokens(tokens.data()), begin(begin), end(end), atTextEnd(atTextEnd), perByte(perByte), guide(guide) {}
    SpanResult run(const Cancellation& cancel) {
        constexpr int Ring = 32;
        std::array<std::vector<Hyp>, Ring> buckets;
        std::array<double, Ring> best{}; best.fill(-1e300);
        const auto progress = [this](const Cursor& c) { return 4 * (c.token - begin) + c.pending.length; };
        buckets[0].push_back({.score = 0.0, .key = 0, .parent = -1, .cursor = {.token = begin, .pending = {}}, .guide = 0, .lm = Root, .step = {}, .matched = false});
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
                const int node = static_cast<int>(nodes.size());
                nodes.push_back({.parent = h.parent, .step = h.step, .matched = h.matched});
                const auto& [token, pending] = h.cursor;
                const double complete = h.score + endWord(t, h.lm);  // the last word ends with the span
                if (token == end && !pending.length) {
                    if (complete > finalScore) { finalScore = complete; finalNode = node; }
                    continue;
                }
                // At the end of the text a decoder reports pending bytes as replacement characters.
                if (atTextEnd && pending.length) {
                    const int replaced = perByte ? pending.length : 1;
                    bool flushed = token + replaced == end;
                    for (int k = token; flushed && k < end; ++k) flushed = tokens[k].length == 0;
                    if (flushed && complete > finalScore) { finalScore = complete; finalNode = node; }
                }
                expand(h, node, [&](Hyp&& next) {
                    const int to = progress(next.cursor);
                    if (to <= level || to > last) return;
                    auto& target = buckets[to % Ring];
                    auto& top = best[to % Ring];
                    if (next.score < top - PruneMargin) return;
                    top = std::max(top, next.score);
                    target.push_back(next);
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
        constexpr auto better = [](const Hyp& a, const Hyp& b) { return a.score > b.score; };
        if (constexpr size_t Wide = 4 * static_cast<size_t>(Beam); bucket.size() > Wide) {
            std::ranges::nth_element(bucket, bucket.begin() + Wide, better);
            bucket.resize(Wide);
        }
        std::ranges::sort(bucket, [](const Hyp& a, const Hyp& b) { return a.key != b.key ? a.key < b.key : a.score > b.score; });
        bucket.erase(std::ranges::unique(bucket, [](const Hyp& a, const Hyp& b) { return a.key == b.key; }).begin(), bucket.end());
        if (bucket.size() > static_cast<size_t>(Beam)) {
            std::ranges::nth_element(bucket, bucket.begin() + Beam, better);
            bucket.resize(Beam);
        }
    }
    template <typename Sink> void tryCandidate(const Hyp& h, const int node, const Choice& c, Sink& sink) {
        Cursor cursor = h.cursor;
        for (int k = 0; k < c.length; ++k) if (!feed(cursor, c.bytes[k], tokens, end, perByte)) return;
        if (!viable(cursor, tokens, end, perByte)) return;
        const auto withGuide = [&](const double score, const quint32 lm) {
            const auto push = [&](const double s, const int g, const bool matched) {
                // pending: 24 bits, model state: 20 bits (loadModel), guide position: 20 bits
                const quint64 key = static_cast<quint64>(cursor.pending.key()) | static_cast<quint64>(lm) << 24 | static_cast<quint64>(static_cast<quint32>(g) & 0xfffff) << 44;
                sink(Hyp{.score = s, .key = key, .parent = node, .cursor = cursor, .guide = g, .lm = lm, .step = c, .matched = matched});
            };
            if (guide.isEmpty()) { push(score, 0, false); return; }
            const int g = h.guide, size = static_cast<int>(guide.size());
            if (!c.wild && g < size && guide[g].unicode() == c.ch) { push(score + GuideMatch, g + 1, true); return; }
            if (!c.wild && g + 1 < size && guide[g + 1].unicode() == c.ch) { push(score + GuideMatch - GuideSkip, g + 2, true); return; }
            if (!c.wild && g + 2 < size && guide[g + 2].unicode() == c.ch) { push(score + GuideMatch - 2 * GuideSkip, g + 3, true); return; }
            push(score, std::min(g + 1, size), false);       // substitution
            push(score - GuideInsert, g, false);              // insertion
        };
        // A placeholder stands for some rare character: it ends the current word and is a word of its own.
        if (c.wild) withGuide(h.score + endWord(t, h.lm) + std::log(2e-5), Root);
        else transitions(t, h.lm, c.ch, [&](const double lp, const quint32 next) { withGuide(h.score + lp, next); });
    }
    template <typename Sink> void expand(const Hyp& h, int node, Sink&& sink) {
        const auto& [at, pending] = h.cursor;
        if (at >= end) return;
        if (const auto& token = tokens[at]; token.length) {
            if (const quint8 f = token.bytes[pending.length]; f < 0x80) tryCandidate(h, node, single(f), sink);
            else for (const auto& candidate : t.byLead[f]) tryCandidate(h, node, candidate, sink);
            return;
        }
        if (pending.length && at + 1 < end && tokens[at + 1].length == 1) {
            const quint8 b = tokens[at + 1].bytes[0];
            tryCandidate(h, node, single(b), sink);
        }
        for (const auto& candidate : t.unknownContext) tryCandidate(h, node, candidate, sink);
        if (h.lm != Root) {
            const auto first = t.byMass.begin() + t.firstChild[h.lm];
            for (auto it = first, last = first + std::min<int>(Continuations, t.childCount[h.lm]); it != last; ++it) {
                const char16_t ch = t.nodeChar[*it];
                if (const auto bytes = t.bytesOf[ch]) tryCandidate(h, node, pair(bytes, ch), sink);
            }
        }
        if (at + 1 < end && tokens[at + 1].length) {
            const quint8 f = tokens[at + 1].bytes[0];
            for (const auto& candidate : t.byTrail[f]) tryCandidate(h, node, candidate, sink);
            for (const auto& candidate : t.byLead[f]) tryCandidate(h, node, candidate, sink);
        }
        if (at + 2 < end && tokens[at + 2].length)
            for (const auto& candidate : t.byTrail[tokens[at + 2].bytes[0]]) tryCandidate(h, node, candidate, sink);
        for (int k = 0; k < 3 && h.guide + k < guide.size(); ++k) {
            const char16_t ch = guide[h.guide + k].unicode();
            if (const auto bytes = t.bytesOf[ch]) tryCandidate(h, node, pair(bytes, ch), sink);
        }
    }
    [[nodiscard]] SpanResult assemble(const int node) const {
        std::vector<const Node*> path;
        for (int n = node; n >= 0 && nodes[n].step.length; n = nodes[n].parent) path.push_back(&nodes[n]);
        std::ranges::reverse(path);
        // Which generated bytes survived: those that form valid UTF-8 sequences.
        std::vector<quint8> bytes;
        for (const auto* n : path) for (int k = 0; k < n->step.length; ++k) bytes.push_back(n->step.bytes[k]);
        std::vector known(bytes.size(), false);
        for (size_t i = 0; i < bytes.size();) {
            const int need = bytes[i] < 0x80 ? 0 : trailCount(bytes[i]);
            if (need < 0) { ++i; continue; }
            size_t k = 1;
            while (static_cast<int>(k) <= need && i + k < bytes.size() && continues(&bytes[i], static_cast<int>(k), bytes[i + k])) ++k;
            if (static_cast<int>(k) == need + 1) { for (size_t q = 0; q < k; ++q) known[i + q] = true; i += k; }
            else i += std::max<size_t>(1, k);
        }
        SpanResult r; r.success = true;
        r.bytes = QByteArray(reinterpret_cast<const char*>(bytes.data()), static_cast<qsizetype>(bytes.size()));
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
            r.text += QChar(ch); r.evidence += static_cast<char>(e);
        }
        if (!guide.isEmpty()) {
            int matched = 0;
            for (const auto* n : path) matched += n->matched;
            r.guideRejected = std::max(0, static_cast<int>(guide.size() - guide.count(QChar(LossyGbkRecovery::GuideGap))) - matched);
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
            u = QChar::surrogateToUcs4(static_cast<char16_t>(u), text[++i].unicode());
        } else if (QChar::isLowSurrogate(u)) return false;
        Token t;
        if (u == 0xfffd) { tokens.push_back(t); continue; }
        if (u < 0x80) { t.length = 1; t.bytes[0] = static_cast<quint8>(u); }
        else if (u < 0x800) { t.length = 2; t.bytes = {static_cast<quint8>(0xc0 | u >> 6), static_cast<quint8>(0x80 | (u & 0x3f))}; }
        else if (u < 0x10000) { t.length = 3; t.bytes = {static_cast<quint8>(0xe0 | u >> 12), static_cast<quint8>(0x80 | (u >> 6 & 0x3f)), static_cast<quint8>(0x80 | (u & 0x3f))}; }
        else { t.length = 4; t.bytes = {static_cast<quint8>(0xf0 | u >> 18), static_cast<quint8>(0x80 | (u >> 12 & 0x3f)), static_cast<quint8>(0x80 | (u >> 6 & 0x3f)), static_cast<quint8>(0x80 | (u & 0x3f))}; }
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
            r.spans.append({.damagedBegin = offsets[tokenBegin], .damagedEnd = offsets[tokenEnd], .textBegin = r.text.size() + textBegin, .textEnd = r.text.size() + textEnd});
            tokenBegin = tokenEnd; textBegin = textEnd;
        }
        r.text += s.text; r.evidence += s.evidence; r.bytes += s.bytes; r.guideRejected += s.guideRejected;
    }
    r.count();
    return r;
}
}
void LossyRecovery::count() {
    exact = static_cast<int>(evidence.count(static_cast<char>(CharEvidence::Exact)));
    partial = static_cast<int>(evidence.count(static_cast<char>(CharEvidence::Partial)));
    guided = static_cast<int>(evidence.count(static_cast<char>(CharEvidence::Guided)));
    unknown = static_cast<int>(evidence.count(static_cast<char>(CharEvidence::Unknown)));
}
bool LossyGbkRecovery::modelAvailable() { return tables().model; }
bool LossyGbkRecovery::applicable(const QString& damaged) {
    qsizetype replacement = 0, debris = 0, cjk = 0;
    for (auto c : damaged) {
        if (const auto u = c.unicode(); u == 0xfffd) ++replacement;
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
    const int n = static_cast<int>(tokens.size());
    for (int start = 0, i = 0; i < n; ++i) {
        // The first job also decides the replacement convention and is solved twice: keep it short.
        if (const int size = jobs.empty() ? 512 : 2048; (boundary(tokens[i]) && i + 1 - start >= size) || i == n - 1) { jobs.push_back({.begin = start, .end = i + 1, .atTextEnd = i == n - 1}); start = i + 1; }
    }
    std::atomic<qint64> done{0};
    const auto solve = [&](const Job& job, const bool perByte) {
        // Solve each boundary-delimited span separately; the model context restarts there anyway.
        SpanResult combined; combined.success = true; combined.score = 0;
        for (int s = job.begin, i = job.begin; i < job.end; ++i) {
            if (!boundary(tokens[i]) && i != job.end - 1) continue;
            const QString none;
            Search search(t, tokens, s, i + 1, job.atTextEnd && i == job.end - 1, none, perByte);
            const auto r = search.run(cancel);
            if (!r.success) { combined.success = false; return combined; }
            combined.text += r.text; combined.evidence += r.evidence; combined.bytes += r.bytes; combined.score += r.score;
            combined.pieces.emplace_back(i + 1, combined.text.size());
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
    // Per-byte damage can often be read just as well under the other convention, which then ties;
    // damage by maximal subparts usually cannot be read per byte at all. A tie therefore means per byte.
    const bool usePerByte = perByte.success && (!subpart.success || perByte.score >= subpart.score);
    QThreadPool pool;
    pool.setMaxThreadCount(std::max(1, QThread::idealThreadCount()));
    const std::vector rest(jobs.begin() + 1, jobs.end());
    auto results = QtConcurrent::blockingMapped<QList<SpanResult>>(&pool, rest.begin(), rest.end(), [&](const Job& job) {
        auto r = solve(job, usePerByte);
        const auto total = done.fetch_add(job.end - job.begin) + (job.end - job.begin) + (jobs.front().end - jobs.front().begin);
        if (progress) progress(static_cast<double>(total) / std::max(1, n));
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
    // Compare like with like: the guide is matched character by character. A
    // marker group the AI copied back counts as its first option, and an
    // unknown mark keeps its position without matching any character.
    QString flatGuide; flatGuide.reserve(guide.size());
    bool inGroup = false, optionTaken = false;
    for (auto c : guide) {
        if (c.isSurrogate()) continue;
        if (c == QChar(0x3014)) { inGroup = true; optionTaken = false; continue; }
        if (c == QChar(0x3015)) { inGroup = false; continue; }
        if (inGroup) { if (c == u'|') optionTaken = true; else if (!optionTaken) flatGuide += c; continue; }
        flatGuide += c == QChar(UnknownMark) ? QChar(GuideGap) : c;
    }
    Search search(tables(), tokens, 0, static_cast<int>(tokens.size()), true, flatGuide, replacement == Utf8Replacement::PerByte);
    auto r = search.run(cancel);
    if (cancel && cancel->load()) { LossyRecovery result; result.cancelled = true; return result; }
    r.pieces.emplace_back(static_cast<int>(tokens.size()), r.text.size());
    const std::vector<Job> jobs{{.begin = 0, .end = static_cast<int>(tokens.size()), .atTextEnd = true}};
    auto result = combine(offsets, jobs, {r});
    result.replacement = replacement;
    if (result.success) result.source = damaged;
    return result;
}
QString LossyGbkRecovery::lossyUtf8(const QByteArrayView bytes, const Utf8Replacement replacement) {
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
}
bool LossyGbkRecovery::consistent(const QString& candidate, const QString& damaged, const Utf8Replacement replacement) {
    QStringEncoder encoder("GBK", QStringConverter::Flag::Stateless);
    const QByteArray bytes = encoder(candidate);
    return !encoder.hasError() && lossyUtf8(bytes, replacement) == damaged;
}
namespace {
// Word model score of a short text; an unknown mark ends the word before it.
double windowScore(const Tables& t, const QStringView s) {
    std::vector<std::pair<quint32, double>> live{{Root, 0.0}}, next;
    for (const QChar q : s) {
        next.clear();
        const auto keep = [&](double score, quint32 state) {
            for (auto& [st, sc] : next) if (st == state) { sc = std::max(sc, score); return; }
            next.emplace_back(state, score);
        };
        for (const auto& [state, score] : live) {
            if (q.unicode() == LossyGbkRecovery::UnknownMark) keep(score + endWord(t, state), Root);
            else transitions(t, state, q.unicode(), [&](const double lp, const quint32 n) { keep(score + lp, n); });
        }
        live.swap(next);
    }
    double best = -1e300;
    for (const auto& [state, score] : live) best = std::max(best, score + endWord(t, state));
    return best;
}
qsizetype byteOffset(const QString& text, const qsizetype index) {
    qsizetype at = 0;
    for (qsizetype i = 0; i < index; ++i) at += text[i].unicode() < 0x80 ? 1 : 2;
    return at;
}
QString alternativesAt(const LossyRecovery& r, const qsizetype index, const qsizetype at, const int limit) {
    if (limit <= 0 || index >= r.text.size() || static_cast<CharEvidence>(r.evidence.at(index)) != CharEvidence::Partial || at + 2 > r.bytes.size()) return {};
    const auto& t = tables();
    // Bytes around the character: UTF-8 resynchronises within three bytes.
    constexpr qsizetype Margin = 6;
    const qsizetype from = std::max<qsizetype>(0, at - Margin), to = std::min(r.bytes.size(), at + 2 + Margin);
    QByteArray window = r.bytes.mid(from, to - from);
    const QString damaged = LossyGbkRecovery::lossyUtf8(window, r.replacement);
    const qsizetype contextFrom = std::max<qsizetype>(0, index - 4), contextTo = std::min(r.text.size(), index + 5);
    QString context = r.text.mid(contextFrom, contextTo - contextFrom);
    const auto lead = static_cast<quint8>(r.bytes[at]), trail = static_cast<quint8>(r.bytes[at + 1]);
    std::vector<std::pair<double, char16_t>> ranked;
    // The surviving byte is the lead or the trail; try every character sharing either and let the damage decide.
    for (const auto* list : {&t.byLead[lead], &t.byTrail[trail]})
        for (const auto& c : *list) {
            window[at - from] = static_cast<char>(c.bytes[0]); window[at - from + 1] = static_cast<char>(c.bytes[1]);
            if (LossyGbkRecovery::lossyUtf8(window, r.replacement) != damaged) continue;
            // The unknown mark is a GBK character too, but as an option it would read as a lost character.
            if (c.ch == LossyGbkRecovery::UnknownMark || std::ranges::any_of(ranked, [&](const auto& p) { return p.second == c.ch; })) continue;
            context[index - contextFrom] = QChar(c.ch);
            ranked.emplace_back(windowScore(t, context), c.ch);
        }
    std::ranges::sort(ranked, std::greater());
    // The search chose its character with the whole span as context; it leads.
    QString out = r.text[index];
    for (const auto ch : ranked | std::views::values) { if (out.size() >= limit) break; if (ch != r.text[index].unicode()) out += QChar(ch); }
    return out;
}
}
QString LossyGbkRecovery::alternatives(const LossyRecovery& r, const qsizetype index, const int limit) {
    return index < 0 || index >= r.text.size() ? QString() : alternativesAt(r, index, byteOffset(r.text, index), limit);
}
QString LossyGbkRecovery::markup(const LossyRecovery& r, const qsizetype textBegin, const qsizetype textEnd, const int options) {
    QString out;
    qsizetype at = options > 0 ? byteOffset(r.text, textBegin) : 0;
    for (qsizetype i = textBegin; i < textEnd && i < r.text.size(); ++i) {
        if (const auto e = static_cast<CharEvidence>(r.evidence.at(i)); e == CharEvidence::Partial) {
            QString choices = options > 0 ? alternativesAt(r, i, at, options) : QString();
            if (choices.isEmpty()) choices = r.text[i];
            out += QChar(0x3014);
            for (qsizetype k = 0; k < choices.size(); ++k) { if (k) out += u'|'; out += choices[k]; }
            out += QChar(0x3015);
        } else out += r.text[i];
        at += r.text[i].unicode() < 0x80 ? 1 : 2;
    }
    return out;
}
QStringList LossyGbkRecovery::glossary(const LossyRecovery& r, const int maxCharacters) {
    // Units are one Han character or one ASCII word or number. Terms are runs of
    // up to MaxUnits units in which every byte survived.
    constexpr int MaxUnits = 6, Shortlist = 200;
    QHash<QString, int> counts;
    QStringList units;
    bool asciiOpen = false;
    const auto flush = [&] {
        for (qsizetype i = 0; i < units.size(); ++i) {
            QString term;
            for (qsizetype n = 0; n < MaxUnits && i + n < units.size(); ++n) {
                term += units[i + n];
                if (term.size() >= 2) ++counts[term];  // a lone Han character or letter says nothing
            }
        }
        units.clear(); asciiOpen = false;
    };
    for (qsizetype i = 0; i < r.text.size() && i < r.evidence.size(); ++i) {
        const QChar c = r.text[i];
        const bool exact = static_cast<CharEvidence>(r.evidence.at(i)) == CharEvidence::Exact;
        if (const bool ascii = c.unicode() < 0x80 && c.isLetterOrNumber(); exact && ascii) {
            if (asciiOpen) units.last() += c; else units.append(QString(c));
            asciiOpen = true;
        } else if (exact && c.script() == QChar::Script_Han) { units.append(QString(c)); asciiOpen = false; }
        else flush();
    }
    flush();
    struct Term { QString text; int count; };
    std::vector<Term> terms;
    // Two Han characters recur by chance across word boundaries (的财, 值实): ask for a third sighting.
    const auto enough = [](const QString& term, const int count) {
        const bool pair = term.size() == 2 && std::ranges::any_of(term, [](const QChar c) { return c.unicode() >= 0x80; });
        return count >= (pair ? 3 : 2);
    };
    for (auto it = counts.cbegin(); it != counts.cend(); ++it) if (enough(it.key(), it.value())) terms.push_back({it.key(), it.value()});
    const auto weight = [](const Term& t) { return static_cast<qsizetype>(t.count) * t.text.size(); };
    const auto byWeight = [&](const Term& a, const Term& b) { return weight(a) != weight(b) ? weight(a) > weight(b) : a.text < b.text; };
    std::ranges::sort(terms, byWeight);
    if (terms.size() > Shortlist) terms.resize(Shortlist);
    // A term that only ever occurs inside a longer one adds nothing.
    std::ranges::stable_sort(terms, [](const Term& a, const Term& b) { return a.text.size() > b.text.size(); });
    std::vector<Term> kept;
    for (const auto& t : terms)
        if (std::ranges::none_of(kept, [&](const Term& k) { return k.count >= t.count && k.text.contains(t.text); })) kept.push_back(t);
    std::ranges::sort(kept, byWeight);
    QStringList out;
    qsizetype used = 0;
    for (const auto& t : kept) {
        if (used + t.text.size() > maxCharacters) continue;
        out.append(t.text); used += t.text.size() + 1;
    }
    return out;
}
}
