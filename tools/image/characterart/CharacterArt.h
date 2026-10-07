#pragma once
#include <QImage>
#include <QRawFont>
#include <QString>
#include <QVector>
#include <QHash>
#include <atomic>
#include <memory>

namespace characterart {
inline constexpr qint64 MaxFileBytes = 32 * 1024 * 1024;
inline constexpr qint64 MaxSourcePixels = 16 * 1024 * 1024;
inline constexpr int MaxDimension = 8192;
inline constexpr int MaxColumns = 240;
inline constexpr int MaxRows = 300;
inline constexpr int Padding = 8;
struct Token { QString text; double density = 0; };
struct Palette { QString id; QList<Token> tokens; };
struct Maple {
    QRawFont font;
    QSize cell;
    int baseline = 0;
    QList<Palette> palettes;
    QHash<QString, QImage> tiles;
    QString error;
    bool valid() const { return error.isEmpty() && font.isValid() && !palettes.isEmpty(); }
};
struct Options { int columns = 80; double brightness = 0, contrast = 1; bool inverted = false; };
struct Result {
    QString text, error;
    int columns = 0, rows = 0;
    QList<QString> cells;
    bool success() const { return error.isEmpty() && columns > 0 && rows > 0; }
};
struct LoadedImage { QImage image; QString error; bool success() const { return error.isEmpty() && !image.isNull(); } };
// Must be created on the GUI thread. The raw font prevents system fallback.
Maple loadMaple(const QString& resource = QStringLiteral(":/characterart/MapleMonoNL-NFMono-CN-Regular.ttf"));
LoadedImage loadImage(const QString& path);
int rowCount(QSize image, int columns, QSize cell);
double adjustedLuminance(QRgb pixel, const Options& options);
Result generate(const QImage&, const Options&, const Palette&, QSize cell, const std::shared_ptr<std::atomic_bool>& cancel = {});
QImage render(const Result&, const Maple&);
bool saveNewFile(const QString& path, const QByteArray& bytes, QString& error);
}
