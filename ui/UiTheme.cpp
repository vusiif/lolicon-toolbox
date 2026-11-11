#include "UiTheme.h"
#include <ElaTheme.h>
#include <ElaPushButton.h>
#include <QApplication>
#include <QLabel>
#include <QFrame>
#include <QPalette>

namespace {
QColor color(const ElaThemeType::ThemeColor role) { return eTheme->getThemeColor(eTheme->getThemeMode(), role); }
void refreshLabel(QLabel* label)
{
    const auto tone = static_cast<ui::Tone>(label->property("tone").toInt());
    QColor foreground = color(ElaThemeType::BasicText);
    if (tone == ui::Tone::Secondary) foreground = color(ElaThemeType::BasicDetailsText);
    if (tone == ui::Tone::Danger) foreground = color(ElaThemeType::StatusDanger);
    if (tone == ui::Tone::Success) foreground = eTheme->getThemeMode() == ElaThemeType::Light ? QColor("#0F7B0F") : QColor("#6CCB5F");
    auto palette = label->palette();
    palette.setColor(QPalette::WindowText, foreground);
    label->setPalette(palette);
    label->update();
}
void applyPalette()
{
    auto palette = qApp->palette();
    palette.setColor(QPalette::Window, color(ElaThemeType::WindowBase));
    palette.setColor(QPalette::WindowText, color(ElaThemeType::BasicText));
    palette.setColor(QPalette::Base, color(ElaThemeType::BasicBase));
    palette.setColor(QPalette::Text, color(ElaThemeType::BasicText));
    palette.setColor(QPalette::Button, color(ElaThemeType::BasicBase));
    palette.setColor(QPalette::ButtonText, color(ElaThemeType::BasicText));
    palette.setColor(QPalette::Highlight, color(ElaThemeType::PrimaryNormal));
    palette.setColor(QPalette::HighlightedText, color(ElaThemeType::BasicTextInvert));
    palette.setColor(QPalette::PlaceholderText, color(ElaThemeType::BasicDetailsText));
    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText})
        palette.setColor(QPalette::Disabled, role, color(ElaThemeType::BasicTextDisable));
    qApp->setPalette(palette);
}
}
void ui::initializeTheme(const QObject* lifetime)
{
    applyPalette();
    QObject::connect(eTheme, &ElaTheme::themeModeChanged, lifetime, applyPalette);
}
QLabel* ui::label(const QString& text, const int size, const Tone tone, QWidget* parent)
{
    auto* result = new QLabel(text, parent);
    QFont font = result->font();
    font.setPointSize(size);
    result->setFont(font);
    result->setWordWrap(true);
    setTone(result, tone);
    QObject::connect(eTheme, &ElaTheme::themeModeChanged, result, [result] { refreshLabel(result); });
    return result;
}
void ui::setTone(QLabel* label, Tone tone) { label->setProperty("tone", static_cast<int>(tone)); refreshLabel(label); }
void ui::styleCard(QFrame* card)
{
    card->setObjectName("settingsCard");
    auto refresh = [card] {
        card->setStyleSheet(QString("QFrame#settingsCard { background: %1; border: 1px solid %2; border-radius: 8px; }")
            .arg(color(ElaThemeType::PopupBase).name(), color(ElaThemeType::BasicBorder).name()));
    };
    refresh();
    QObject::connect(eTheme, &ElaTheme::themeModeChanged, card, refresh);
}
void ui::primaryButton(ElaPushButton* button)
{
    auto refresh = [button] {
        button->setLightDefaultColor(ElaThemeColor(ElaThemeType::Light, PrimaryNormal));
        button->setLightHoverColor(ElaThemeColor(ElaThemeType::Light, PrimaryHover));
        button->setLightPressColor(ElaThemeColor(ElaThemeType::Light, PrimaryPress));
        button->setLightTextColor(ElaThemeColor(ElaThemeType::Light, BasicTextInvert));
        button->setDarkDefaultColor(ElaThemeColor(ElaThemeType::Dark, PrimaryNormal));
        button->setDarkHoverColor(ElaThemeColor(ElaThemeType::Dark, PrimaryHover));
        button->setDarkPressColor(ElaThemeColor(ElaThemeType::Dark, PrimaryPress));
        button->setDarkTextColor(ElaThemeColor(ElaThemeType::Dark, BasicTextInvert));
        button->update();
    };
    refresh();
    QObject::connect(eTheme, &ElaTheme::themeModeChanged, button, refresh);
}
