// Manual tool, not a ctest test, and it sends nothing: it writes the requests
// the app's AI refinement would send for a damaged lossy GBK text, one JSON
// line per chunk, so tests/harness can replay the app's protocol (E0) with
// its own client. Units are numbered within each chunk, as the app does;
// first_id is the id of the chunk's first unit among all units of the text
// (LossyGbkTool's ids: chunks hold whole spans, so the numbering agrees).
//
//   ExportAiRequests <damaged> [language]     language defaults to Simplified Chinese
#include <QCoreApplication>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTextStream>
#include "MojibakeAi.h"
#include "ai/providers/OpenAICompatibleProvider.h"
using namespace mojibake;
int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    QTextStream out(stdout);
    out.setEncoding(QStringConverter::Utf8);
    if (args.size() < 2) { out << "usage: ExportAiRequests <damaged> [language]" << Qt::endl; return 2; }
    QFile file(args[1]);
    if (!file.open(QIODevice::ReadOnly)) { out << "cannot read " << args[1] << Qt::endl; return 2; }
    auto damaged = QString::fromUtf8(file.readAll());
    if (damaged.startsWith(QChar(0xfeff))) damaged.remove(0, 1);
    const auto language = args.value(2, "Simplified Chinese");
    const auto local = LossyGbkRecovery::recover(damaged);
    if (!local.success) { out << "local recovery failed" << Qt::endl; return 2; }
    const auto glossary = LossyGbkRecovery::glossary(local, MojibakeAi::GlossaryCharacters);
    // Chunks as MojibakeAi::refine() cuts them: whole spans up to ChunkCharacters.
    std::vector<LossySpan> chunks;
    LossySpan open{}; bool started = false;
    for (const auto& s : local.spans) {
        if (!started) { open = s; started = true; } else { open.damagedEnd = s.damagedEnd; open.textEnd = s.textEnd; }
        if (open.textEnd - open.textBegin >= MojibakeAi::ChunkCharacters) { chunks.push_back(open); started = false; }
    }
    if (started) chunks.push_back(open);
    int firstId = 1;
    for (int k = 0; k < static_cast<int>(chunks.size()); ++k) {
        const auto& c = chunks[k];
        const auto units = LossyGbkRecovery::repairUnits(local, c.textBegin, c.textEnd, MojibakeAi::UnitCharacters);
        if (units.isEmpty()) continue;  // nothing to refine: the app sends no request
        const auto context = MojibakeAi::unitsRequest(local, c.textBegin, c.textEnd, units, k + 1, static_cast<int>(chunks.size()), glossary);
        const QJsonObject line{{"chunk", k + 1}, {"chunks", static_cast<int>(chunks.size())}, {"first_id", firstId},
                               {"units", static_cast<int>(units.size())}, {"text_begin", static_cast<int>(c.textBegin)},
                               {"text_end", static_cast<int>(c.textEnd)}, {"messages", ai::OpenAICompatibleProvider::messages(context, language)}};
        out << QString::fromUtf8(QJsonDocument(line).toJson(QJsonDocument::Compact)) << '\n';
        firstId += static_cast<int>(units.size());
    }
    out.flush();
    return 0;
}
