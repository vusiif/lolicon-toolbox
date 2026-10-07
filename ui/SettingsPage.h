#pragma once
#include <QWidget>
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
    void retranslate();
    void synchronize();
    void fetchModels();
    void invalidateModels();
    void updateFetchButton();
    AppSettings& m_settings;
    QLabel* m_title;
    QLabel* m_languageLabel;
    QLabel* m_error;
    ElaComboBox* m_language;
    ElaLineEdit *m_apiUrl, *m_apiKey;
    ElaComboBox* m_apiModel;
    ElaPushButton* m_saveAi;
    ElaPushButton* m_fetchModels;
    ai::AiService* m_models;
    bool m_modelsStale = false;
    QLabel *m_aiTitle, *m_aiInfo, *m_apiUrlLabel, *m_apiModelLabel, *m_apiKeyLabel, *m_aiStatus;
};
