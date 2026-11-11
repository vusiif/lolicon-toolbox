#include "MojibakeAi.h"
#include "services/AppSettings.h"
#include "ai/AiService.h"
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonObject>
#include <QtConcurrentRun>
using namespace mojibake;
MojibakeAi::MojibakeAi(AppSettings* settings, QObject* parent) : QObject(parent), m_settings(settings) {
    for (int i = 0; i < Parallel; ++i) {
        auto* service = new ai::AiService(this);
        m_services.push_back(service); m_running.push_back(-1);
        connect(service, &ai::AiService::completed, this, [this, i](const ai::Reply& reply) { finishChunk(i, reply); });
        connect(service, &ai::AiService::failed, this, [this, i](const QString& error) { failChunk(i, error); });
        connect(service, &ai::AiService::cancelled, this, [this, i] { m_running[i] = -1; });
        connect(service, &ai::AiService::progress, this, [this](const bool thinking, const int characters) {
            if (m_refining) return;
            m_thinking = thinking; m_characters = characters; updateStatus(); emit changed();
        });
    }
    m_ticker.setInterval(1000);
    connect(&m_ticker, &QTimer::timeout, this, [this] { updateStatus(); emit changed(); });
}
MojibakeAi::~MojibakeAi() {
    for (auto* service : m_services) { static_cast<void>(service->disconnect(this)); service->cancel(); }
}
bool MojibakeAi::configured() const { return m_settings && ai::AiService::validate(m_settings->aiConfiguration()).isEmpty(); }
QString MojibakeAi::target() const {
    if (!m_settings) return {};
    const auto c = m_settings->aiConfiguration();
    return c.baseUrl + " · " + c.model;
}
int MojibakeAi::chunkCount(const LossyRecovery& local) {
    int count = 0; qsizetype length = 0; bool open = false, needed = false;
    for (const auto& span : local.spans) {
        length += span.textEnd - span.textBegin; open = true;
        const auto marks = local.evidence.mid(span.textBegin, span.textEnd - span.textBegin);
        needed = needed || marks.contains(static_cast<char>(CharEvidence::Partial)) || marks.contains(static_cast<char>(CharEvidence::Unknown));
        if (length >= ChunkCharacters) { count += needed; length = 0; open = false; needed = false; }
    }
    return count + (open && needed);
}
void MojibakeAi::reset() {
    ++m_generation;
    for (int i = 0; i < Parallel; ++i) { m_running[i] = -1; m_services[i]->cancel(); }
    m_ticker.stop();
    m_chunks.clear(); m_local.reset(); m_reply = {};
    m_text.clear(); m_evidence.clear(); m_status.clear(); m_lastError.clear(); ++m_revision;
    m_done = m_failed = m_needed = 0; m_busy = m_refining = m_cancelled = false;
    emit changed();
}
void MojibakeAi::cancel() {
    if (!m_busy) return;
    m_cancelled = true;
    for (int i = 0; i < Parallel; ++i) {
        if (m_running[i] >= 0 && m_refining) m_chunks[m_running[i]].state = Chunk::Pending;
        m_running[i] = -1; m_services[i]->cancel();
    }
    m_busy = false; m_ticker.stop();
    m_status = m_refining ? tr("AI refinement stopped: %1 of %2 chunks verified. Run it again to continue with the rest.").arg(m_done).arg(m_needed)
                          : tr("AI request cancelled. Local results are unchanged.");
    emit changed();
}
void MojibakeAi::suggest(const QString& original, const QList<Candidate>& candidates, bool possibleLoss) {
    if (!configured() || m_busy) return;
    reset();
    QJsonArray list;
    for (int i = 0; i < candidates.size(); ++i) {
        const auto& candidate = candidates[i];
        QJsonArray path;
        for (const auto& step : candidate.path) path.append(QJsonObject{
            {"encode_as", EncodingConverter::name(step.encodeAs)}, {"decode_as", EncodingConverter::name(step.decodeAs)},
            {"segmented", step.segmented}, {"partial", step.partial}});
        list.append(QJsonObject{{"index", i + 1}, {"text", candidate.text}, {"quality_score", candidate.score},
                                {"inverse_verified", candidate.reversible}, {"path", path}});
    }
    const QJsonObject context{{"original_text", original}, {"candidates", list}, {"possible_loss", possibleLoss}};
    m_busy = true; m_characters = 0; m_thinking = false;
    m_elapsed.start(); m_ticker.start();
    m_running[0] = 0;
    updateStatus();
    const auto generation = m_generation;
    m_services[0]->analyze(m_settings->aiConfiguration(), context, m_settings->locale());
    if (generation == m_generation && m_busy) emit changed();  // A synchronous failure has already reported.
}
void MojibakeAi::refine(const std::shared_ptr<const LossyRecovery>& local) {
    if (!configured() || m_busy || !local || !local->success) return;
    if (m_refining && m_local == local) {
        // Continue: keep verified chunks, retry the rest.
        for (auto& chunk : m_chunks) if (chunk.state == Chunk::Failed) { chunk.state = Chunk::Pending; --m_failed; }
    } else {
        reset();
        m_refining = true; m_local = local;
        Chunk chunk; bool open = false;
        for (const auto& [damagedBegin, damagedEnd, textBegin, textEnd] : local->spans) {
            if (!open) { chunk = {.damagedBegin = damagedBegin, .damagedEnd = damagedEnd, .textBegin = textBegin, .textEnd = textEnd}; open = true; }
            else { chunk.damagedEnd = damagedEnd; chunk.textEnd = textEnd; }
            if (chunk.textEnd - chunk.textBegin >= ChunkCharacters) { m_chunks.push_back(chunk); open = false; }
        }
        if (open) m_chunks.push_back(chunk);
        for (auto& c : m_chunks) {
            const auto marks = local->evidence.mid(c.textBegin, c.textEnd - c.textBegin);
            c.needed = marks.contains(static_cast<char>(CharEvidence::Partial)) || marks.contains(static_cast<char>(CharEvidence::Unknown));
            m_needed += c.needed;
        }
        rebuild();
    }
    m_cancelled = false; m_lastError.clear();
    if (!m_needed) { m_status = tr("Nothing to refine: every character is backed by surviving bytes."); emit changed(); return; }
    m_busy = true; m_elapsed.start(); m_ticker.start();
    dispatch();
    updateStatus(); emit changed();
}
void MojibakeAi::dispatch() {
    if (!m_busy || !m_refining) return;
    for (int i = 0; i < Parallel; ++i) {
        if (m_running[i] >= 0 || m_services[i]->isBusy()) continue;
        int next = -1;
        for (int k = 0; k < static_cast<int>(m_chunks.size()); ++k)
            if (m_chunks[k].needed && m_chunks[k].state == Chunk::Pending) { next = k; break; }
        if (next < 0) break;
        auto& chunk = m_chunks[next];
        chunk.state = Chunk::Running; m_running[i] = next;
        const QJsonObject context{{"task", "lossy_gbk_refine"}, {"draft", LossyGbkRecovery::markup(*m_local, chunk.textBegin, chunk.textEnd, Options)},
                                  {"chunk", next + 1}, {"chunks", static_cast<int>(m_chunks.size())},
                                  {"lost_count_exact", m_local->replacement == Utf8Replacement::PerByte}};
        m_services[i]->analyze(m_settings->aiConfiguration(), context, m_settings->locale());
        if (!m_busy) return;  // A synchronous failure stopped the run.
    }
    finish();
}
void MojibakeAi::finishChunk(const int service, const ai::Reply& reply) {
    const int index = m_running[service];
    m_running[service] = -1;
    if (index < 0 || !m_busy) return;
    if (!m_refining) {
        m_reply = reply; m_text = reply.text; m_busy = false; m_ticker.stop(); ++m_revision;
        m_status = reply.text.isEmpty() ? tr("AI could not propose a reliable recovery. See its explanation.")
                                        : tr("AI suggestion received. Verify the text and any inferred characters.");
        emit changed(); return;
    }
    // guided() reads markup the model copied back; only a reply without any character is useless.
    const auto guide = reply.text;
    if (QString(guide).remove(QChar(0x3014)).remove(QChar(0x3015)).remove(QChar(LossyGbkRecovery::UnknownMark)).trimmed().isEmpty()) {
        m_chunks[index].state = Chunk::Failed; ++m_failed;
        m_lastError = tr("The AI returned no text for a chunk.");
        QMetaObject::invokeMethod(this, &MojibakeAi::dispatch, Qt::QueuedConnection);
        updateStatus(); emit changed(); return;
    }
    // Re-solve the chunk with the AI text as a guide; surviving bytes always win.
    const auto& chunk = m_chunks[index];
    const QString damaged = m_local->source.mid(chunk.damagedBegin, chunk.damagedEnd - chunk.damagedBegin);
    const auto replacement = m_local->replacement;
    const auto generation = m_generation;
    ++m_verifying;
    auto* watcher = new QFutureWatcher<LossyRecovery>(this);
    connect(watcher, &QFutureWatcher<LossyRecovery>::finished, this, [this, watcher, index, generation] {
        watcher->deleteLater(); --m_verifying;
        if (generation != m_generation || index >= static_cast<int>(m_chunks.size())) return;
        auto& verified = m_chunks[index];
        if (const auto result = watcher->result(); result.success && verified.state == Chunk::Running) {
            verified.result = result; verified.state = Chunk::Done; ++m_done;
        } else if (verified.state == Chunk::Running) { verified.state = Chunk::Failed; ++m_failed; }
        rebuild(); finish(); updateStatus(); emit changed();
    });
    watcher->setFuture(QtConcurrent::run([damaged, guide, replacement] { return LossyGbkRecovery::guided(damaged, guide, replacement); }));
    QMetaObject::invokeMethod(this, &MojibakeAi::dispatch, Qt::QueuedConnection);
}
void MojibakeAi::failChunk(const int service, const QString& error) {
    const int index = m_running[service];
    m_running[service] = -1;
    if (!m_busy) return;
    m_lastError = error;
    if (!m_refining) { m_busy = false; m_ticker.stop(); m_status = error; emit changed(); return; }
    if (index >= 0) { m_chunks[index].state = Chunk::Failed; ++m_failed; }
    // A wrong key, address or model fails every chunk: stop instead of repeating it.
    if (m_done == 0 && m_failed >= Parallel) {
        for (int i = 0; i < Parallel; ++i) {
            if (m_running[i] >= 0) m_chunks[m_running[i]].state = Chunk::Pending;
            m_running[i] = -1; m_services[i]->cancel();
        }
        m_busy = false; m_ticker.stop();
        m_status = tr("AI refinement stopped after repeated failures: %1").arg(error);
        emit changed(); return;
    }
    QMetaObject::invokeMethod(this, &MojibakeAi::dispatch, Qt::QueuedConnection);
    updateStatus(); emit changed();
}
void MojibakeAi::rebuild() {
    m_text.clear(); m_evidence.clear(); ++m_revision;
    if (!m_local) return;
    for (const auto& chunk : m_chunks) {
        if (chunk.state == Chunk::Done && chunk.result) { m_text += chunk.result->text; m_evidence += chunk.result->evidence; }
        else {
            m_text += m_local->text.mid(chunk.textBegin, chunk.textEnd - chunk.textBegin);
            m_evidence += m_local->evidence.mid(chunk.textBegin, chunk.textEnd - chunk.textBegin);
        }
    }
}
void MojibakeAi::finish() {
    if (!m_busy || !m_refining || m_verifying) return;
    for (int i = 0; i < Parallel; ++i) if (m_running[i] >= 0) return;
    for (const auto& chunk : m_chunks) if (chunk.needed && chunk.state == Chunk::Pending) return;
    m_busy = false; m_ticker.stop();
    m_status = m_failed ? tr("AI refinement finished: %1 of %2 chunks verified, %3 failed. Run it again to retry the failed chunks.").arg(m_done).arg(m_needed).arg(m_failed)
                        : tr("AI refinement finished: all %1 chunks were checked against the surviving bytes.").arg(m_needed);
    // Reached from queued dispatches too, where no caller reports the change.
    QMetaObject::invokeMethod(this, &MojibakeAi::changed, Qt::QueuedConnection);
}
void MojibakeAi::updateStatus() {
    if (!m_busy) return;
    const auto seconds = m_elapsed.elapsed() / 1000;
    if (m_refining) {
        int running = 0;
        for (int i = 0; i < Parallel; ++i) running += m_running[i] >= 0;
        m_status = tr("AI refinement: %1 of %2 chunks done, %3 in progress, %4 s elapsed.").arg(m_done + m_failed).arg(m_needed).arg(running + m_verifying).arg(seconds);
        return;
    }
    if (!m_characters) m_status = tr("Waiting for the service to start responding… %1 s elapsed (90 s limit).").arg(seconds);
    else if (m_thinking) m_status = tr("The model is thinking… %1 s elapsed, %2 reasoning characters received.").arg(seconds).arg(m_characters);
    else m_status = tr("Receiving AI reply… %1 s elapsed, %2 output characters received. The result will be validated when complete.").arg(seconds).arg(m_characters);
}
QStringList MojibakeAi::explanation() const {
    QStringList lines;
    if (m_refining) {
        int filled = 0, rejected = 0;
        for (const auto& chunk : m_chunks) if (chunk.state == Chunk::Done && chunk.result) { filled += chunk.result->guided; rejected += chunk.result->guideRejected; }
        lines << tr("AI refinement: %1 of %2 chunks verified against the surviving bytes; %3 failed.").arg(m_done).arg(m_needed).arg(m_failed);
        lines << tr("AI filled %1 lost characters. %2 AI characters contradicted surviving bytes and were not used.").arg(filled).arg(rejected);
        lines << tr("Characters filled by AI are contextual guesses, not recovered bytes. Chunks without a verified AI reply keep the local result.");
        if (!m_lastError.isEmpty()) lines << tr("Last error: %1").arg(m_lastError);
        return lines;
    }
    if (m_reply.reason.isEmpty() && m_text.isEmpty()) return lines;
    lines << m_reply.reason;
    if (m_reply.selectedCandidate >= 0) lines << tr("AI preferred local candidate %1.").arg(m_reply.selectedCandidate + 1);
    if (!m_reply.guesses.isEmpty()) lines << tr("Contextual guesses reported by the model:") << m_reply.guesses;
    lines << tr("All AI output is a suggestion. Inferred characters are not verified byte recovery.");
    return lines;
}
