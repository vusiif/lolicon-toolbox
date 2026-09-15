#include "Theme.h"

QString theme::globalStyleSheet()
{
    return QStringLiteral(R"(
        * {
            font-family: "Segoe UI", "Microsoft YaHei UI", sans-serif;
        }

        QMainWindow {
            background-color: %1;
        }

        QWidget#centralWidget {
            background-color: %1;
        }
    )")
    .arg(WindowBg);
}