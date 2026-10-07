#include "CharacterArt.h"
#include <QFile>
#include <QFileInfo>
#include <QGlyphRun>
#include <QImageReader>
#include <QPainter>
#include <QElapsedTimer>
#include <QFontDatabase>
#include <QTextBoundaryFinder>
#include <QTextLayout>
#include <QSet>
#include <algorithm>
#include <cmath>

static void initializeMapleResource() { Q_INIT_RESOURCE(maple); }
namespace characterart {
namespace {
void drawToken(QPainter& painter, const QRawFont& font, const QString& text, QPointF baseline) {
    const auto glyphs = font.glyphIndexesForString(text);
    const auto advances = font.advancesForGlyphIndexes(glyphs);
    QList<QPointF> positions; QPointF cursor;
    for (const auto& advance : advances) { positions.append(cursor); cursor += advance; }
    QGlyphRun run; run.setRawFont(font); run.setGlyphIndexes(glyphs); run.setPositions(positions);
    painter.drawGlyphRun(baseline, run);
}
}
Maple loadMaple(const QString& resource) {
    initializeMapleResource(); Maple maple;
    maple.font = QRawFont(resource, 20, QFont::PreferNoHinting);
    if (!maple.font.isValid() || maple.font.familyName() != "Maple Mono NL NFMono CN") {
        maple.error = QStringLiteral("The bundled Maple font could not be loaded. No fallback font is used."); return maple;
    }
    // The same application resource is used for shaping full grapheme clusters.
    static const int fontId=QFontDatabase::addApplicationFont(":/characterart/MapleMonoNL-NFMono-CN-Regular.ttf");
    if(fontId<0) { maple.error="The bundled Maple font could not be loaded. No fallback font is used."; return maple; }
    maple.shapingFont=QFont("Maple Mono NL NFMono CN"); maple.shapingFont.setPixelSize(20);
    maple.shapingFont.setHintingPreference(QFont::PreferNoHinting); maple.shapingFont.setStyleStrategy(QFont::NoFontMerging);
    const auto advance = maple.font.advancesForGlyphIndexes(maple.font.glyphIndexesForString("M"));
    if (advance.isEmpty()) { maple.error = "Invalid Maple font metrics."; return maple; }
    maple.cell = QSize(int(std::ceil(advance.first().x())), int(std::ceil(maple.font.ascent() + maple.font.descent() + maple.font.leading())));
    maple.baseline = int(std::ceil(maple.font.ascent()));
    if (maple.cell.width() <= 0 || maple.cell.height() <= 0) { maple.error = "Invalid Maple font metrics."; return maple; }
    const QList<QPair<QString, QString>> sets{{"basic", " .:-=+*#%@"}, {"dense", " !\"#$%&'()*+,-./0123456789:;<=>?@ABCDEFGHIJKLMNOPQRSTUVWXYZ[\\]^_`abcdefghijklmnopqrstuvwxyz{|}~"}};
    for (const auto& set : sets) {
        Palette palette; palette.id = set.first;
        for (const auto character : set.second) {
            if (!maple.font.supportsCharacter(character)) { maple.error = "The bundled Maple font does not support the built-in palette."; return maple; }
            const QString token(character);
            QImage image(maple.cell, QImage::Format_RGB32); image.fill(Qt::white);
            { QPainter painter(&image); painter.setRenderHint(QPainter::Antialiasing); painter.setRenderHint(QPainter::TextAntialiasing); painter.setPen(Qt::black); drawToken(painter, maple.font, token, QPointF(0, maple.baseline)); }
            double ink = 0;
            for (int y = 0; y < image.height(); ++y) for (int x = 0; x < image.width(); ++x) ink += 1.0 - qGray(image.pixel(x, y)) / 255.0;
            palette.tokens.append({token, ink / (image.width() * image.height())});
            maple.tiles.insert(token, image);
        }
        std::stable_sort(palette.tokens.begin(), palette.tokens.end(), [](const Token& a, const Token& b) {
            return a.density == b.density ? a.text < b.text : a.density < b.density;
        });
        maple.palettes.append(palette);
    }
    return maple;
}
CustomPalette calibrateCustom(const Maple& base, const QString& input, bool includeSpace) {
    CustomPalette result; result.maple=base; result.palette.id="custom";
    if(!base.valid()) { result.error=base.error; return result; }
    if(input.size()>2048) { result.error="Custom characters exceed the 2048 UTF-16 unit limit."; return result; }
    if(QString::fromUtf8(input.toUtf8())!=input) { result.error="Invalid Unicode in custom characters."; return result; }
    QList<QString> tokens; QSet<QString> seen;
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme,input); finder.toStart(); int start=0;
    for(int end=finder.toNextBoundary();end>=0;end=finder.toNextBoundary()) {
        const auto token=input.mid(start,end-start); start=end;
        if(seen.contains(token)) continue;
        seen.insert(token); tokens.append(token);
    }
    if(includeSpace && !seen.contains(" ")) tokens.append(" ");
    if(tokens.size()<2 || tokens.size()>128) { result.error="Use 2 to 128 distinct grapheme clusters, including the optional space."; return result; }
    QList<double> advances; double maxAdvance=base.cell.width();
    for(const auto& token:tokens) {
        bool supported=token.size()<=32;
        for(const auto codepoint:token.toUcs4()) {
            if(QChar::isSpace(codepoint) && codepoint!=' ') supported=false;
            if(!base.font.supportsCharacter(codepoint)) supported=false;
        }
        QTextLayout layout(token,base.shapingFont); layout.beginLayout(); const auto line=layout.createLine(); layout.endLayout();
        if(!line.isValid()) supported=false;
        const double advance=line.isValid() ? line.horizontalAdvance() : 0;
        for(const auto& run:layout.glyphRuns()) {
            if(run.rawFont().fontTable("head")!=base.font.fontTable("head")) supported=false;
            for(const auto glyph:run.glyphIndexes()) if(!glyph) supported=false;
        }
        if(advance<=0 || std::abs(advance/base.cell.width()-std::round(advance/base.cell.width()))>.05) supported=false;
        if(!supported) result.unsupported.append(token);
        advances.append(advance); maxAdvance=std::max(maxAdvance,advance);
    }
    if(!result.unsupported.isEmpty()) return result;
    result.maple.cell.setWidth(int(std::ceil(maxAdvance/base.cell.width()))*base.cell.width());
    result.maple.tiles.clear();
    for(qsizetype i=0;i<tokens.size();++i) {
        QImage image(result.maple.cell,QImage::Format_RGB32); image.fill(Qt::white);
        QTextLayout layout(tokens[i],base.shapingFont); layout.beginLayout(); const auto line=layout.createLine(); layout.endLayout();
        { QPainter painter(&image); painter.setPen(Qt::black); painter.setRenderHint(QPainter::TextAntialiasing); layout.draw(&painter,QPointF(0,base.baseline-line.ascent())); }
        double ink=0;
        for(int y=0;y<image.height();++y) for(int x=0;x<image.width();++x) ink+=1-qGray(image.pixel(x,y))/255.0;
        const int spaces=std::max(0,int(std::lround((result.maple.cell.width()-advances[i])/base.cell.width())));
        result.palette.tokens.append({tokens[i],ink/(image.width()*image.height()),QString(spaces,' ')});
        result.maple.tiles.insert(tokens[i],image);
    }
    std::stable_sort(result.palette.tokens.begin(),result.palette.tokens.end(),[](const Token& a,const Token& b) { return a.density==b.density ? a.text<b.text : a.density<b.density; });
    return result;
}
LoadedImage loadImage(const QString& path) {
    const QFileInfo info(path);
    if (!info.isFile() || info.size() <= 0 || info.size() > MaxFileBytes) return {{}, "Select an existing PNG or JPEG file up to 32 MiB."};
    QImageReader reader(path); reader.setAutoTransform(true);
    const auto format = reader.format().toLower();
    if (format != "png" && format != "jpeg" && format != "jpg") return {{}, "Only static PNG and JPEG images are supported."};
    const auto size = reader.size();
    if (!size.isValid() || size.width() > MaxDimension || size.height() > MaxDimension || qint64(size.width()) * size.height() > MaxSourcePixels)
        return {{}, "Image exceeds the 8192-pixel dimension or 16-megapixel limit."};
    // Decode under an explicit budget; autoTransform applies EXIF orientation.
    const auto image = reader.read();
    if (image.isNull()) return {{}, "Image decoding failed. The file may be damaged."};
    if (qint64(image.width()) * image.height() > MaxSourcePixels) return {{}, "Decoded image exceeds the pixel limit."};
    return {image, {}};
}
int rowCount(QSize image, int columns, QSize cell) {
    if (image.width() <= 0 || image.height() <= 0 || columns <= 0 || cell.width() <= 0 || cell.height() <= 0) return 0;
    const double rows = double(image.height()) / image.width() * columns * cell.width() / cell.height();
    if (rows > MaxRows + 0.5) return MaxRows + 1;
    return std::max(1, int(std::lround(rows)));
}
double adjustedLuminance(QRgb pixel, const Options& options) {
    // Alpha is composited on white; transparent pixels do not become black ink.
    const double alpha = qAlpha(pixel) / 255.0;
    double light = (0.2126 * qRed(pixel) + 0.7152 * qGreen(pixel) + 0.0722 * qBlue(pixel)) / 255.0;
    light = light * alpha + 1 - alpha;
    light = (light + options.brightness - 0.5) * options.contrast + 0.5;
    if (options.inverted) light = 1 - light;
    return std::clamp(light, 0.0, 1.0);
}
Result generate(const QImage& image, const Options& options, const Palette& palette, QSize cell, const std::shared_ptr<std::atomic_bool>& cancel) {
    const auto failure = [](const QString& error) { Result result; result.error = error; return result; };
    if (image.isNull() || image.width() > MaxDimension || image.height() > MaxDimension || qint64(image.width()) * image.height() > MaxSourcePixels) return failure("Invalid or oversized image.");
    if (options.columns < 10 || options.columns > MaxColumns || !std::isfinite(options.brightness) || !std::isfinite(options.contrast) ||
        options.brightness < -1 || options.brightness > 1 || options.contrast < 0 || options.contrast > 3 || palette.tokens.size() < 2)
        return failure("Invalid generation options or palette.");
    for (qsizetype i = 0; i < palette.tokens.size(); ++i) {
        const auto& token = palette.tokens[i];
        if (token.text.isEmpty() || !std::isfinite(token.density) || token.density < 0 || token.density > 1 || (i && token.density < palette.tokens[i - 1].density)) return failure("Invalid glyph palette.");
    }
    const auto rows = rowCount(image.size(), options.columns, cell);
    if (rows <= 0 || rows > MaxRows) return failure("The image is too tall at this column count. Reduce the number of columns.");
    if (cancel && cancel->load()) return failure("Generation cancelled.");
    QElapsedTimer timer; timer.start();
    // Smooth sampling after white compositing avoids fringes around transparent edges.
    QImage opaque(image.size(), QImage::Format_RGB32); opaque.fill(Qt::white);
    { QPainter painter(&opaque); painter.drawImage(0, 0, image); }
    const auto sampled = opaque.scaled(options.columns, rows, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    Result result; result.columns = options.columns; result.rows = rows; result.cells.reserve(rows * options.columns);
    for (int y = 0; y < rows; ++y) {
        if (cancel && cancel->load()) return failure("Generation cancelled.");
        if (timer.elapsed() > 5000) return failure("Generation timed out.");
        if (y) result.text += '\n';
        for (int x = 0; x < options.columns; ++x) {
            const auto index = qsizetype(std::lround((1 - adjustedLuminance(sampled.pixel(x, y), options)) * (palette.tokens.size() - 1)));
            const auto& token = palette.tokens[index];
            result.cells.append(token.text); result.text += token.text + token.padding;
        }
    }
    return result;
}
QImage render(const Result& result, const Maple& maple) {
    if (!maple.valid() || !result.success() || result.columns > MaxColumns || result.rows > MaxRows || result.cells.size() != result.columns * result.rows) return {};
    const QSize size(result.columns * maple.cell.width() + 2 * Padding, result.rows * maple.cell.height() + 2 * Padding);
    if (qint64(size.width()) * size.height() > 32 * 1024 * 1024) return {};
    QImage image(size, QImage::Format_RGB32); image.fill(Qt::white);
    QPainter painter(&image);
    for (int y = 0; y < result.rows; ++y) for (int x = 0; x < result.columns; ++x) {
        const auto tile = maple.tiles.constFind(result.cells[y * result.columns + x]);
        if (tile == maple.tiles.cend()) return {};
        painter.drawImage(Padding + x * maple.cell.width(), Padding + y * maple.cell.height(), *tile);
    }
    return image;
}
bool saveNewFile(const QString& path, const QByteArray& bytes, QString& error) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) { error = "Could not create a new output file. Choose a path that does not already exist."; return false; }
    if (file.write(bytes) != bytes.size() || !file.flush()) { file.close(); file.remove(); error = "Could not write the output file."; return false; }
    return true;
}
}
