#include <QtTest>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QStringConverter>
#include "MojibakeAnalyzer.h"
#include "CandidateGenerator.h"
#include "CandidateScorer.h"
using namespace mojibake;
class TestMojibake : public QObject {
    Q_OBJECT
private slots:
    void converters();
    void qualityScores();
    void recoveryDataset();
    void preservesNormal();
    void damagedAndLimits();
    void mixedText();
    void damagedChineseParagraph();
    void performance();
};
void TestMojibake::converters() {
    qInfo() << "Qt native codecs:" << QStringConverter::availableCodecs();
    const auto available = EncodingConverter::supported();
    for (auto e : available) qInfo() << "Tool codec:" << EncodingConverter::name(e);
    QVERIFY(available.contains(Encoding::Utf8));
    QVERIFY(available.contains(Encoding::Latin1));
#ifdef Q_OS_WIN
    for (auto e : {Encoding::Windows1252, Encoding::Gbk, Encoding::Gb18030, Encoding::Big5, Encoding::ShiftJis, Encoding::EucKr})
        QVERIFY2(available.contains(e), qPrintable(EncodingConverter::name(e)));
#endif
    QCOMPARE(*EncodingConverter::encode("你好", Encoding::Utf8), QByteArray::fromHex("e4bda0e5a5bd"));
    QCOMPARE(*EncodingConverter::encode("你好", Encoding::Gbk), QByteArray::fromHex("c4e3bac3"));
    QCOMPARE(*EncodingConverter::encode("中文", Encoding::Big5), QByteArray::fromHex("a4a4a4e5"));
    QCOMPARE(*EncodingConverter::encode("こんにちは", Encoding::ShiftJis), QByteArray::fromHex("82b182f182c982bf82cd"));
    QCOMPARE(*EncodingConverter::encode("안녕", Encoding::EucKr), QByteArray::fromHex("bec8b3e7"));
    QVERIFY(!EncodingConverter::encode("你好", Encoding::Latin1));
    QVERIFY(!EncodingConverter::encode("😀", Encoding::Gbk));
    QVERIFY(!EncodingConverter::decode(QByteArray::fromHex("e4bd"), Encoding::Utf8));
    QVERIFY(!EncodingConverter::decode(QByteArray::fromHex("81"), Encoding::Gbk));
    QVERIFY(!EncodingConverter::decode(QByteArray::fromHex("c0af"), Encoding::Utf8));
    QVERIFY(!EncodingConverter::encode(QString(QChar(0xd800)), Encoding::Utf8));
    const QString binary = QString::fromUtf8("A\0B", 3);
    QCOMPARE(*EncodingConverter::decode(*EncodingConverter::encode(binary, Encoding::Utf8), Encoding::Utf8), binary);
    QCOMPARE(*EncodingConverter::decode(QByteArray(""), Encoding::Utf8), QString(""));
    QCOMPARE(*EncodingConverter::encode("😀", Encoding::Gb18030), QByteArray::fromHex("9439fc36"));
}
void TestMojibake::qualityScores() {
    const auto shortSample = QString("ä½ å¥½ ").repeated(10);
    const auto longSample = QString("ä½ å¥½ ").repeated(100);
    QCOMPARE(CandidateScorer::score({shortSample, {}}), CandidateScorer::score({longSample, {}}));
    Candidate complete{"你好世界", {{Encoding::Gbk, Encoding::Utf8}}};
    Candidate partial{"你好�世界", {{Encoding::Gbk, Encoding::Utf8, false, true}}};
    partial.reversible = false;
    QVERIFY(CandidateScorer::score(complete) > CandidateScorer::score(partial));
    const auto damaged = MojibakeAnalyzer::analyze("浣犲ソ�");
    for (const auto& c : damaged.candidates) {
        QVERIFY(c.score >= 0 && c.score <= 100);
    }
    QVERIFY(CandidateScorer::score({QString(1000, QChar(0xfffd)), {}}) >= 0);
}
void TestMojibake::recoveryDataset() {
    QFile file(QString(TEST_DATA_DIR) + "/mojibake_cases.json");
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto dataset = QJsonDocument::fromJson(file.readAll()).array();
    QVERIFY(dataset.size() >= 8);
    auto encoding = [](const QString& name) {
        for (auto e : EncodingConverter::supported()) if (EncodingConverter::name(e) == name) return e;
        return Encoding::Utf8;
    };
    for (const auto& value : dataset) {
        const auto item = value.toObject();
        const auto original = item["original"].toString();
        const auto source = encoding(item["source"].toString());
        const auto wrong = encoding(item["wrong"].toString());
        QString input = original;
        const int layers = item["layers"].toInt(1);
        for (int i = 0; i < layers; ++i) {
            const auto bytes = EncodingConverter::encode(input, source);
            QVERIFY2(bytes.has_value(), qPrintable(item["name"].toString()));
            const auto decoded = EncodingConverter::decode(*bytes, wrong);
            QVERIFY2(decoded.has_value(), qPrintable(item["name"].toString()));
            input = *decoded;
        }
        if (item.contains("mojibake")) QCOMPARE(input, item["mojibake"].toString());
        const auto result = MojibakeAnalyzer::analyze(input);
        qInfo().noquote() << item["name"].toString() << "input:" << input;
        for (const auto& c : result.candidates) qInfo().noquote() << c.score << c.text;
        QVERIFY(!result.candidates.isEmpty());
        QVERIFY2(std::any_of(result.candidates.begin(), result.candidates.end(), [&](const auto& c) { return c.text == original; }),
                 qPrintable("Missing candidate: " + item["name"].toString()));
        if (item["top"].toBool()) QCOMPARE(result.candidates.first().text, original);
        QSet<QString> seen;
        for (const auto& c : result.candidates) {
            QVERIFY(!seen.contains(c.text)); seen.insert(c.text);
            QVERIFY(c.path.size() <= 2);
            if (!c.reversible) continue;
            QString restored = c.text;
            for (auto it = c.path.crbegin(); it != c.path.crend(); ++it) {
                const auto inverse = CandidateGenerator::transform(restored, {it->decodeAs, it->encodeAs});
                QVERIFY(inverse.has_value()); restored = *inverse;
            }
            QCOMPARE(restored, input);
        }
    }
}
void TestMojibake::preservesNormal() {
    for (const auto& text : {QString("Hello World"), QString("你好世界"), QString("正常的中文文本。"),
                             QString("日本語 こんにちは"), QString("繁體中文測試"), QString("안녕하세요"),
                             QString("café naïve Straße São Paulo"), QString("😀🧮 ∑ π ± 中文 English\n\t")}) {
        const auto result = MojibakeAnalyzer::analyze(text);
        QVERIFY(!result.likelyMojibake);
        QCOMPARE(result.candidates.first().text, text);
    }
}
void TestMojibake::damagedAndLimits() {
    for (const auto& text : {QString("你好�"), QString("??"), QString("浣犲ソ�")}) {
        const auto result = MojibakeAnalyzer::analyze(text);
        QVERIFY(result.possibleLoss);
        QCOMPARE(result.confidence, Confidence::Unknown);
    }
    QVERIFY(MojibakeAnalyzer::analyze(QString(MojibakeAnalyzer::MaxInputBytes + 1, 'a')).tooLarge);
    auto cancelled = std::make_shared<std::atomic_bool>(true);
    QVERIFY(MojibakeAnalyzer::analyze("ä½ å¥½", cancelled).cancelled);
    QVERIFY(MojibakeAnalyzer::analyze("").candidates.isEmpty());
}
void TestMojibake::mixedText() {
    const auto result = MojibakeAnalyzer::analyze("正常前缀：ä½ å¥½\nEnglish");
    QCOMPARE(result.candidates.first().text, QString("正常前缀：你好\nEnglish"));
    QVERIFY(result.likelyMojibake);
}
void TestMojibake::performance() {
    for (int bytes : {32, 1024, 100 * 1024, 1024 * 1024}) {
        QElapsedTimer timer; timer.start();
        const auto result = MojibakeAnalyzer::analyze(QString(bytes, 'a'));
        QCOMPARE(result.candidates.first().text, QString(bytes, 'a'));
        QVERIFY(result.generated <= 100);
        qInfo() << "ASCII bytes" << bytes << "elapsed ms" << timer.elapsed();
    }
    QElapsedTimer timer; timer.start();
    const auto result = MojibakeAnalyzer::analyze(QString("ä½ å¥½ ").repeated(16000));
    QVERIFY(result.generated <= 100);
    qInfo() << "large mojibake elapsed ms" << timer.elapsed() << "candidates" << result.generated;
}
void TestMojibake::damagedChineseParagraph() {
    const QString input = "璇峰皬缁勮礋璐ｄ汉鍦ㄧ兢鍐呮眹鎶ュ綋鍓嶉」鐩繘搴︿互鍙婇亣鍒扮殑闂锛屾柟渚挎寚瀵艰�佸笀鎺屾彙椤圭洰鎯呭喌銆佸強鏃剁粰鍑烘寚瀵煎缓璁�傜鍚勪綅椤圭洰鎺ㄨ繘椤哄埄";
    const auto result = MojibakeAnalyzer::analyze(input);
    QVERIFY(!result.candidates.isEmpty());
    const auto& best = result.candidates.first();
    QCOMPARE(best.text, QString("请小组负责人在群内汇报当前项目进度以及遇到的问题，方便指导�师掌握项目情况、及时给出指导建议�祝各位项目推进顺利"));
    QVERIFY(best.text.contains(QChar(0xfffd)));
    QVERIFY(!best.reversible);
    QVERIFY(result.likelyMojibake);
    QVERIFY(result.possibleLoss);
    QCOMPARE(result.confidence, Confidence::Unknown);
    for (const QString& readable : {QString("请小组负责人在群内汇报当前项目进度。�"),
                                    QString("正常文本�仍然可读�不该被改成替换字符。")}) {
        const auto unchanged = MojibakeAnalyzer::analyze(readable);
        QCOMPARE(unchanged.candidates.first().text, readable);
        QVERIFY(!unchanged.likelyMojibake);
    }
}
QTEST_GUILESS_MAIN(TestMojibake)
#include "TestMojibake.moc"
