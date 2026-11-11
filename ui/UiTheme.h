#pragma once
#include <QString>
class QObject;
class QWidget;
class QLabel;
class QFrame;
class ElaPushButton;
namespace ui {
enum class Tone { Primary, Secondary, Success, Danger };
void initializeTheme(const QObject* lifetime);
QLabel* label(const QString& text, int pointSize, Tone tone = Tone::Primary, QWidget* parent = nullptr);
void setTone(QLabel* label, Tone tone);
void styleCard(QFrame* card);
void primaryButton(ElaPushButton* button);
}
