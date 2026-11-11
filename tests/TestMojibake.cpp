#include <QTest>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QStringConverter>
#include "MojibakeAnalyzer.h"
#include "CandidateGenerator.h"
#include "CandidateScorer.h"
#include "ByteEncodingAnalyzer.h"
using namespace mojibake;
namespace {
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
    void commonCharacterScoring();
    void questionMarkLoss();
};
void TestMojibake::commonCharacterScoring() {
    // Generic UTF-8-as-GBK damage, not only phrases the scorer happens to know.
    const auto damage = [](const QString& text, const Encoding source, const Encoding wrong) {
        const auto bytes = EncodingConverter::encode(text, source);
        return bytes ? EncodingConverter::decode(*bytes, wrong) : std::nullopt;
    };
    for (const auto& text : {QString("测试成功"), QString("文件已保存到桌面"), QString("你好")}) {
        const auto input = damage(text, Encoding::Utf8, Encoding::Gbk);
        QVERIFY2(input, qPrintable(text)); // The UTF-8 bytes of these phrases are valid GBK.
        const auto result = MojibakeAnalyzer::analyze(*input);
        QVERIFY2(result.likelyMojibake, qPrintable(*input));
        QCOMPARE(result.candidates.first().text, text);
    }
    // Legacy bytes read as Latin-1: the native codec beats cross-codec noise such as 脛茫潞脙.
    const QList<std::pair<QString, Encoding>> legacy{{"你好", Encoding::Gbk}, {"中文測試", Encoding::Big5},
                                                    {"こんにちは", Encoding::ShiftJis}, {"파일을 저장했습니다.", Encoding::EucKr}};
    for (const auto& [text, codec] : legacy) {
        const auto input = damage(text, codec, Encoding::Latin1);
        QVERIFY2(input, qPrintable(text));
        QCOMPARE(MojibakeAnalyzer::analyze(*input).candidates.first().text, text);
    }
    QCOMPARE(CandidateScorer::rareCharacters("你好，世界"), 0);
    QVERIFY(CandidateScorer::rareCharacters(QString::fromUtf8("娴嬭瘯鎴愬姛")) >= 4); // 测试成功 read as GBK
    // A GBK file is reported as GBK, not as Latin-1 plus a repair to the same text.
    for (const auto& [text, codec] : QList<std::pair<QString, Encoding>>{{"你好，这是文件内容。", Encoding::Gbk}, {"中文測試，繁體字的文件內容。", Encoding::Big5}}) {
        const auto file = ByteEncodingAnalyzer::analyze(*EncodingConverter::encode(text, codec));
        QVERIFY(!file.candidates.isEmpty());
        QCOMPARE(file.candidates.first().text, text);
        QCOMPARE(file.candidates.first().encoding, codec);
        QVERIFY(file.candidates.first().repairPath.isEmpty());
    }
    QVERIFY(CandidateScorer::rareCharacters(QString::fromUtf8("ﾄ羲ﾃ")) >= 2);    // Halfwidth kana debris.
    // Rare but genuine names stay as typed.
    const auto names = MojibakeAnalyzer::analyze("张鑫和王淼都参加了这次比赛。");
    QVERIFY(!names.likelyMojibake); QCOMPARE(names.candidates.first().text, QString("张鑫和王淼都参加了这次比赛。"));
}
void TestMojibake::questionMarkLoss() {
    for (const auto& text : {QString("??"), QString("文件??.txt"), QString("用户???登录"), QString("?? ??")})
        QVERIFY2(MojibakeAnalyzer::lossyQuestionMarks(text), qPrintable(text));
    for (const auto& text : {QString("What??"), QString("真的吗??"), QString("Really?! Yes?? OK"), QString("单个?问号")})
        QVERIFY2(!MojibakeAnalyzer::lossyQuestionMarks(text), qPrintable(text));
}
void TestMojibake::converters() {
    qInfo() << "Qt native codecs:" << QStringConverter::availableCodecs();
    const auto available = EncodingConverter::supported();
    for (const auto e : available) qInfo() << "Tool codec:" << EncodingConverter::name(e);
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
    QCOMPARE(CandidateScorer::score({.text = shortSample, .path = {}}), CandidateScorer::score({.text = longSample, .path = {}}));
    const Candidate complete{.text = "你好世界", .path = {{.encodeAs = Encoding::Gbk, .decodeAs = Encoding::Utf8}}};
    Candidate partial{.text = "你好�世界", .path = {{.encodeAs = Encoding::Gbk, .decodeAs = Encoding::Utf8, .segmented = false, .partial = true}}};
    partial.reversible = false;
    QVERIFY(CandidateScorer::score(complete) > CandidateScorer::score(partial));
    for (const auto damaged = MojibakeAnalyzer::analyze("浣犲ソ�"); const auto& c : damaged.candidates) {
        QVERIFY(c.score >= 0 && c.score <= 100);
    }
    QVERIFY(CandidateScorer::score({.text = QString(1000, QChar(0xfffd)), .path = {}}) >= 0);
}
void TestMojibake::recoveryDataset() {
    QFile file(QString(TEST_DATA_DIR) + "/mojibake_cases.json");
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto dataset = QJsonDocument::fromJson(file.readAll()).array();
    QVERIFY(dataset.size() >= 8);
    auto encoding = [](const QString& name) {
        for (const auto e : EncodingConverter::supported()) if (EncodingConverter::name(e) == name) return e;
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
        QVERIFY2(std::ranges::any_of(result.candidates, [&](const auto& c) { return c.text == original; }),
                 qPrintable("Missing candidate: " + item["name"].toString()));
        if (item["top"].toBool()) QCOMPARE(result.candidates.first().text, original);
        QSet<QString> seen;
        for (const auto& c : result.candidates) {
            QVERIFY(!seen.contains(c.text)); seen.insert(c.text);
            QVERIFY(c.path.size() <= 2);
            if (!c.reversible) continue;
            QString restored = c.text;
            for (auto it = c.path.crbegin(); it != c.path.crend(); ++it) {
                const auto inverse = CandidateGenerator::transform(restored, {.encodeAs = it->decodeAs, .decodeAs = it->encodeAs});
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
    const auto cancelled = std::make_shared<std::atomic_bool>(true);
    QVERIFY(MojibakeAnalyzer::analyze("ä½ å¥½", cancelled).cancelled);
    QVERIFY(MojibakeAnalyzer::analyze("").candidates.isEmpty());
}
void TestMojibake::mixedText() {
    const auto result = MojibakeAnalyzer::analyze("正常前缀：ä½ å¥½\nEnglish");
    QCOMPARE(result.candidates.first().text, QString("正常前缀：你好\nEnglish"));
    QVERIFY(result.likelyMojibake);
}
void TestMojibake::performance() {
    for (const int bytes : {32, 1024, 100 * 1024, 1024 * 1024}) {
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
    // Common-character lookups run for every candidate; keep large CJK input bounded too.
    timer.restart();
    const auto chinese = MojibakeAnalyzer::analyze(QString("明天早上八点开会，记得带上电脑。").repeated(20000));
    QVERIFY(!chinese.likelyMojibake); QVERIFY(chinese.generated <= 100);
    qInfo() << "large Chinese elapsed ms" << timer.elapsed() << "candidates" << chinese.generated;
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
}  // namespace
QTEST_GUILESS_MAIN(TestMojibake)
#include "TestMojibake.moc"
