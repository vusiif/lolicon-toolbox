// Manual tool, not a ctest test: it calls the AI service configured in the
// app's own settings (the key stays DPAPI-protected and is never printed) and
// costs real requests. For every lossy GBK case in the corpus it runs the
// app's AI refinement and saves the verified text, ready for
//   MOJIBAKE_CORPUS_GUIDES=<output> TestMojibakeCorpus lossyGbk
//
// RunAiRefinement <corpus dir> <output dir> [--no-context] [--rewrite] [--only <document>] [--dump-first]
// --rewrite asks for whole-chunk rewrites (the protocol before per-span fills).
// Progress and the app's AI explanation go to <output dir>/run.log.
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>
#include "MojibakeAi.h"
#include "ai/AiService.h"
#include "services/AppSettings.h"
using namespace mojibake;
int main(int argc, char* argv[]) {
    QApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() < 3) { qCritical("usage: RunAiRefinement <corpus dir> <output dir> [--no-context]"); return 2; }
    const QDir corpus(args[1]), output(args[2]);
    const bool context = !args.contains("--no-context");
    const auto onlyAt = args.indexOf("--only");
    const auto only = onlyAt > 0 && onlyAt + 1 < args.size() ? args[onlyAt + 1] : QString();
    QDir().mkpath(output.path());
    QFile log(output.filePath("run.log"));
    if (!log.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) return 2;
    const auto note = [&log](const QString& text) { log.write(text.toUtf8() + '\n'); log.flush(); qInfo().noquote() << text; };
    AppSettings settings{QString()};
    MojibakeAi ai(&settings);
    ai.setContextEnabled(context);
    const bool units = !args.contains("--rewrite");
    ai.setUnitsEnabled(units);
    if (!ai.configured()) { qCritical("No AI service is configured in the app."); return 2; }
    note("Model: " + ai.target() + (context ? " with context" : " without context") + (units ? ", per-span fills" : ", whole-chunk rewrite"));
    int failures = 0;
    for (const auto& document : corpus.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name)) {
        if (!only.isEmpty() && document != only) continue;
        QFile manifestFile(corpus.filePath(document + "/manifest.json"));
        if (!manifestFile.open(QIODevice::ReadOnly)) continue;
        for (const auto& value : QJsonDocument::fromJson(manifestFile.readAll()).object().value("cases").toArray()) {
            const auto o = value.toObject();
            if (o.value("id").toString() != "gbk_as_utf8_replace" || o.value("status").toString() != "ok") continue;
            QFile caseFile(corpus.filePath(document + '/' + o.value("text_file").toString()));
            if (!caseFile.open(QIODevice::ReadOnly)) continue;
            auto local = std::make_shared<const LossyRecovery>(LossyGbkRecovery::recover(QString::fromUtf8(caseFile.readAll())));
            if (args.contains("--dump-first")) {
                // One raw exchange for the first chunk, to read what the model makes of the draft.
                const auto end = std::min<qsizetype>(local->text.size(), MojibakeAi::ChunkCharacters);
                const auto glossary = LossyGbkRecovery::glossary(*local, MojibakeAi::GlossaryCharacters);
                const auto request = units ? MojibakeAi::unitsRequest(*local, 0, end, LossyGbkRecovery::repairUnits(*local, 0, end, MojibakeAi::UnitCharacters), 1, 1, glossary)
                                           : MojibakeAi::refineRequest(*local, 0, end, 1, 1, glossary);
                ai::AiService service;
                QEventLoop wait;
                QObject::connect(&service, &ai::AiService::completed, &wait, [&](const ai::Reply& reply) {
                    note("DRAFT:\n" + request.value("draft").toString() + "\nREPLY:\n" + reply.text +
                         QString::fromUtf8(QJsonDocument(reply.fills).toJson(QJsonDocument::Compact)) + "\nREASON: " + reply.reason); wait.quit(); });
                QObject::connect(&service, &ai::AiService::failed, &wait, [&](const QString& error) { note("FAILED: " + error); wait.quit(); });
                note(QStringLiteral("Request: %1 bytes, %2 spans").arg(QJsonDocument(request).toJson(QJsonDocument::Compact).size())
                         .arg(request.value("units").toArray().size()));
                QElapsedTimer timer; timer.start();
                QObject::connect(&service, &ai::AiService::completed, &wait, [&] { note(QStringLiteral("Reply after %1 ms").arg(timer.elapsed())); });
                service.analyze(settings.aiConfiguration(), request, settings.locale());
                wait.exec();
                return 0;
            }
            QEventLoop loop;
            const auto connection = QObject::connect(&ai, &MojibakeAi::changed, &loop, [&] { if (!ai.busy()) loop.quit(); });
            ai.refine(local);
            if (ai.busy()) loop.exec();
            QObject::disconnect(connection);
            const auto id = o.value("id").toString();
            note(document + ' ' + id + " | " + ai.status());
            for (const auto& line : ai.explanation()) note("    " + line);
            if (!ai.hasResult()) { ++failures; continue; }
            QDir().mkpath(output.filePath(document));
            QFile out(output.filePath(document + '/' + id + ".txt"));
            if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) { ++failures; continue; }
            out.write(ai.text().toUtf8());
            ai.reset();
        }
    }
    return failures ? 1 : 0;
}
