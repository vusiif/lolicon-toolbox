#pragma once
#include "tool/ToolPage.h"
#include "TextStyleEngine.h"
#include "TextStyleRestore.h"
#include "ai/AiProvider.h"
#include <QFutureWatcher>
#include <QElapsedTimer>
#include <QTimer>
#include <array>
namespace ai { class AiService; }
class QCheckBox;
class AppSettings;
class ElaPlainTextEdit;
class ElaLineEdit;
class ElaComboBox;
class ElaPushButton;
class ElaIconButton;
class QLabel;
class QBoxLayout;
class QGridLayout;
class TextStylePage : public ToolPage {
    Q_OBJECT
public:
    explicit TextStylePage(QWidget* parent = nullptr, AppSettings* settings = nullptr);
    ~TextStylePage() override;
    // Narrow execution seam for deterministic lifecycle tests; production uses Engine.
    using Runner = std::function<textstyle::Result(const QString&, std::shared_ptr<const textstyle::Definition>, const QString&, uint64_t, const textstyle::Control&)>;
    void setRunner(Runner runner) { m_runner = std::move(runner); }
    void setClipboardWriter(std::function<bool(const QString&)> writer) { m_clipboardWriter = std::move(writer); }
protected:
    void changeEvent(QEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
private:
    QString inputText() const;
    void invalidate();
    void run();
    void randomizeSeed();
    void refresh();
    void retranslate();
    void savePreferences();
    bool restoring() const;
    bool busy() const;
    void runAi();
    void showOutput();
    void refreshAi();
    AppSettings* m_settings;
    textstyle::Loaded m_loaded;
    textstyle::Result m_result;
    textstyle::Error m_error = textstyle::Error::None;
    QFutureWatcher<textstyle::Result> m_worker;
    QFutureWatcher<textstyle::Restoration> m_restoreWorker;
    textstyle::Restoration m_restored;
    int m_selectedRestore = 0;
    ai::AiService* m_aiService;
    ai::Reply m_aiReply;
    uint64_t m_aiRevision = UINT64_MAX;
    bool m_showAi = false;
    QString m_aiMessage;
    QElapsedTimer m_aiElapsed;
    QTimer m_aiTimer;
    int m_aiCharacters = 0;
    bool m_aiThinking = false;
    std::shared_ptr<std::atomic_bool> m_cancelToken;
    uint64_t m_revision = 0, m_submittedRevision = 0;
    bool m_success = false, m_copied = false;
    bool m_clipboardFailed = false;
    std::function<bool(const QString&)> m_clipboardWriter;
    Runner m_runner;
    ElaPlainTextEdit *m_input, *m_output;
    ElaComboBox *m_styles, *m_presets;
    ElaLineEdit* m_seed;
    ElaPushButton *m_convert, *m_randomConvert, *m_cancel, *m_clear, *m_copy, *m_copySeed;
    ElaIconButton* m_randomSeed;
    ElaPushButton *m_encodeMode, *m_restoreMode, *m_aiRun, *m_aiCancel, *m_localView, *m_aiView;
    std::array<ElaPushButton*,5> m_restoreChoices;
    QWidget *m_seedArea, *m_restoreArea;
    QCheckBox* m_removeWrappers;
    QLabel *m_aiHint, *m_aiStatus;
    QLabel *m_title, *m_description, *m_styleLabel, *m_presetLabel, *m_inputLabel, *m_outputLabel, *m_status, *m_seedLabel;
    QBoxLayout* m_columns = nullptr;
    QGridLayout* m_restoreChoiceLayout = nullptr;
};
