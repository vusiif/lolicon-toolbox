#include "DiagnosticReport.h"
#include "CandidateScorer.h"
#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QDateTime>
#include <QFile>
#include <cmath>
#include <algorithm>
namespace mojibake {
namespace {
QString tr(const char* source) { return QCoreApplication::translate("DiagnosticReport", source); }
QJsonObject base(const char* kind, bool bytes) {
    return {{"schema_version", 1}, {"source_kind", kind}, {"assessment", "unavailable"},
            {"original_bytes_available", bytes}, {"inverse_verified", QJsonValue::Null},
            {"possible_information_loss", false}, {"ambiguous", false},
            {"text_content_included", false}, {"paths_or_names_included", false},
            {"ai_output_included", false}, {"api_configuration_included", false},
            {"limitations", QJsonArray{"scores_are_not_probabilities", "inverse_does_not_prove_meaning"}}};
}
QJsonArray steps(const QList<Transformation>& path) {
    QJsonArray result;
    for (const auto& [encodeAs, decodeAs, segmented, partial, lossy] : path) result.append(QJsonObject{
        {"encode_as", EncodingConverter::name(encodeAs)}, {"decode_as", EncodingConverter::name(decodeAs)},
        {"segmented", segmented}, {"partial", partial}, {"lossy_reconstruction", lossy}});
    return result;
}
QJsonObject candidate(double score, bool reversible, const QList<Transformation>& path) {
    return {{"quality_score", score}, {"inverse_verified", reversible}, {"transformation_path", steps(path)}};
}
bool possibleLoss(const QString& text) { return text.contains(QChar(0xfffd)) || text.contains("??"); }
QString assessment(const bool same, const bool loss, const bool reversible) { return !reversible ? "partial" : loss || !same ? "review" : "retained"; }
}
DiagnosticReport DiagnosticReport::text(const Result& result, const int selected) {
    DiagnosticReport report{.data = base("unicode_text", false), .ready = !result.original.isEmpty() || result.tooLarge || result.cancelled};
    auto& data = report.data;
    data["possible_information_loss"] = result.possibleLoss || possibleLoss(result.original);
    data["metrics"] = QJsonObject{{"source_utf16_units", result.original.size()}, {"examined_candidates", result.generated}};
    data["evidence"] = QJsonObject{{"search_limited", result.searchLimited}, {"input_too_large", result.tooLarge}, {"cancelled", result.cancelled}};
    if (result.tooLarge) data["assessment"] = "blocked";
    QJsonArray candidates;
    for (const auto& c : result.candidates) candidates.append(candidate(c.score, c.reversible, c.path));
    data["candidates"] = candidates;
    if (selected < 0 || selected >= result.candidates.size()) return report;
    const auto& c = result.candidates[selected];
    const bool loss = result.possibleLoss || possibleLoss(result.original) || possibleLoss(c.text) || !c.reversible;
    data["assessment"] = assessment(c.text == result.original, loss, c.reversible);
    data["possible_information_loss"] = loss; data["inverse_verified"] = c.reversible;
    data["selected_candidate"] = selected + 1;
    auto metrics = data["metrics"].toObject();
    metrics["source_replacement_characters"] = result.original.count(QChar(0xfffd));
    metrics["selected_replacement_characters"] = c.text.count(QChar(0xfffd));
    metrics["selected_anomalies"] = c.anomalies; data["metrics"] = metrics;
    for (int i = 0; i < result.candidates.size(); ++i)
        if (i != selected && std::abs(result.candidates[i].score - c.score) <= 5 && result.candidates[i].text != c.text) data["ambiguous"] = true;
    return report;
}
DiagnosticReport DiagnosticReport::file(const FileResult& result, const int selected) {
    DiagnosticReport report{.data = base("text_file", !result.hash.isEmpty()), .ready = !result.path.isEmpty() || !result.hash.isEmpty() || result.tooLarge || result.cancelled};
    auto& data = report.data;
    data["paths_or_names_included"] = !result.path.isEmpty();
    data["metrics"] = QJsonObject{{"source_bytes", result.bytes.size()}, {"candidate_count", result.candidates.size()}};
    data["evidence"] = QJsonObject{{"source_path", result.path}, {"source_sha256", QString::fromLatin1(result.hash.toHex())},
        {"bom", result.bomEncoding ? QJsonValue(EncodingConverter::name(*result.bomEncoding)) : QJsonValue(QJsonValue::Null)},
        {"preferred_encoding", result.preferredEncoding ? QJsonValue(EncodingConverter::name(*result.preferredEncoding)) : QJsonValue(QJsonValue::Null)},
        {"invalid_bom_or_conflict", result.invalidBom}, {"probably_binary", result.probablyBinary},
        {"input_too_large", result.tooLarge}, {"read_error", !result.error.isEmpty()}, {"cancelled", result.cancelled},
        {"inverse_search_skipped", result.inverseRepairSkipped}};
    if (result.invalidBom || result.probablyBinary || result.tooLarge || !result.error.isEmpty()) data["assessment"] = "blocked";
    QJsonArray candidates;
    for (const auto& c : result.candidates) {
        auto meta = candidate(c.score, c.reversible, c.repairPath); QJsonArray aliases;
        for (const auto e : c.aliases) aliases.append(EncodingConverter::name(e));
        meta["source_encoding"] = EncodingConverter::name(c.encoding); meta["encoding_aliases"] = aliases; candidates.append(meta);
    }
    data["candidates"] = candidates;
    if (selected < 0 || selected >= result.candidates.size()) return report;
    const auto& c = result.candidates[selected]; const bool loss = possibleLoss(c.text) || !c.reversible;
    data["assessment"] = assessment(c.repairPath.isEmpty(), loss, c.reversible);
    data["possible_information_loss"] = loss; data["inverse_verified"] = c.reversible;
    data["selected_candidate"] = selected + 1; data["ambiguous"] = c.aliases.size() > 1;
    auto metrics = data["metrics"].toObject(); metrics["selected_utf16_units"] = c.text.size();
    metrics["selected_replacement_characters"] = c.text.count(QChar(0xfffd));
    metrics["selected_anomalies"] = c.anomalies; data["metrics"] = metrics;
    for (int i = 0; i < result.candidates.size(); ++i)
        if (i != selected && std::abs(result.candidates[i].score - c.score) <= 5) data["ambiguous"] = true;
    return report;
}
DiagnosticReport DiagnosticReport::names(const NamePlan& plan) {
    DiagnosticReport report{.data = base("name_plan", false), .ready = !plan.root.isEmpty() || !plan.error.isEmpty() || plan.cancelled};
    auto& data = report.data; const auto validation = FilenameRecovery::validate(plan);
    data["paths_or_names_included"] = !plan.root.isEmpty() || !plan.entries.isEmpty();
    const bool complete = !plan.root.isEmpty() && plan.error.isEmpty() && !plan.cancelled;
    const bool hashes = complete && std::ranges::all_of(plan.entries, [](const auto& e) { return e.directory || e.hash.size() == 32; });
    data["evidence"] = QJsonObject{{"source_root", plan.root}, {"scan_error", !plan.error.isEmpty()}, {"cancelled", plan.cancelled},
                                  {"scan_complete", complete}, {"file_hashes_available", hashes}};
    int changes = 0, errors = 0; bool loss = false, ambiguous = false, reversible = true, unverified = false;
    QJsonArray entries;
    for (int i = 0; i < plan.entries.size(); ++i) {
        const auto& e = plan.entries[i]; const auto name = e.relativePath.section('/', -1);
        const bool changed = name != e.selectedName; changes += changed; errors += !validation.errors[i].isEmpty();
        loss |= possibleLoss(name) || possibleLoss(e.selectedName);
        QJsonObject meta{{"parent_index", e.parent}, {"original_name", name}, {"selected_name", e.selectedName},
            {"directory", e.directory}, {"size_bytes", e.size}, {"source_sha256", QString::fromLatin1(e.hash.toHex())},
            {"validation_error", validation.errors[i]}};
        QJsonArray candidates; bool matched = false;
        for (int index = 0; index < e.candidates.size(); ++index) {
            const auto& c = e.candidates[index]; auto item = candidate(c.score, c.reversible, c.path);
            item["name"] = c.text; candidates.append(item);
            if (c.text != e.selectedName) continue;
            matched = true; meta["selected_candidate"] = index + 1;
            if (changed) {
                reversible &= c.reversible; loss |= !c.reversible;
                for (const auto& alternative : e.candidates)
                    if (alternative.text != c.text && std::abs(alternative.score - c.score) <= 5) ambiguous = true;
            }
        }
        if (changed && !matched) unverified = true;
        meta["candidates"] = candidates; entries.append(meta);
    }
    data["entries"] = entries;
    data["metrics"] = QJsonObject{{"entries", plan.entries.size()}, {"total_file_bytes", plan.totalBytes}, {"name_changes", changes}, {"validation_errors", errors}};
    data["assessment"] = plan.cancelled ? "unavailable" : !plan.error.isEmpty() || errors ? "blocked" : !reversible ? "partial" : changes || loss ? "review" : "retained";
    data["possible_information_loss"] = loss; data["ambiguous"] = ambiguous;
    if (complete && changes && (!reversible || !unverified)) data["inverse_verified"] = reversible;
    return report;
}
DiagnosticReport DiagnosticReport::clipboard(const ClipboardTexts& snapshot, const int selected) {
    DiagnosticReport report{.data = base("clipboard", false), .ready = true}; auto& data = report.data;
    const auto& c = snapshot.capture;
    data["evidence"] = QJsonObject{{"formats", QJsonArray::fromStringList(c.formats)}, {"rtf_not_parsed", c.hasRtf},
        {"plain_usable", snapshot.plainUsable()}, {"html_usable", snapshot.htmlUsable()},
        {"plain_too_large", c.plainTooLarge}, {"html_too_large", c.htmlTooLarge || snapshot.htmlTextTooLarge},
        {"plain_invalid_unicode", c.plainInvalid}, {"html_invalid_unicode", c.htmlInvalid || snapshot.htmlTextInvalid},
        {"representations_differ", snapshot.plainUsable() && snapshot.htmlUsable() && c.plain != snapshot.htmlText},
        {"repair_analysis_performed", false}};
    data["metrics"] = QJsonObject{{"plain_utf16_units", c.plain.size()}, {"html_text_utf16_units", snapshot.htmlText.size()},
        {"plain_replacement_characters", c.plain.count(QChar(0xfffd))}, {"html_replacement_characters", snapshot.htmlText.count(QChar(0xfffd))},
        {"plain_anomalies", CandidateScorer::anomalies(c.plain)}, {"html_anomalies", CandidateScorer::anomalies(snapshot.htmlText)}};
    const auto* text = selected == 0 && snapshot.plainUsable() ? &c.plain : selected == 1 && snapshot.htmlUsable() ? &snapshot.htmlText : nullptr;
    if (!text) {
        if (c.plainTooLarge || c.htmlTooLarge || snapshot.htmlTextTooLarge || c.plainInvalid || c.htmlInvalid || snapshot.htmlTextInvalid) data["assessment"] = "blocked";
        return report;
    }
    data["assessment"] = "source_selected"; data["selected_representation"] = selected == 0 ? "plain" : "html_text";
    data["possible_information_loss"] = possibleLoss(*text);
    data["ambiguous"] = snapshot.plainUsable() && snapshot.htmlUsable() && c.plain != snapshot.htmlText;
    return report;
}
QString DiagnosticReport::summary() const {
    if (!ready) return tr("Diagnostic report will be available after analysis or a source read.");
    const auto state = data["assessment"].toString();
    const QString status = state == "blocked" ? tr("Blocked; review input or plan errors") : state == "partial" ? tr("Partial recovery; complete inverse not proven") :
        state == "review" ? tr("Possible recovery or information loss; review required") : state == "retained" ? tr("Original retained or decoded without text repair") :
        state == "source_selected" ? tr("Source selected; repair analysis has not run") : tr("No usable result");
    const bool bytes = data["original_bytes_available"].toBool(); const auto inverse = data["inverse_verified"];
    QStringList lines{tr("Diagnosis: %1.").arg(status), tr("Source evidence: %1. Inverse: %2.").arg(
        bytes ? tr("original file bytes") : data["source_kind"].toString() == "text_file" ? tr("original bytes not captured") : tr("Unicode text or names"),
        inverse.isNull() ? tr("not verified") : inverse.toBool() ? tr("verified") : tr("not proven for the complete text"))};
    if (data["possible_information_loss"].toBool()) lines << tr("Possible information loss is a warning, not proof that a replacement character was originally different.");
    if (data["ambiguous"].toBool()) lines << tr("Ambiguity remains: equivalent encodings, different sources or candidate scores within 5 points.");
    lines << tr("Scores are not probabilities. Reversibility does not prove the original meaning.");
    return lines.join('\n');
}
bool DiagnosticReport::save(const QString& destination, QString& error) const {
    error.clear(); if (!ready) { error = tr("No diagnostic report is available."); return false; }
    auto record = data; record["exported_at_utc"] = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
    const auto bytes = QJsonDocument(record).toJson(QJsonDocument::Indented);
    QFile file(destination);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) { error = tr("Choose a new report filename; existing files are never overwritten."); return false; }
    qsizetype written = 0;
    while (written < bytes.size()) {
        const auto count = file.write(bytes.constData() + written, bytes.size() - written);
        if (count <= 0) { file.close(); file.remove(); error = tr("Could not write the complete report."); return false; }
        written += count;
    }
    if (!file.flush()) { file.close(); file.remove(); error = tr("Could not write the complete report."); return false; }
    return true;
}
}
