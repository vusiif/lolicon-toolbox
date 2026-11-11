#include <QTest>
#include <QFile>
#include <QJsonDocument>
#include <QJsonArray>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QTextBoundaryFinder>
#include <future>
#include "TextStyleEngine.h"
#include "TextStyleRestore.h"
using namespace textstyle;
namespace {
QByteArray demoBytes() { QFile f(QString(TEST_DATA_DIR) + "/style-demo.json"); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll(); }
QJsonObject demo() { return QJsonDocument::fromJson(demoBytes()).object(); }
Loaded load(const QJsonObject& data) { return Loader::parse(QJsonDocument(data).toJson(), "fixture"); }
QJsonObject rule(QString source, QString target, QString group = "core") {
    return {{"id", "r" + QString::number(source.size())}, {"kind", source.size() == 1 ? "character" : "phrase"}, {"group", group}, {"source", source}, {"candidates", QJsonArray{QJsonObject{{"text", target}, {"weight", 100}}}}};
}
std::shared_ptr<const Definition> fixture(QJsonArray rules, int rate = 10000, const bool decorate = false) {
    auto data = demo(); data["rule_groups"] = QJsonArray{"core"}; data["rules"] = rules;
    QJsonObject presets;
    for (const auto* id : {"light", "medium", "heavy"}) presets[id] = QJsonObject{{"rule_groups", QJsonArray{"core"}}, {"replacement_rate_bp", rate}, {"decorator_rate_bp", decorate ? 10000 : 0}, {"decorator_ids", decorate ? QJsonArray{"star"} : QJsonArray{}}};
    data["presets"] = presets;
    // Test helper assigns distinct IDs; expected output remains independently specified.
    for (int i = 0; i < rules.size(); ++i) { auto r = rules[i].toObject(); r["id"] = "r" + QString::number(i); rules[i] = r; } data["rules"] = rules;
    return load(data).style;
}
class TestTextStyle : public QObject {
    Q_OBJECT
private slots:
    void randomVectors();
    void seedAndSampling();
    void schemaValidation();
    void goldenDemo();
    void matchingUnicode();
    void limitsAndControl();
    void resourceAndPerformance();
    void restoration();
};
void TestTextStyle::randomVectors() {
    constexpr uint64_t expected[][3]{{0xe220a8397b1dcdafULL,0x6e789e6aa1b965f4ULL,0x06c45d188009454fULL},
        {0x910a2dec89025cc1ULL,0xbeeb8da1658eec67ULL,0xf893a2eefb32555eULL},
        {0xbdd732262feb6e95ULL,0x28efe333b266f103ULL,0x47526757130f9f52ULL},
        {0xe4d971771b652c20ULL,0xe99ff867dbf682c9ULL,0x382ff84cb27281e9ULL}};
    for (int i = 0; i < 4; ++i) { constexpr uint64_t seeds[]{0,1,42,UINT64_MAX}; Random random(seeds[i]); for (auto value : expected[i]) QCOMPARE(random.next(), value); QCOMPARE(random.calls(), static_cast<uint64_t>(3)); }
}
void TestTextStyle::seedAndSampling() {
    uint64_t value;
    for (const auto& text : {QString("0"), QString("18446744073709551615"), QString("00042"), QString(" 42 ")}) QVERIFY(Engine::seed(text, value));
    QVERIFY(Engine::seed("18446744073709551615", value)); QCOMPARE(value, UINT64_MAX);
    QVERIFY(Engine::seed("9007199254740993", value)); QCOMPARE(QString::number(value), QString("9007199254740993"));
    for (const auto& text : {QString(), QString("-1"), QString("+1"), QString("1e3"), QString("1.0"), QString("0xff"), QString("４２"), QString("18446744073709551616"), QString("\t42")}) QVERIFY(!Engine::seed(text, value));
    UserSeed named;
    QVERIFY(Engine::userSeed("CAT42", named)); QCOMPARE(named.text, QString("cat42"));
    QCOMPARE(named.mapping, QString("ascii-alnum-sha256be64-v1")); QCOMPARE(named.value, UINT64_C(0x983f1e82483d66e9));
    UserSeed lower; QVERIFY(Engine::userSeed("cat42", lower)); QCOMPARE(lower.value, named.value);
    QVERIFY(Engine::userSeed("00042", named)); QCOMPARE(named.text, QString("42")); QCOMPARE(named.value, static_cast<uint64_t>(42)); QCOMPARE(named.mapping, QString("decimal-u64-v1"));
    QVERIFY(Engine::userSeed("18446744073709551615", named)); QCOMPARE(named.value, UINT64_MAX);
    QVERIFY(Engine::userSeed(QString(64, 'a'), named));
    for (const auto& text : {QString(), QString("a_b"), QString("a b"), QString("中文"), QString("４２"), QString("é"), QString("-42"), QString("18446744073709551616"), QString(65,'a')}) QVERIFY(!Engine::userSeed(text, named));
    QList<uint64_t> raw{0,5,6}; int used = 0; QCOMPARE(Random::sample(10, [&] { return raw[used++]; }), static_cast<uint64_t>(6)); QCOMPARE(used, 3);
    Random random(0); QVERIFY(!random.gate(0)); QVERIFY(random.gate(10000)); QCOMPARE(random.choose({100}), 0); QCOMPARE(random.calls(), static_cast<uint64_t>(0));
    // First raw value for seed 0 gives 7535 modulo 10000.
    Random gateA(0), gateB(0); QVERIFY(!gateA.gate(7535)); QVERIFY(gateB.gate(7536));
    Random choiceA(0), choiceB(0); QCOMPARE(choiceA.choose({7535,2465}), 1); QCOMPARE(choiceB.choose({7536,2464}), 0);
}
void TestTextStyle::schemaValidation() {
    QVERIFY(load(demo()).style);
    auto bad = demo(); bad["surprise"] = true; QVERIFY(!load(bad).style);
    bad = demo(); bad.remove("rules"); QVERIFY(!load(bad).style);
    bad = demo(); bad["schema_version"] = 2; QCOMPARE(load(bad).error, Error::UnsupportedSchema);
    for (const auto& value : {QJsonValue("80"), QJsonValue(80.5), QJsonValue(-1), QJsonValue(0), QJsonValue(1000001), QJsonValue(QJsonValue::Null)}) {
        bad = demo(); auto rules = bad["rules"].toArray(); auto row = rules[0].toObject(); auto choices = row["candidates"].toArray(); auto c = choices[0].toObject(); c["weight"] = value; choices[0] = c; row["candidates"] = choices; rules[0] = row; bad["rules"] = rules; QVERIFY(!load(bad).style);
    }
    for (int mode = 0; mode < 9; ++mode) {
        bad = demo(); auto rules = bad["rules"].toArray(); auto r = rules[0].toObject(); auto choices = r["candidates"].toArray();
        if (mode == 0) r["source"] = "";
        if (mode == 1) r["source"] = "ab";
        if (mode == 2) r["kind"] = "phrase";
        if (mode == 3) r["group"] = "unknown";
        if (mode == 4) r["source"] = " a";
        if (mode == 5) choices = {};
        if (mode == 6) { auto c = choices[0].toObject(); c["text"] = r["source"]; choices[0] = c; }
        if (mode == 7) choices.append(choices[0]);
        if (mode == 8) rules.append(r);
        r["candidates"] = choices; rules[0] = r; bad["rules"] = rules; QVERIFY(!load(bad).style);
    }
    for (char32_t scalar : {static_cast<char32_t>(0),static_cast<char32_t>(9),static_cast<char32_t>(0x85),static_cast<char32_t>(0x2028),static_cast<char32_t>(0x202e),static_cast<char32_t>(0x200b),static_cast<char32_t>(0xfeff),static_cast<char32_t>(0xfdd0),static_cast<char32_t>(0x1ffff)}) {
        bad = demo(); auto m = bad["metadata"].toObject(); m["license"] = QString::fromUcs4(&scalar,1); bad["metadata"] = m; QVERIFY(!load(bad).style);
    }
    bad = demo(); auto presets = bad["presets"].toObject(); auto heavy = presets["heavy"].toObject(); heavy["decorator_ids"] = QJsonArray{"unknown"}; presets["heavy"] = heavy; bad["presets"] = presets; QVERIFY(!load(bad).style);
    QVERIFY(!Loader::parse(QByteArray::fromHex("efbbbf")+demoBytes(),"bad").style); QVERIFY(!Loader::parse(QByteArray("\xff",1),"bad").style);
    const auto raw = demoBytes(); QCOMPARE(Loader::parse(raw,"demo").style->sha256, QString::fromLatin1(QCryptographicHash::hash(raw,QCryptographicHash::Sha256).toHex()));
    QVERIFY(Loader::parse(raw+" ","demo").style->sha256 != Loader::parse(raw,"demo").style->sha256);
}
void TestTextStyle::goldenDemo() {
    const auto style = load(demo()).style; QVERIFY(style);
    struct Golden { const char* preset; uint64_t seed; QString output; int matched,replaced,decorated,calls; };
    const QList<Golden> cases{
        {.preset = "light", .seed = 0, .output = "我喜欢你\n你好\n\n为什么", .matched = 3, .replaced = 0, .decorated = 0, .calls = 3}, {.preset = "light", .seed = 42, .output = "我喜欢伱\n你好\n\n为什么", .matched = 3, .replaced = 1, .decorated = 0, .calls = 4},
        {.preset = "medium", .seed = 0, .output = "莪囍歡伱\n伱恏\n\n为什么", .matched = 5, .replaced = 5, .decorated = 0, .calls = 3}, {.preset = "medium", .seed = 42, .output = "莪囍歡鉨\n伱恏\n\n为什么", .matched = 5, .replaced = 5, .decorated = 0, .calls = 3},
        {.preset = "heavy", .seed = 0, .output = "★·°莪囍歡伱°·★\n☆伱恏☆\n\n★·°為蝦米°·★", .matched = 6, .replaced = 6, .decorated = 3, .calls = 6}, {.preset = "heavy", .seed = 42, .output = "☆莪囍歡鉨☆\n☆伱恏☆\n\n☆為蝦米☆", .matched = 6, .replaced = 6, .decorated = 3, .calls = 6}};
    for (const auto& [preset, seed, output, matched, replaced, decorated, calls] : cases) {
        const auto r = Engine::transform("我喜欢你\n你好\n\n为什么",style,preset,seed); QVERIFY(r.success()); QCOMPARE(r.output,output);
        QCOMPARE(r.stats.matched,static_cast<uint64_t>(matched)); QCOMPARE(r.stats.replaced,static_cast<uint64_t>(replaced));
        QCOMPARE(r.stats.decorated,static_cast<uint64_t>(decorated)); QCOMPARE(r.randomCalls,static_cast<uint64_t>(calls));
    }
    auto reordered = demo(); const auto rules = reordered["rules"].toArray(); QJsonArray reverseRules; for (int i = rules.size()-1; i >= 0; --i) reverseRules.append(rules[i]); reordered["rules"] = reverseRules;
    const auto decorators = reordered["decorators"].toArray(); QJsonArray reverseDecorators; for (int i = decorators.size()-1; i >= 0; --i) reverseDecorators.append(decorators[i]); reordered["decorators"] = reverseDecorators;
    QCOMPARE(Engine::transform("我喜欢你\n你好\n\n为什么",load(reordered).style,"heavy",42).output,cases.last().output);
    auto a = std::async(std::launch::async,[style] { return Engine::transform("我喜欢你\n你好\n\n为什么",style,"heavy",0).output; });
    auto b = std::async(std::launch::async,[style] { return Engine::transform("我喜欢你\n你好\n\n为什么",style,"heavy",42).output; });
    QCOMPARE(a.get(),cases[4].output); QCOMPARE(b.get(),cases[5].output);
}
void TestTextStyle::matchingUnicode() {
    const auto style = fixture({rule("你","伱"),rule("伱","鉨"),rule("我","莪"),rule("喜欢","稀飯"),rule("喜","囍"),rule("欢","歡"),rule("喜欢你","囍歡伱")}); QVERIFY(style);
    QCOMPARE(Engine::transform("你伱",style,"medium",0).output,QString("伱鉨"));
    QCOMPARE(Engine::transform("我喜欢你",style,"medium",0).output,QString("莪囍歡伱"));
    const QString unicode = "你́ é é 𠀀 👍🏽 🇨🇳 👨‍👩‍👧‍👦 未知🙂 <script>&\t�";
    QCOMPARE(Engine::transform(unicode,style,"medium",0).output,unicode);
    const auto failedGate = fixture({rule("喜欢","稀飯"),rule("喜","囍"),rule("欢","歡")},1);
    const auto failed = Engine::transform("喜欢",failedGate,"medium",0); QCOMPARE(failed.output,QString("喜欢")); QCOMPARE(failed.stats.matched,static_cast<uint64_t>(1)); QCOMPARE(failed.randomCalls,static_cast<uint64_t>(1));
    const auto decorated = fixture({rule("你","伱"),rule("好","恏")},10000,true);
    QCOMPARE(Engine::transform("  你好  \r\n\r世界\u2028\u2029",decorated,"heavy",0).output,QString("  ☆伱恏☆  \n\n☆世界☆\n\n"));
    QCOMPARE(Engine::transform("\t\u00a0\n  \n",decorated,"heavy",0).output,QString("\t\u00a0\n  \n"));
    const auto spaced = fixture({rule("a b","X")}); QCOMPARE(Engine::transform("a b\na\nb",spaced,"medium",0).output,QString("X\na\nb"));
    auto composed = rule("é","Z"); composed["kind"] = "character";
    auto emoji = rule("👍","Y"); emoji["kind"] = "character";
    const auto graphStyle = fixture({composed,emoji}); QVERIFY(graphStyle);
    QCOMPARE(Engine::transform("é é 👍🏽 👍",graphStyle,"medium",0).output,QString("Z é 👍🏽 Y"));
    QCOMPARE(Engine::transform(" ́你好 ",decorated,"heavy",0).output,QString("☆ ́伱恏☆ "));
    auto grouped = demo(); grouped["rule_groups"] = QJsonArray{"core","extended"};
    auto longRule = rule("喜欢你","LONG","extended"); longRule["id"] = "long";
    auto shortRule = rule("喜欢","SHORT"); shortRule["id"] = "short";
    auto youRule = rule("你","YOU"); youRule["id"] = "you";
    grouped["rules"] = QJsonArray{longRule,shortRule,youRule}; QJsonObject presets;
    for (const auto* id : {"light","medium","heavy"}) presets[id] = QJsonObject{{"rule_groups",QString(id)=="light" ? QJsonArray{"core"} : QJsonArray{"core","extended"}}, {"replacement_rate_bp",10000}, {"decorator_rate_bp",0}, {"decorator_ids",QJsonArray{}}};
    grouped["presets"] = presets; const auto filtered = load(grouped); QVERIFY(filtered.style);
    QCOMPARE(Engine::transform("喜欢你",filtered.style,"light",0).output,QString("SHORTYOU"));
    QCOMPARE(Engine::transform("喜欢你",filtered.style,"medium",0).output,QString("LONG"));
}
void TestTextStyle::limitsAndControl() {
    auto style = fixture({rule("a","abcdefgh")}); QVERIFY(style);
    const auto exact = Engine::transform(QString(Engine::MaxInputBytes,'a'),style,"medium",UINT64_MAX); QVERIFY(exact.success()); QCOMPARE(exact.stats.outputBytes,static_cast<uint64_t>(Engine::MaxOutputBytes)); QCOMPARE(exact.metadata["seed"].toString(),QString("18446744073709551615"));
    auto expanding = fixture({rule("a","abcdefghi")}); const auto tooBig = Engine::transform(QString(Engine::MaxInputBytes,'a'),expanding,"medium",0); QCOMPARE(tooBig.error,Error::OutputTooLarge); QVERIFY(tooBig.output.isEmpty()); QVERIFY(tooBig.metadata.isEmpty()); QCOMPARE(tooBig.stats.outputBytes,static_cast<uint64_t>(0));
    QCOMPARE(Engine::transform(QString(Engine::MaxInputBytes+1,'b'),style,"medium",0).error,Error::InputTooLarge);
    QCOMPARE(Engine::transform(QString(Engine::MaxInputUnits+1,'\r'),style,"medium",0).error,Error::InputTooLarge);
    QCOMPARE(Engine::transform(QString(QChar(0xd800)),style,"medium",0).error,Error::InvalidUnicodeInput);
    auto cancelled = std::make_shared<std::atomic_bool>(true); QCOMPARE(Engine::transform("a",style,"medium",0,Control{.cancelled = cancelled}).error,Error::Cancelled);
    int tick = 0; Control timeout{.cancelled = {}, .elapsed = [&] { return ++tick >= 8 ? 5000 : 0; }};
    const auto timed = Engine::transform("aaaaaa",style,"medium",0,timeout); QCOMPARE(timed.error,Error::Timeout); QVERIFY(timed.output.isEmpty()); QVERIFY(timed.metadata.isEmpty());
    QCOMPARE(Engine::transform("a",style,"invalid",0).error,Error::InvalidPreset);
    QCOMPARE(Engine::transform("a",{},"medium",0).error,Error::StyleUnavailable);
    int checks = 0; auto during = std::make_shared<std::atomic_bool>(false); Control midway{.cancelled = during, .elapsed = [&] { if (++checks==8) during->store(true); return 0; }};
    const auto stopped = Engine::transform("aaaaaaaa",style,"medium",0,midway); QCOMPARE(stopped.error,Error::Cancelled); QVERIFY(stopped.output.isEmpty());
}
void TestTextStyle::resourceAndPerformance() {
    QElapsedTimer clock; clock.start(); const auto loaded = Loader::builtin(); QVERIFY2(loaded.style,qPrintable(loaded.detail)); qInfo()<<"style cold load ms"<<clock.elapsed()<<"Qt"<<qVersion();
    int characters = 0, phrases = 0, multi = 0;
    for (const auto& rule : loaded.style->rules) { QTextBoundaryFinder boundaries(QTextBoundaryFinder::Grapheme,rule.source); int count = 0; while(boundaries.toNextBoundary()>=0) ++count; characters+=count==1; phrases+=count>1; multi+=rule.choices.size()>1; }
    QCOMPARE(loaded.style->version, QString("2.0.0")); QCOMPARE(characters,4791); QCOMPARE(phrases,38); QCOMPARE(multi,614);
    qInfo()<<"rules"<<loaded.style->rules.size()<<"characters"<<characters<<"phrases"<<phrases<<"multi"<<multi;
    auto candidates = [&](const QString& source) { QStringList texts; for (const auto& r : loaded.style->rules) if (r.source == source) for (const auto& c : r.choices) texts.append(c.text); return texts; };
    QVERIFY(candidates("你").contains("沵")); QVERIFY(candidates("你").contains("妳"));
    QVERIFY(candidates("一").contains("①")); QVERIFY(candidates("又").contains("叒")); QVERIFY(candidates("双").contains("叒"));
    QCOMPARE(Engine::transform("妑",loaded.style,"medium",42).output,QString("妑"));
    const QString chunk = "你好，我喜欢你。谢谢朋友，加油！ABC123 👍🏽 é\n";
    for (int target : {10*1024,256*1024}) {
        QString text; const auto bytes = chunk.toUtf8().size(); text = chunk.repeated(target/bytes); text += QString(target-text.toUtf8().size(),'x');
        clock.restart(); const auto result = Engine::transform(text,loaded.style,"heavy",42); QVERIFY(result.success()); qInfo()<<"mixed Chinese bytes"<<target<<"ms"<<clock.elapsed()<<"matches"<<result.stats.matched;
        const auto again = Engine::transform(text,loaded.style,"heavy",42); QCOMPARE(again.output.toUtf8(),result.output.toUtf8()); QCOMPARE(again.metadata,result.metadata);
    }
    auto denseRule = rule(QString(64,'a'),QString(128,'b')); QJsonArray denseChoices;
    for (int i = 0; i < 16; ++i) denseChoices.append(QJsonObject{{"text",QString(127,'b')+QChar('b'+i)}, {"weight",100}});
    denseRule["candidates"] = denseChoices; const auto stress = fixture({denseRule}); QVERIFY(stress);
    clock.restart(); const auto longest = Engine::transform(QString(256*1024,'a'),stress,"medium",42); QVERIFY(longest.success()); QCOMPARE(longest.stats.matched,static_cast<uint64_t>(4096)); qInfo()<<"64-EGC matching stress ms"<<clock.elapsed();
    QFile goldenFile(QString(TEST_DATA_DIR)+"/style-release-goldens.json"); QVERIFY(goldenFile.open(QIODevice::ReadOnly)); const auto golden = QJsonDocument::fromJson(goldenFile.readAll()).object();
    QCOMPARE(golden["style_sha256"].toString(),loaded.style->sha256);
    for (const auto& item : golden["cases"].toArray()) { const auto c = item.toObject(); const auto result = Engine::transform(golden["input"].toString(),loaded.style,c["preset"].toString(),42); QVERIFY(result.success()); QCOMPARE(result.output,c["output"].toString()); QCOMPARE(result.stats.matched,static_cast<uint64_t>(c["matched"].toInt())); QCOMPARE(result.stats.replaced,static_cast<uint64_t>(c["replaced"].toInt())); QCOMPARE(result.stats.decorated,static_cast<uint64_t>(c["decorated"].toInt())); QCOMPARE(result.randomCalls,static_cast<uint64_t>(c["random_calls"].toInt())); }
}
void TestTextStyle::restoration() {
    const auto style=fixture({rule("a","X"),rule("b","X"),rule("c","XY")},10000,true); QVERIFY(style);
    const auto result=Restore::run("\t☆XYX👍🏽\u00a0☆ \r\n",style);
    QVERIFY(result.success()); QCOMPARE(result.matched,static_cast<uint64_t>(2)); QCOMPARE(result.ambiguous,static_cast<uint64_t>(1)); QCOMPARE(result.wrappers,static_cast<uint64_t>(1));
    QCOMPARE(result.candidates.first(),QString("\tcX👍🏽\u00a0 \n")); QVERIFY(result.candidates.contains("\tca👍🏽\u00a0 \n")); QVERIFY(result.candidates.contains("\tcb👍🏽\u00a0 \n"));
    QVERIFY(result.candidates.contains("\t☆XYX👍🏽\u00a0☆ \n")); QCOMPARE(result.ambiguities.size(),1);
    QCOMPARE(Restore::run("X́",style).candidates.first(),QString("X́"));
    QCOMPARE(Restore::run("ABC😀 é\n",style).candidates.first(),QString("ABC😀 é\n"));
    QVERIFY(Restore::run("☆XY☆",style,false).candidates.first().contains("☆c☆"));
    QVERIFY(Restore::run("☆XY☆",style).candidates.size()<=5);
    QCOMPARE(Restore::run(QString(Engine::MaxInputBytes+1,'x'),style).error,Error::InputTooLarge);
    const auto cancelled=std::make_shared<std::atomic_bool>(true); const auto stopped=Restore::run("XY",style,true,Control{.cancelled = cancelled}); QCOMPARE(stopped.error,Error::Cancelled); QVERIFY(stopped.candidates.isEmpty());
    QCOMPARE(Restore::run("XY",style,true,Control{.cancelled = {}, .elapsed = []{return 5000;}}).error,Error::Timeout);
    QCOMPARE(Restore::run(QString(QChar(0xd800)),style).error,Error::InvalidUnicodeInput);
    const auto tooLarge=Restore::run(QString(64*1024,'X'),fixture({rule(QString(64,'a'),"X")}));
    QCOMPARE(tooLarge.error,Error::OutputTooLarge); QVERIFY(tooLarge.candidates.isEmpty()); QCOMPARE(tooLarge.matched,static_cast<uint64_t>(0));
    const auto builtin=Loader::builtin(); QVERIFY(builtin.style);
    QVERIFY(Restore::run("莪愛沵",builtin.style).candidates.contains("我爱你"));
    const auto ambiguous=Restore::run("妑",builtin.style); QVERIFY(ambiguous.ambiguous>0); QCOMPARE(ambiguous.candidates.first(),QString("妑"));
    QElapsedTimer elapsed; elapsed.start(); QVERIFY(Restore::run(QString(128*1024,'X'),style).success()); QVERIFY(elapsed.elapsed()<5000);
}
}  // namespace
QTEST_GUILESS_MAIN(TestTextStyle)
#include "TestTextStyle.moc"
