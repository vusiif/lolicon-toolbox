#pragma once
#include <QWidget>
class AppSettings;
class ElaComboBox;
class QLabel;
class ElaLineEdit;
class ElaPushButton;
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
    AppSettings& m_settings;
    QLabel* m_title;
    QLabel* m_languageLabel;
    QLabel* m_error;
    ElaComboBox* m_language;
    ElaLineEdit *m_apiUrl, *m_apiModel, *m_apiKey;
    ElaPushButton* m_saveAi;
    QLabel *m_aiTitle, *m_aiInfo, *m_apiUrlLabel, *m_apiModelLabel, *m_apiKeyLabel, *m_aiStatus;
};
