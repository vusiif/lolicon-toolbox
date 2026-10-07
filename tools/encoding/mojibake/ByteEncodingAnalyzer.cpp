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
bool wide(Encoding e) { return e == Encoding::Utf16LE || e == Encoding::Utf16BE || e == Encoding::Utf32LE || e == Encoding::Utf32BE; }
bool wideEvidence(const QByteArray& bytes, Encoding e) {
    if (!wide(e) || bytes.size() < 4) return false;
    const bool four = e == Encoding::Utf32LE || e == Encoding::Utf32BE;
    const bool little = e == Encoding::Utf16LE || e == Encoding::Utf32LE;
    int zeros = 0, total = 0;
    for (qsizetype i = 0; i < std::min<qsizetype>(bytes.size(), 65536); ++i) {
        const auto position = i % (four ? 4 : 2);
        const bool high = four ? (little ? position >= 2 : position < 2) : position == (little ? 1 : 0);
        if (high) { ++total; if (bytes[i] == 0) ++zeros; }
    }
    return total && double(zeros) / total >= 0.65;
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
QByteArray ByteEncodingAnalyzer::bom(Encoding e) {
    switch (e) {
    case Encoding::Utf8: return QByteArray::fromHex("efbbbf");
    case Encoding::Utf16LE: return QByteArray::fromHex("fffe");
    case Encoding::Utf16BE: return QByteArray::fromHex("feff");
    case Encoding::Utf32LE: return QByteArray::fromHex("fffe0000");
    case Encoding::Utf32BE: return QByteArray::fromHex("0000feff");
    default: return {};
    }
}
FileResult ByteEncodingAnalyzer::load(const QString& path, std::optional<Encoding> forced, const Cancellation& cancel) {
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
    result = analyze(bytes, forced, cancel); result.path = QFileInfo(path).absoluteFilePath(); return result;
}
FileResult ByteEncodingAnalyzer::analyze(const QByteArray& bytes, std::optional<Encoding> forced, const Cancellation& cancel) {
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
    auto encodings = forced ? QList<Encoding>{*forced} : result.bomEncoding ? QList<Encoding>{*result.bomEncoding} : EncodingConverter::supported();
    result.preferredEncoding = result.bomEncoding;
    if (forced && result.bomEncoding && *forced != *result.bomEncoding) { result.invalidBom = true; return result; }
    int rawControls = 0;
    const auto sample = payload.left(65536);
    for (unsigned char c : sample) if (c < 32 && c != 0 && c != '\r' && c != '\n' && c != '\t') ++rawControls;
    const bool hasWideEvidence = std::any_of(encodings.begin(), encodings.end(), [&](auto e) { return wideEvidence(payload, e); });
    if (!result.bomEncoding && !forced && rawControls > std::max<qsizetype>(4, sample.size() / 20) && !hasWideEvidence) { result.probablyBinary = true; return result; }
    auto append = [&](FileCandidate c) {
        auto existing = std::find_if(result.candidates.begin(), result.candidates.end(), [&](const auto& other) { return other.text == c.text; });
        if (existing == result.candidates.end()) result.candidates.append(std::move(c));
        else if (existing->repairPath.isEmpty() && c.repairPath.isEmpty()) {
            existing->aliases.append(c.encoding);
            if (c.score > existing->score) { existing->encoding = c.encoding; existing->score = c.score; }
        }
    };
    bool nonTextDecode = false;
    for (auto e : encodings) {
        if (cancelled(cancel)) { result.cancelled = true; result.candidates.clear(); return result; }
        const auto decoded = EncodingConverter::decode(payload, e);
        if (!decoded) continue;
        if (!textLike(*decoded)) { nonTextDecode = true; continue; }
        if (e == Encoding::Utf8 || (!result.preferredEncoding && wideEvidence(payload, e))) result.preferredEncoding = e;
        double score = CandidateScorer::score(Candidate{*decoded, {}});
        if (!result.bomEncoding) {
            if (wide(e) && !wideEvidence(payload, e)) score -= 25;
            else if (e != Encoding::Utf8 && !wide(e)) score -= 6;
        }
        append({*decoded, e, {e}, {}, std::clamp(score, 0.0, 100.0), true});
        if (CandidateScorer::anomalies(*decoded) == 0) continue;
        if (decoded->size() > MojibakeAnalyzer::MaxInputBytes || decoded->toUtf8().size() > MojibakeAnalyzer::MaxInputBytes) {
            result.inverseRepairSkipped = true; continue;
        }
        const auto repaired = MojibakeAnalyzer::analyze(*decoded, cancel);
        if (!repaired.likelyMojibake) continue;
        for (const auto& candidate : repaired.candidates) {
            if (candidate.path.isEmpty() || candidate.anomalies >= CandidateScorer::anomalies(*decoded)) continue;
            append({candidate.text, e, {e}, candidate.path, candidate.score, candidate.reversible});
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
    std::stable_sort(result.candidates.begin(), result.candidates.end(), [](const auto& a, const auto& b) { return a.score > b.score; });
    result.candidates = result.candidates.mid(0, 5);
    return result;
}
}
