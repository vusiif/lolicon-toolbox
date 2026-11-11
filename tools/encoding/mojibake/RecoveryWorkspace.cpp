#include "RecoveryWorkspace.h"
#include "LossyGbkRecovery.h"
#include "UiTheme.h"
#include <ElaTheme.h>
#include <QButtonGroup>
#include <QEvent>
#include <QLabel>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QSyntaxHighlighter>
#include <QTextBlock>
#include <QVBoxLayout>
using mojibake::CharEvidence;
namespace {
QColor themeColor(const ElaThemeType::ThemeColor role) { return eTheme->getThemeColor(eTheme->getThemeMode(), role); }
bool darkTheme() { return eTheme->getThemeMode() == ElaThemeType::Dark; }
QString oneLine(QString text) {
    text.replace("\r\n", " ").replace('\n', ' ').replace('\r', ' ').replace('\t', ' ');
    return text.left(200);
}
}
// ------------------------------------------------------------------ card
CandidateCard::CandidateCard(QWidget* parent) : ElaPushButton(parent) {
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    setFixedHeight(CandidateCard::sizeHint().height());
    connect(eTheme, &ElaTheme::themeModeChanged, this, [this] { update(); });
}
QSize CandidateCard::sizeHint() const { return {240, 84}; }
void CandidateCard::setContent(const WorkspaceCandidate& content) {
    m_content = content;
    // Accessible names and tooltips carry what the custom painting shows.
    setToolTip(content.title + (content.subtitle.isEmpty() ? QString() : "\n" + content.subtitle));
    setAccessibleName(content.title);
    update();
}
void CandidateCard::enterEvent(QEnterEvent* event) { m_hover = true; update(); ElaPushButton::enterEvent(event); }
void CandidateCard::leaveEvent(QEvent* event) { m_hover = false; update(); ElaPushButton::leaveEvent(event); }
void CandidateCard::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::TextAntialiasing);
    const bool on = isChecked();
    const QRectF frame = QRectF(rect()).adjusted(1, 1, -1, -1);
    QPainterPath shape; shape.addRoundedRect(frame, 7, 7);
    p.fillPath(shape, themeColor(ElaThemeType::BasicBase));
    if (on || m_hover) {
        QColor tint = themeColor(on ? ElaThemeType::PrimaryNormal : ElaThemeType::BasicHover);
        if (on) tint.setAlpha(darkTheme() ? 70 : 34);
        p.fillPath(shape, tint);
    }
    p.setPen(QPen(themeColor(on ? ElaThemeType::PrimaryNormal : ElaThemeType::BasicBorder), on ? 1.6 : 1.0));
    p.drawPath(shape);
    const QRect area = rect().adjusted(12, 8, -12, -9);
    int top = area.top();
    // Title line with an optional badge.
    QFont badgeFont = font(); badgeFont.setPixelSize(11); badgeFont.setBold(true);
    int badgeWidth = 0;
    if (!m_content.badge.isEmpty()) {
        const QFontMetrics metrics(badgeFont);
        badgeWidth = metrics.horizontalAdvance(m_content.badge) + 12;
        const QRect badge(area.right() - badgeWidth + 1, top, badgeWidth, metrics.height() + 2);
        const bool accent = m_content.accentBadge;
        p.setPen(Qt::NoPen);
        p.setBrush(themeColor(accent ? ElaThemeType::PrimaryNormal : ElaThemeType::BasicBaseDeep));
        p.drawRoundedRect(badge, 4, 4);
        p.setFont(badgeFont);
        p.setPen(themeColor(accent ? ElaThemeType::BasicTextInvert : ElaThemeType::BasicText));
        p.drawText(badge, Qt::AlignCenter, m_content.badge);
    }
    QFont titleFont = font(); titleFont.setPixelSize(13); titleFont.setBold(true);
    const QFontMetrics titleMetrics(titleFont);
    p.setFont(titleFont);
    p.setPen(themeColor(ElaThemeType::BasicText));
    p.drawText(QRect(area.left(), top, area.width() - badgeWidth - 6, titleMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
               titleMetrics.elidedText(m_content.title, Qt::ElideRight, area.width() - badgeWidth - 6));
    top += titleMetrics.height() + 3;
    // Evidence summary and score.
    QFont smallFont = font(); smallFont.setPixelSize(11);
    const QFontMetrics smallMetrics(smallFont);
    p.setFont(smallFont);
    p.setPen(themeColor(ElaThemeType::BasicDetailsText));
    QString score;
    if (m_content.score >= 0) score = QString::number(m_content.score, 'f', 0);
    const int scoreWidth = score.isEmpty() ? 0 : smallMetrics.horizontalAdvance(score) + 6;
    p.drawText(QRect(area.left(), top, area.width() - scoreWidth, smallMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
               smallMetrics.elidedText(m_content.subtitle, Qt::ElideRight, area.width() - scoreWidth));
    if (!score.isEmpty()) p.drawText(QRect(area.right() - scoreWidth, top, scoreWidth, smallMetrics.height()), Qt::AlignRight | Qt::AlignVCenter, score);
    top += smallMetrics.height() + 3;
    // Preview of the text itself.
    QFont previewFont = font(); previewFont.setPixelSize(12);
    const QFontMetrics previewMetrics(previewFont);
    p.setFont(previewFont);
    p.setPen(themeColor(ElaThemeType::BasicText));
    p.drawText(QRect(area.left(), top, area.width(), previewMetrics.height()), Qt::AlignLeft | Qt::AlignVCenter,
               previewMetrics.elidedText(oneLine(m_content.text), Qt::ElideRight, area.width()));
    // Score bar: a quality index, not a probability.
    if (m_content.score >= 0) {
        const QRectF track(area.left(), area.bottom() - 2, area.width(), 3);
        p.setPen(Qt::NoPen);
        p.setBrush(themeColor(ElaThemeType::BasicBaseDeep));
        p.drawRoundedRect(track, 1.5, 1.5);
        p.setBrush(themeColor(ElaThemeType::PrimaryNormal));
        p.drawRoundedRect(QRectF(track.left(), track.top(), track.width() * std::clamp(m_content.score, 0.0, 100.0) / 100.0, 3), 1.5, 1.5);
    }
}
// ------------------------------------------------------------ preview
class EvidenceHighlighter : public QSyntaxHighlighter {
public:
    using QSyntaxHighlighter::QSyntaxHighlighter;
    QByteArray evidence;  // aligned with document positions
protected:
    void highlightBlock(const QString& text) override {
        if (evidence.isEmpty()) return;
        const int start = currentBlock().position();
        const bool dark = darkTheme();
        const auto at = [&](const int i) { return start + i < evidence.size() ? evidence[start + i] : static_cast<char>(CharEvidence::Exact); };
        for (int i = 0; i < text.size();) {
            const char e = at(i);
            int j = i + 1;
            while (j < text.size() && at(j) == e) ++j;
            if (e != static_cast<char>(CharEvidence::Exact)) {
                QTextCharFormat format;
                format.setBackground(EvidenceView::color(e, dark));
                setFormat(i, j - i, format);
            }
            i = j;
        }
    }
};
EvidenceView::EvidenceView(QWidget* parent) : ElaPlainTextEdit(parent), m_highlighter(new EvidenceHighlighter(document())) {
    setReadOnly(true);
    connect(eTheme, &ElaTheme::themeModeChanged, this, [this] { m_highlighter->rehighlight(); });
}
QColor EvidenceView::color(char evidence, const bool dark) {
    switch (static_cast<CharEvidence>(evidence)) {
    case CharEvidence::Partial: return dark ? QColor("#5C4B12") : QColor("#FFEDB0");
    case CharEvidence::Guided: return dark ? QColor("#1E3D66") : QColor("#D3E6FF");
    case CharEvidence::Unknown: return dark ? QColor("#5E2A2A") : QColor("#FFD3D3");
    default: return {};
    }
}
qsizetype EvidenceView::show(const QString& text, const QByteArray& evidence, const qsizetype limit) {
    // Display only: CR LF becomes one line break so evidence stays aligned with
    // document positions. Copying and saving use the candidate text itself.
    QString shown; QByteArray marks;
    shown.reserve(std::min(text.size(), limit)); if (!evidence.isEmpty()) marks.reserve(shown.capacity());
    for (qsizetype i = 0; i < text.size() && shown.size() < limit; ++i) {
        QChar c = text[i];
        if (c == '\r') { if (i + 1 < text.size() && text[i + 1] == '\n') continue; c = '\n'; }
        if (shown.size() + 1 == limit && c.isHighSurrogate()) break;
        shown += c;
        if (!evidence.isEmpty()) marks += i < evidence.size() ? evidence[i] : static_cast<char>(CharEvidence::Exact);
    }
    m_highlighter->evidence = marks;
    setPlainText(shown);
    return shown.size();
}
// ---------------------------------------------------------- workspace
RecoveryWorkspace::RecoveryWorkspace(const QString& prefix, const QString& previewName, QWidget* parent) : QWidget(parent) {
    auto* layout = new QHBoxLayout(this); layout->setContentsMargins(0, 0, 0, 0); layout->setSpacing(14);
    auto* column = new QVBoxLayout; column->setSpacing(8);
    m_listTitle = ui::label({}, 10, ui::Tone::Secondary, this);
    column->addWidget(m_listTitle);
    m_group = new QButtonGroup(this); m_group->setExclusive(true);
    for (int i = 0; i <= LocalSlots; ++i) {
        auto* card = new CandidateCard(this);
        card->setObjectName(i < LocalSlots ? QString("%1Candidate%2").arg(prefix).arg(i + 1) : prefix + "AiCandidate");
        card->setEnabled(false); card->hide();
        m_cards.append(card); m_group->addButton(card, i);
    }
    column->addWidget(m_cards[LocalSlots]);  // The AI result sits above the local ones.
    for (int i = 0; i < LocalSlots; ++i) column->addWidget(m_cards[i]);
    column->addStretch();
    auto* list = new QWidget(this); list->setLayout(column); list->setFixedWidth(270);
    layout->addWidget(list);
    auto* right = new QVBoxLayout; right->setSpacing(6);
    m_preview = new EvidenceView(this); m_preview->setObjectName(previewName); m_preview->setMinimumHeight(240);
    right->addWidget(m_preview, 1);
    auto* footer = new QHBoxLayout; footer->setSpacing(12);
    m_legend = ui::label({}, 9, ui::Tone::Secondary, this); m_legend->setTextFormat(Qt::RichText); m_legend->setWordWrap(false);
    m_stats = ui::label({}, 9, ui::Tone::Secondary, this); m_stats->setObjectName(prefix + "EvidenceStats"); m_stats->setTextFormat(Qt::PlainText);
    footer->addWidget(m_legend); footer->addWidget(m_stats, 1);
    right->addLayout(footer);
    layout->addLayout(right, 1);
    connect(m_group, &QButtonGroup::idClicked, this, [this](const int id) { select(id); });
    connect(eTheme, &ElaTheme::themeModeChanged, this, [this] { showSelected(); });
    retranslate();
}
ElaPushButton* RecoveryWorkspace::card(const int index) const { return index >= 0 && index < m_cards.size() ? m_cards[index] : nullptr; }
void RecoveryWorkspace::setPlaceholder(const QString& text) { m_preview->setPlaceholderText(text); }
void RecoveryWorkspace::setCandidates(const QList<WorkspaceCandidate>& local, const WorkspaceCandidate* ai) {
    m_local = local.mid(0, LocalSlots);
    m_hasAi = ai != nullptr;
    if (ai) m_ai = *ai; else m_ai = {};
    for (int i = 0; i <= LocalSlots; ++i) {
        const bool available = i < LocalSlots ? i < m_local.size() : m_hasAi;
        auto* card = m_cards[i];
        card->setEnabled(available); card->setVisible(available);
        if (available) card->setContent(i < LocalSlots ? m_local[i] : m_ai);
    }
    if (const bool valid = m_selected == LocalSlots ? m_hasAi : m_selected >= 0 && m_selected < m_local.size(); !valid) m_selected = m_local.isEmpty() ? (m_hasAi ? LocalSlots : -1) : 0;
    m_listTitle->setVisible(!m_local.isEmpty() || m_hasAi);
    showSelected();
}
void RecoveryWorkspace::select(const int index) {
    if (const bool valid = index == LocalSlots ? m_hasAi : index >= 0 && index < m_local.size(); !valid) return;
    const bool changed = index != m_selected;
    m_selected = index;
    showSelected();
    if (changed) emit selectionChanged(index);
}
void RecoveryWorkspace::showSelected() {
    m_group->setExclusive(false);
    for (int i = 0; i <= LocalSlots; ++i) m_cards[i]->setChecked(i == m_selected);
    m_group->setExclusive(true);
    const WorkspaceCandidate* current = m_selected == LocalSlots ? (m_hasAi ? &m_ai : nullptr)
                                      : m_selected >= 0 && m_selected < m_local.size() ? &m_local[m_selected] : nullptr;
    if (!current) { m_preview->show({}, {}, m_limit); m_shown = 0; m_lastShown = -1; m_legend->clear(); m_stats->clear(); return; }
    // Re-showing the same card (for example as AI chunks arrive) keeps the reading position.
    const bool same = m_selected == m_lastShown;
    const int scroll = m_preview->verticalScrollBar()->value();
    m_shown = m_preview->show(current->text, current->evidence, m_limit);
    if (same) m_preview->verticalScrollBar()->setValue(scroll);
    m_lastShown = m_selected;
    QStringList legend, stats;
    const auto& e = current->evidence;
    const bool dark = darkTheme();
    const auto chip = [&](CharEvidence kind, const QString& name) {
        if (const auto count = e.count(static_cast<char>(kind)); !count) return;
        legend << QString("<span style=\"background:%1\">&nbsp;&nbsp;&nbsp;</span>&nbsp;%2").arg(EvidenceView::color(static_cast<char>(kind), dark).name(), name.toHtmlEscaped());
    };
    if (!e.isEmpty()) {
        chip(CharEvidence::Partial, tr("guessed from one surviving byte"));
        chip(CharEvidence::Guided, tr("filled in by AI"));
        chip(CharEvidence::Unknown, tr("lost"));
        stats << tr("%1 certain, %2 guessed, %3 AI, %4 lost").arg(e.count(static_cast<char>(CharEvidence::Exact))).arg(e.count(static_cast<char>(CharEvidence::Partial)))
                     .arg(e.count(static_cast<char>(CharEvidence::Guided))).arg(e.count(static_cast<char>(CharEvidence::Unknown)));
    }
    if (m_shown < current->text.size()) stats << tr("Preview shows the first %1 characters.").arg(m_shown);
    m_legend->setText(legend.join("&nbsp;&nbsp;&nbsp;"));
    m_legend->setVisible(!legend.isEmpty());
    m_stats->setText(stats.join("  "));
}
void RecoveryWorkspace::retranslate() { m_listTitle->setText(tr("Candidates")); showSelected(); }
void RecoveryWorkspace::changeEvent(QEvent* event) {
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) retranslate();
}
