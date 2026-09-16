#include "Theme.h"
#include "ThemeManager.h"

QString theme::globalStyleSheet()
{
    return ThemeManager::instance().styleSheet();
}
