#pragma once

#include <QWidget>

class QComboBox;
class QLabel;

class SettingsPage : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsPage(QWidget* parent = nullptr);
    ~SettingsPage() override = default;

signals:
    void languageChanged(const QString& locale);

private:
    void setupUi();
    void refreshThemePreview();

    QComboBox* m_themeCombo = nullptr;
    QComboBox* m_langCombo = nullptr;
};
