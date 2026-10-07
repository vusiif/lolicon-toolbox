#include <QtTest>
#include <QTemporaryDir>
#include <QBuffer>
#include <QImageReader>
#include "CharacterArt.h"
using namespace characterart;
class TestCharacterArt : public QObject {
    Q_OBJECT
private slots:
    void fontAndDensity() {
        QElapsedTimer timer; timer.start(); const auto maple = loadMaple();
        QVERIFY2(maple.valid(), qPrintable(maple.error));
        qInfo() << "Maple calibration ms" << timer.elapsed() << "cell" << maple.cell;
        QCOMPARE(maple.font.familyName(), "Maple Mono NL NFMono CN");
        const auto second = loadMaple();
        QCOMPARE(maple.cell, second.cell); QCOMPARE(maple.palettes.size(), 2);
        for (int p = 0; p < maple.palettes.size(); ++p) {
            const auto& palette = maple.palettes[p]; double previous = -1;
            QCOMPARE(palette.tokens.first().text, " "); QCOMPARE(palette.tokens.first().density, 0.0);
            for (int i = 0; i < palette.tokens.size(); ++i) {
                const auto& token = palette.tokens[i]; QVERIFY(token.density >= previous && token.density <= 1);
                QCOMPARE(token.density, second.palettes[p].tokens[i].density); previous = token.density;
                QVERIFY(maple.font.supportsCharacter(token.text[0])); QVERIFY(maple.tiles.contains(token.text));
            }
        }
        QVERIFY(!loadMaple(":/missing-font.ttf").valid());
    }
    void aspectAndMapping() {
        const auto maple = loadMaple(); QVERIFY(maple.valid());
        QCOMPARE(rowCount({100, 100}, 80, {10, 20}), 40);
        QCOMPARE(rowCount({200, 100}, 80, {10, 20}), 20);
        QCOMPARE(rowCount({100, 200}, 80, {10, 20}), 80);
        Options options; QCOMPARE(adjustedLuminance(qRgb(255,255,255), options), 1.0);
        QCOMPARE(adjustedLuminance(qRgba(0,0,0,0), options), 1.0);
        options.brightness = 1; QCOMPARE(adjustedLuminance(qRgb(0,0,0), options), 1.0);
        options = {}; options.inverted = true; QCOMPARE(adjustedLuminance(qRgb(255,255,255), options), 0.0);
        options = {}; options.columns = 40;
        QImage gradient(40, 40, QImage::Format_RGB32);
        for (int y=0;y<40;++y) for (int x=0;x<40;++x) gradient.setPixel(x,y,qRgb(x*255/39,x*255/39,x*255/39));
        const auto result = generate(gradient, options, maple.palettes[0], maple.cell);
        QVERIFY(result.success()); QCOMPARE(result.rows, rowCount(gradient.size(), 40, maple.cell));
        QCOMPARE(result.text.split('\n').size(), result.rows);
        for (const auto& row : result.text.split('\n')) QCOMPARE(row.size(), 40);
        QCOMPARE(result.cells.first(), maple.palettes[0].tokens.last().text); QCOMPARE(result.cells[39], " ");
        options.inverted = true; const auto inverse = generate(gradient, options, maple.palettes[0], maple.cell);
        QCOMPARE(inverse.cells.first(), " "); QCOMPARE(inverse.cells[39], maple.palettes[0].tokens.last().text);
        auto token = std::make_shared<std::atomic_bool>(true);
        const auto cancelled = generate(gradient, options, maple.palettes[0], maple.cell, token);
        QVERIFY(!cancelled.success()); QVERIFY(cancelled.cells.isEmpty());
        options.columns = 241; QVERIFY(!generate(gradient, options, maple.palettes[0], maple.cell).success());
        options.columns = 80; QImage tall(1,8192,QImage::Format_RGB32); QVERIFY(!generate(tall, options, maple.palettes[0], maple.cell).success());
    }
    void imagesAndExports() {
        QTemporaryDir dir; QImage source(100,100,QImage::Format_ARGB32); source.fill(Qt::transparent);
        for (const auto& format : {"png", "jpg"}) {
            const auto path = dir.filePath(QString("source.%1").arg(format)); QVERIFY(source.save(path, format));
            const auto loaded = loadImage(path); QVERIFY2(loaded.success(), qPrintable(loaded.error)); QCOMPARE(loaded.image.size(), source.size());
        }
        QVERIFY(!loadImage(dir.filePath("missing.png")).success());
        QFile oversized(dir.filePath("oversized.png")); QVERIFY(oversized.open(QIODevice::WriteOnly)); QVERIFY(oversized.resize(MaxFileBytes+1)); oversized.close();
        QVERIFY(!loadImage(oversized.fileName()).success());
        QImage pixelHeavy(4097,4096,QImage::Format_RGB32); pixelHeavy.fill(Qt::white);
        QVERIFY(pixelHeavy.save(dir.filePath("pixels.png"))); QVERIFY(!loadImage(dir.filePath("pixels.png")).success());
        QImage oriented(40,20,QImage::Format_RGB32); oriented.fill(Qt::gray);
        QByteArray jpeg; QBuffer jpegBuffer(&jpeg); QVERIFY(jpegBuffer.open(QIODevice::WriteOnly)); QVERIFY(oriented.save(&jpegBuffer,"JPEG")); jpegBuffer.close();
        // APP1 EXIF: little-endian TIFF, orientation 6 (90 degrees clockwise).
        const auto exif=QByteArray::fromHex("ffe1002245786966000049492a0008000000010012010300010000000600000000000000");
        jpeg.insert(2,exif); QString orientationError; const auto orientationPath=dir.filePath("orientation.jpg");
        QVERIFY(saveNewFile(orientationPath,jpeg,orientationError)); const auto transformed=loadImage(orientationPath);
        QVERIFY2(transformed.success(),qPrintable(transformed.error)); QCOMPARE(transformed.image.size(),QSize(20,40));
        QString error; const auto broken = dir.filePath("broken.png"); QVERIFY(saveNewFile(broken,"bad image",error)); QVERIFY(!loadImage(broken).success());
        QImage big(8193,1,QImage::Format_RGB32); big.fill(Qt::white); QVERIFY(big.save(dir.filePath("wide.png"))); QVERIFY(!loadImage(dir.filePath("wide.png")).success());
        const auto maple = loadMaple(); const auto result = generate(source, {}, maple.palettes[0], maple.cell);
        QVERIFY(result.success()); const auto output = render(result, maple); QVERIFY(!output.isNull());
        QCOMPARE(output.size(), QSize(result.columns*maple.cell.width()+2*Padding,result.rows*maple.cell.height()+2*Padding));
        QCOMPARE(output, render(result,maple));
        QByteArray png; QBuffer buffer(&png); QVERIFY(buffer.open(QIODevice::WriteOnly)); QVERIFY(output.save(&buffer,"PNG"));
        QCOMPARE(QImage::fromData(png).size(),output.size());
        const auto path = dir.filePath("art.txt"); QVERIFY(saveNewFile(path,result.text.toUtf8(),error));
        QVERIFY(!saveNewFile(path,"overwrite",error)); QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), result.text.toUtf8());
        QVERIFY(render(result, loadMaple(":/missing.ttf")).isNull());
        QElapsedTimer timer; timer.start(); QImage large(2048,2048,QImage::Format_RGB32); large.fill(Qt::gray);
        Options options; options.columns=240; const auto measured = generate(large,options,maple.palettes[1],maple.cell); QVERIFY(measured.success());
        const auto rendered = render(measured,maple); QVERIFY(!rendered.isNull()); qInfo()<<"2048 image + 240 columns generation/render ms"<<timer.elapsed();
    }
};
QTEST_MAIN(TestCharacterArt)
#include "TestCharacterArt.moc"
