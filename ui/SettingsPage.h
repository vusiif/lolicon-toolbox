#pragma once
#include <QWidget>
#include "ai/AiProvider.h"
class AppSettings;
class ElaComboBox;
class QLabel;
class ElaLineEdit;
class ElaPushButton;
namespace ai { class AiService; }
class SettingsPage : public QWidget
{
    Q_OBJECT
public:
    explicit SettingsPage(AppSettings& settings, QWidget* parent = nullptr);
protected:
    void changeEvent(QEvent* event) override;
private:
    // One card per API: text AI tools and the Voice Designer audio API are configured separately.
    struct ApiForm {
        ElaLineEdit *url = nullptr, *key = nullptr;
        ElaComboBox* model = nullptr;
        ElaPushButton *save = nullptr, *fetch = nullptr;
        ai::AiService* models = nullptr;
        bool stale = false;
        QLabel *title = nullptr, *info = nullptr, *urlLabel = nullptr, *modelLabel = nullptr, *keyLabel = nullptr, *status = nullptr;
        ai::Configuration input() const;
    };
    enum class Api { Text, Voice };
    QWidget* buildApiForm(ApiForm& form, Api api, const char* prefix, const ai::Configuration& config);
    ApiForm& form(const Api api) { return api == Api::Text ? m_text : m_voice; }
    void retranslate();
    void synchronize();
    void fetchModels(Api api);
    void modelsListed(Api api, const QStringList& listed);
    void invalidateModels(Api api);
    void updateFetchButton(Api api);
    void saveApi(Api api);
    AppSettings& m_settings;
    QLabel* m_title;
    QLabel* m_languageLabel;
    QLabel* m_error;
    ElaComboBox* m_language;
    ApiForm m_text, m_voice;
    ai::Configuration m_voiceMirror; // Inherited values last shown in the unsaved voice form.
};
