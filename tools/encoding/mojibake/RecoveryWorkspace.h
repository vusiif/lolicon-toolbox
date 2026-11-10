#pragma once
#include <ElaPushButton.h>
#include <ElaPlainTextEdit.h>
#include <QList>
#include <QWidget>
class QLabel;
class QButtonGroup;
class QVBoxLayout;
class EvidenceHighlighter;
// One entry of the candidate list shared by the text and file recovery pages.
struct WorkspaceCandidate {
    QString title;     // what happened, in words ("UTF-8 read as GBK")
    QString badge;     // "Recommended", "Original", "AI" or empty
    bool accentBadge = true;  // primary-coloured badge; false for a neutral one
    QString subtitle;  // evidence summary
    QString text;
    QByteArray evidence;  // optional per-QChar mojibake::CharEvidence
    double score = -1;    // < 0 hides the score bar
};
// A checkable card: title and badge, one-line evidence summary, preview and score bar.
class CandidateCard : public ElaPushButton {
    Q_OBJECT
public:
    explicit CandidateCard(QWidget* parent = nullptr);
    void setContent(const WorkspaceCandidate& content);
    QSize sizeHint() const override;
protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
private:
    WorkspaceCandidate m_content;
    bool m_hover = false;
};
// Read-only preview that colours characters by how much byte evidence backs them.
class EvidenceView : public ElaPlainTextEdit {
    Q_OBJECT
public:
    explicit EvidenceView(QWidget* parent = nullptr);
    // Shows at most limit characters; returns the number shown.
    qsizetype show(const QString& text, const QByteArray& evidence, qsizetype limit);
    static QColor color(char evidence, bool dark);
private:
    EvidenceHighlighter* m_highlighter;
};
class RecoveryWorkspace : public QWidget {
    Q_OBJECT
public:
    static constexpr int LocalSlots = 5;
    // Card object names: <prefix>Candidate1..5 and <prefix>AiCandidate.
    RecoveryWorkspace(const QString& prefix, const QString& previewName, QWidget* parent = nullptr);
    // Local candidates fill slots 1-5; an optional AI result takes the extra card at the top.
    void setCandidates(const QList<WorkspaceCandidate>& local, const WorkspaceCandidate* ai);
    // -1 = nothing, 0..4 = local, LocalSlots = AI.
    int selected() const { return m_selected; }
    void select(int index);
    EvidenceView* preview() const { return m_preview; }
    void setPreviewLimit(qsizetype limit) { m_limit = limit; }
    qsizetype shownCharacters() const { return m_shown; }
    void setPlaceholder(const QString& text);
    ElaPushButton* card(int index) const;
signals:
    void selectionChanged(int index);
protected:
    void changeEvent(QEvent* event) override;
private:
    void showSelected();
    void retranslate();
    QList<WorkspaceCandidate> m_local;
    WorkspaceCandidate m_ai;
    bool m_hasAi = false;
    QList<CandidateCard*> m_cards;  // LocalSlots local cards, then the AI card
    QButtonGroup* m_group;
    EvidenceView* m_preview;
    QLabel *m_listTitle, *m_legend, *m_stats;
    int m_selected = -1, m_lastShown = -1;
    qsizetype m_limit = 32768, m_shown = 0;
};
