#include "ByteEncodingAnalyzer.h"
#include "CandidateScorer.h"
#include "MojibakeAnalyzer.h"
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <algorithm>
namespace mojibake {
namespace {
bool cancelled(const Cancellation& cancel) { return cancel && cancel->load(); }
bool wide(const Encoding e) { return e == Encoding::Utf16LE || e == Encoding::Utf16BE || e == Encoding::Utf32LE || e == Encoding::Utf32BE; }
bool wideEvidence(const QByteArray& bytes, const Encoding e) {
    if (!wide(e) || bytes.size() < 4) return false;
    const bool four = e == Encoding::Utf32LE || e == Encoding::Utf32BE;
    const bool little = e == Encoding::Utf16LE || e == Encoding::Utf32LE;
    int zeros = 0, total = 0;
    for (qsizetype i = 0; i < std::min<qsizetype>(bytes.size(), 65536); ++i) {
        if (const auto position = i % (four ? 4 : 2); four ? (little ? position >= 2 : position < 2) : position == (little ? 1 : 0)) { ++total; if (bytes[i] == 0) ++zeros; }
    }
    return total && static_cast<double>(zeros) / total >= 0.65;
}
bool textLike(const QString& text) {
    if (text.contains(QChar(0))) return false;
    int controls = 0;
    for (auto c : text) if ((c.unicode() < 32 && c != '\r' && c != '\n' && c != '\t') || (c.unicode() >= 0x7f && c.unicode() < 0xa0)) ++controls;
    return controls <= std::max<qsizetype>(0, text.size() / 100);
}
bool binarySignature(const QByteArray& bytes) {
    for (const auto& magic : {QByteArray::fromHex("89504e470d0a1a0a"), QByteArray::fromHex("ffd8ff"),
                            QByteArray("PK\x03\x04", 4), QByteArray::fromHex("1f8b"), QByteArray("\x7f" "ELF", 4), QByteArray("%PDF-")})
        if (bytes.startsWith(magic)) return true;
    return false;
}
}
QByteArray ByteEncodingAnalyzer::bom(const Encoding e) {
    switch (e) {
    case Encoding::Utf8: return QByteArray::fromHex("efbbbf");
    case Encoding::Utf16LE: return QByteArray::fromHex("fffe");
    case Encoding::Utf16BE: return QByteArray::fromHex("feff");
    case Encoding::Utf32LE: return QByteArray::fromHex("fffe0000");
    case Encoding::Utf32BE: return QByteArray::fromHex("0000feff");
    default: return {};
    }
}
FileResult ByteEncodingAnalyzer::load(const QString& path, const std::optional<Encoding> forced, const Cancellation& cancel,
                                      const std::function<void(double)>& progress) {
    FileResult result; result.path = QFileInfo(path).absoluteFilePath();
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) { result.error = file.errorString(); return result; }
    if (file.size() > MaxFileBytes) { result.tooLarge = true; return result; }
    QByteArray bytes;
    while (!file.atEnd()) {
        if (cancelled(cancel)) { result.cancelled = true; return result; }
        const auto chunk = file.read(65536);
        if (chunk.isEmpty() && file.error() != QFileDevice::NoError) { result.error = file.errorString(); return result; }
        bytes += chunk;
        if (bytes.size() > MaxFileBytes) { result.tooLarge = true; return result; }
    }
    result = analyze(bytes, forced, cancel, progress); result.path = QFileInfo(path).absoluteFilePath(); return result;
}
FileResult ByteEncodingAnalyzer::analyze(const QByteArray& bytes, const std::optional<Encoding> forced, const Cancellation& cancel,
                                         const std::function<void(double)>& progress) {
    FileResult result;
    if (bytes.size() > MaxFileBytes) { result.tooLarge = true; return result; }
    result.bytes = bytes; result.hash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    if (cancelled(cancel)) { result.cancelled = true; return result; }
    if (binarySignature(bytes)) { result.probablyBinary = true; return result; }
    QByteArray payload = bytes;
    // UTF-32LE starts with the UTF-16LE marker: longest signatures first.
    for (auto e : {Encoding::Utf32LE, Encoding::Utf32BE, Encoding::Utf8, Encoding::Utf16LE, Encoding::Utf16BE}) {
        if (bytes.startsWith(bom(e))) { result.bomEncoding = e; payload.remove(0, bom(e).size()); break; }
    }
    auto encodings = forced ? QList{*forced} : result.bomEncoding ? QList{*result.bomEncoding} : EncodingConverter::supported();
    result.preferredEncoding = result.bomEncoding;
    if (forced && result.bomEncoding && *forced != *result.bomEncoding) { result.invalidBom = true; return result; }
    int rawControls = 0;
    const auto sample = payload.left(65536);
    for (unsigned char c : sample) if (c < 32 && c != 0 && c != '\r' && c != '\n' && c != '\t') ++rawControls;
    if (const bool hasWideEvidence = std::ranges::any_of(encodings, [&](auto e) { return wideEvidence(payload, e); });
        !result.bomEncoding && !forced && rawControls > std::max<qsizetype>(4, sample.size() / 20) && !hasWideEvidence) {
        result.probablyBinary = true; return result;
    }
    auto append = [&](FileCandidate c) {
        if (const auto existing = std::ranges::find_if(result.candidates, [&](const auto& other) { return other.text == c.text; });
            existing == result.candidates.end()) result.candidates.append(std::move(c));
        // The same text reached by a plain decode is the simpler, exact explanation:
        // a GBK file is GBK, not "Latin-1 plus a repair back to the same text".
        else if (!existing->repairPath.isEmpty() && c.repairPath.isEmpty()) *existing = std::move(c);
        else if (existing->repairPath.isEmpty() && c.repairPath.isEmpty()) {
            existing->aliases.append(c.encoding);
            if (c.score > existing->score) { existing->encoding = c.encoding; existing->score = c.score; }
        }
    };
    bool nonTextDecode = false, lossyFound = false;
    for (auto e : encodings) {
        if (cancelled(cancel)) { result.cancelled = true; result.candidates.clear(); return result; }
        const auto decoded = EncodingConverter::decode(payload, e);
        if (!decoded) continue;
        if (!textLike(*decoded)) { nonTextDecode = true; continue; }
        if (e == Encoding::Utf8 || (!result.preferredEncoding && wideEvidence(payload, e))) result.preferredEncoding = e;
        // The plain decode has no path, so pass the codec for the script check.
        double score = CandidateScorer::score(Candidate{.text = *decoded, .path = {}}, e);
        if (!result.bomEncoding) {
            if (wide(e) && !wideEvidence(payload, e)) score -= 25;
            else if (e != Encoding::Utf8 && !wide(e)) score -= 6 + CandidateScorer::legacyPrior(e);
        }
        const auto anomalies = CandidateScorer::anomalies(*decoded);
        append({.text = *decoded, .encoding = e, .aliases = {e}, .repairPath = {}, .score = std::clamp(score, 0.0, 100.0), .reversible = true, .anomalies = anomalies});
        if (anomalies == 0) continue;
        // Once the UTF-8 reading has been rebuilt as damaged GBK, repairs of other readings only
        // re-encode its replacement characters (and are discarded below): skip the search.
        if (lossyFound) continue;
        // A UTF-16/32 reading without a BOM or structural evidence is already ranked
        // far down; most such readings are runs of rare Han characters (bytes "aa"
        // become 慡). Searching repairs inside them costs time and finds nothing.
        if (wide(e) && !result.bomEncoding && !wideEvidence(payload, e)) continue;
        if (decoded->size() > MojibakeAnalyzer::MaxInputBytes || decoded->toUtf8().size() > MojibakeAnalyzer::MaxInputBytes) {
            result.inverseRepairSkipped = true; continue;
        }
        const auto repaired = MojibakeAnalyzer::analyze(*decoded, cancel, progress);
        if (!repaired.likelyMojibake) continue;
        for (const auto& candidate : repaired.candidates) {
            if (candidate.path.isEmpty() || candidate.anomalies >= anomalies) continue;
            append({.text = candidate.text, .encoding = e, .aliases = {e}, .repairPath = candidate.path, .score = candidate.score, .reversible = candidate.reversible, .anomalies = candidate.anomalies, .evidence = candidate.evidence, .lossy = candidate.lossy});
            lossyFound = lossyFound || (!candidate.path.isEmpty() && candidate.path.last().lossy);
        }
    }
    if (cancelled(cancel)) { result.cancelled = true; result.candidates.clear(); return result; }
    if (result.candidates.isEmpty()) {
        result.invalidBom = result.bomEncoding.has_value() && !nonTextDecode;
        result.probablyBinary = nonTextDecode || (!result.bomEncoding && !forced);
    }
    // A clean-looking legacy decode must not hide damage in a structurally
    // valid UTF file. Preserve the byte evidence, then rank text repairs within
    // that encoding. This preference is explicitly reported, not certainty.
    if (result.preferredEncoding) {
        double best = -1;
        for (const auto& c : result.candidates) if (c.aliases.contains(*result.preferredEncoding)) best = std::max(best, c.score);
        if (best >= 0) for (auto& c : result.candidates)
            if (!c.aliases.contains(*result.preferredEncoding)) c.score = std::min(c.score, std::max(0.0, best - 8));
    }
    std::ranges::stable_sort(result.candidates, [](const auto& a, const auto& b) { return a.score > b.score; });
    // As in MojibakeAnalyzer: a lossy GBK reconstruction is the only candidate that
    // explains the replacement pattern; conversions of the debris merely look clean.
    const auto lossy = std::ranges::find_if(result.candidates, [](const auto& c) {
        return !c.repairPath.isEmpty() && c.repairPath.last().lossy;
    });
    if (lossy != result.candidates.end()) {
        std::rotate(result.candidates.begin(), lossy, lossy + 1);
        // Other repairs only re-encode the replacement characters; plain decodings stay as alternatives.
        result.candidates.removeIf([](const auto& c) { return !c.repairPath.isEmpty() && !c.repairPath.last().lossy; });
    }
    result.candidates = result.candidates.mid(0, 5);
    return result;
}
}
