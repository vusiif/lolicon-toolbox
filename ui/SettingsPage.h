#pragma once

#include <QWidget>

class QComboBox;
class QPushButton;

class SettingsPage : public QWidget
{
    Q_OBJECT

public:
    explicit SettingsPage(QWidget* parent = nullptr);
    ~SettingsPage() override = default;

signals:
    void languageApplyRequested(const QString& locale);

private:
    void setupUi();

    QComboBox* m_themeCombo = nullptr;
    QComboBox* m_langCombo = nullptr;
    QPushButton* m_applyBtn = nullptr;
};
