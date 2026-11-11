#pragma once
#include "LossyGbkRecovery.h"
#include "ai/AiProvider.h"
#include <QElapsedTimer>
#include <QJsonObject>
#include <QObject>
#include <QTimer>
#include <memory>
#include <vector>
class AppSettings;
namespace ai { class AiService; }
// AI assistance shared by the text and file recovery pages.
// suggest(): one semantic suggestion for ordinary mojibake (at most 16 KiB).
// refine(): a lossy GBK reconstruction is sent in chunks; every AI chunk is
// re-solved against the surviving bytes (LossyGbkRecovery::guided), so AI text
// never overrides a byte that survived. Nothing is sent before the user asks.
class MojibakeAi : public QObject {
    Q_OBJECT
public:
    static constexpr int ChunkCharacters = 900;
    static constexpr int Parallel = 3;
    // Alternatives listed for each partial character: the top five hold the
    // original about four times in five, against one in two for the top one.
    static constexpr int Options = 5;
    // Read-only text on each side of a chunk, so a sentence cut by the chunk
    // boundary is still understood, and the length of the document glossary.
    static constexpr int ContextCharacters = 150;
    static constexpr int GlossaryCharacters = 400;
    // The request for one chunk: its draft, the neighbouring text and the glossary.
    static QJsonObject refineRequest(const mojibake::LossyRecovery& local, qsizetype textBegin, qsizetype textEnd,
                                     int chunk, int chunks, const QStringList& glossary);
    // Longest damaged span answered at once: long enough for a word, short enough
    // that a wrong guess cannot shift the rest of the chunk.
    static constexpr int UnitCharacters = 8;
    // The per-span request: the draft with every damaged span as ⟦id:...⟧ and the
    // number of characters each one takes. The model answers each id on its own.
    static QJsonObject unitsRequest(const mojibake::LossyRecovery& local, qsizetype textBegin, qsizetype textEnd,
                                    const QList<mojibake::RepairUnit>& units, int chunk, int chunks, const QStringList& glossary);
    explicit MojibakeAi(AppSettings* settings, QObject* parent = nullptr);
    ~MojibakeAi() override;
    bool configured() const;
    QString target() const;  // "address · model"
    bool busy() const { return m_busy; }
    static int chunkCount(const mojibake::LossyRecovery& local);
    // Share of characters with no surviving byte from which AI refinement is not
    // worth its cost. Measured on the corpus (about 45% lost, tests/DiagnoseAiRefinement):
    // mimo-v2.6-pro and -flash guessed about 4% of lost characters right and
    // picked partly damaged ones no better than the local word model.
    static constexpr double HeavyLoss = 0.2;
    // A warning for the AI hint when local is that damaged, otherwise empty.
    static QString heavyDamageWarning(const mojibake::LossyRecovery& local);
    void suggest(const QString& original, const QList<mojibake::Candidate>& candidates, bool possibleLoss);
    void refine(const std::shared_ptr<const mojibake::LossyRecovery>& local);
    // On by default. Off sends each chunk without neighbouring text or glossary,
    // to measure what the context is worth (tests/RunAiRefinement.cpp).
    void setContextEnabled(bool enabled) { m_context = enabled; }
    // On by default: the model fills each damaged span by id. Off asks it to
    // rewrite whole chunks, the earlier protocol, kept for comparison and as the
    // fallback for a reply without fills.
    void setUnitsEnabled(bool enabled) { m_units = enabled; }
    void cancel();
    void reset();  // cancel and forget any result
    bool hasResult() const { return !m_text.isEmpty(); }
    bool refined() const { return m_refining; }
    QString text() const { return m_text; }
    quint64 revision() const { return m_revision; }  // changes whenever text() or evidence() does
    QByteArray evidence() const { return m_evidence; }
    QString status() const { return m_status; }
    QStringList explanation() const;
    int progress() const { return m_done + m_failed; }
    int progressMaximum() const { return m_needed; }
signals:
    void changed();
private:
    struct Chunk {
        qsizetype damagedBegin = 0, damagedEnd = 0, textBegin = 0, textEnd = 0;
        bool needed = false;
        enum State { Pending, Running, Done, Failed } state = Pending;
        std::optional<mojibake::LossyRecovery> result;
        QList<mojibake::RepairUnit> units;
        int answered = 0, wrongLength = 0;  // spans the model filled; fills of the wrong length
    };
    void dispatch();
    void finishChunk(int service, const ai::Reply& reply);
    void failChunk(int service, const QString& error);
    void rebuild();
    void updateStatus();
    void finish();
    AppSettings* m_settings;
    std::vector<ai::AiService*> m_services;
    std::vector<int> m_running;  // chunk index per service, or -1
    std::vector<Chunk> m_chunks;
    std::shared_ptr<const mojibake::LossyRecovery> m_local;
    QStringList m_glossary;
    ai::Reply m_reply;
    QString m_text, m_status, m_lastError;
    QByteArray m_evidence;
    QElapsedTimer m_elapsed;
    QTimer m_ticker;
    quint64 m_generation = 0, m_revision = 0;
    int m_done = 0, m_failed = 0, m_needed = 0, m_verifying = 0;
    int m_characters = 0;
    bool m_thinking = false, m_busy = false, m_refining = false, m_cancelled = false, m_context = true, m_units = true;
};
