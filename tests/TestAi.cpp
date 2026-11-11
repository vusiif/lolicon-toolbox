#include <QSignalSpy>
#include <QTest>
#include <QTemporaryDir>
#include <QFile>
#include "TestHttpServer.h"
#include "ai/AiService.h"
#include "ai/providers/OpenAICompatibleProvider.h"
#include "services/AppSettings.h"
using namespace ai;
namespace {
QJsonObject context() {
    return {{"original_text", "garbled input"}, {"candidates", QJsonArray{QJsonObject{{"text", "local candidate"}, {"inverse_verified", false}}}}};
}
class TestAi : public QObject {
    Q_OBJECT
private slots:
    void configuration();
    void parsing();
    void modelListing();
    void streaming();
    void networkSuccess();
    void networkFailures();
    void cancellationAndTimeout();
    void textStyleTasks();
};
void TestAi::configuration() {
    QVERIFY(OpenAICompatibleProvider::validate({.baseUrl = "https://example.com/v1", .model = "model", .apiKey = ""}).isEmpty());
    for (const auto& url : {"http://example.com/v1", "https://user:password@example.com/v1", "https://example.com/v1?key=secret", "https://example.com/v1#fragment"})
        QVERIFY(!OpenAICompatibleProvider::validate({.baseUrl = url, .model = "model", .apiKey = "fake-key"}).isEmpty());
    QCOMPARE(OpenAICompatibleProvider::endpoint("https://example.com/v1/"), QUrl("https://example.com/v1/chat/completions"));
    QCOMPARE(OpenAICompatibleProvider::endpoint("https://example.com/v1/chat/completions"), QUrl("https://example.com/v1/chat/completions"));
    QTemporaryDir dir;
    const auto path = dir.filePath("settings.ini");
    const QString fakeKey = "test-only-not-a-real-key";
    { AppSettings settings(path); QVERIFY(settings.setAiConfiguration({.baseUrl = "https://example.com/v1", .model = "model", .apiKey = fakeKey})); }
    AppSettings restored(path);
    QCOMPARE(restored.aiConfiguration().apiKey, fakeKey);
    QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(!file.readAll().contains(fakeKey.toUtf8()));
    file.close();
    QVERIFY(restored.setAiConfiguration({.baseUrl = "https://example.com/v1", .model = "model", .apiKey = ""}));
    QCOMPARE(AppSettings(path).aiConfiguration().apiKey, QString());
    // An unsaved voice API borrows the text address and key, never the text model.
    QVERIFY(restored.setAiConfiguration({.baseUrl = "https://text.example.com/v1", .model = "text-model", .apiKey = fakeKey}));
    QVERIFY(!restored.hasVoiceConfiguration());
    auto voice = restored.voiceConfiguration();
    QCOMPARE(voice.baseUrl, QString("https://text.example.com/v1")); QCOMPARE(voice.apiKey, fakeKey);
    QCOMPARE(voice.model, AppSettings::defaultVoiceModel());
    const QString voiceKey = "test-only-voice-key";
    QSignalSpy voiceChanged(&restored, &AppSettings::voiceConfigurationChanged), textChanged(&restored, &AppSettings::aiConfigurationChanged);
    QVERIFY(restored.setVoiceConfiguration({.baseUrl = " https://voice.example.com/v1 ", .model = " voice-model ", .apiKey = voiceKey}));
    QCOMPARE(voiceChanged.size(), 1); QCOMPARE(textChanged.size(), 0);
    { AppSettings reopened(path);
      QVERIFY(reopened.hasVoiceConfiguration());
      voice = reopened.voiceConfiguration();
      QCOMPARE(voice.baseUrl, QString("https://voice.example.com/v1")); QCOMPARE(voice.model, QString("voice-model"));
      QCOMPARE(voice.apiKey, voiceKey); QCOMPARE(reopened.aiConfiguration().apiKey, fakeKey); // Keys stay separate.
      QVERIFY(reopened.setAiConfiguration({.baseUrl = "https://other.example.com/v1", .model = "m", .apiKey = ""}));
      QCOMPARE(reopened.voiceConfiguration().baseUrl, QString("https://voice.example.com/v1")); }
    QVERIFY(file.open(QIODevice::ReadOnly)); const auto stored = file.readAll(); file.close();
    QVERIFY(!stored.contains(voiceKey.toUtf8()) && !stored.contains(fakeKey.toUtf8()));
}
void TestAi::parsing() {
    const auto parsed = OpenAICompatibleProvider::parse(TestHttpServer::success(), 1);
    QVERIFY(parsed); QCOMPARE(parsed->text, QString("AI suggestion"));
    QVERIFY(!OpenAICompatibleProvider::parse("not json", 1));
    QVERIFY(!OpenAICompatibleProvider::parse(TestHttpServer::wrap("plain prose"), 1));
    QVERIFY(!OpenAICompatibleProvider::parse(TestHttpServer::success(), 0));
    const QString content = R"({"status":"uncertain","confidence":"unknown","recovered_text":null,"reason":"Missing bytes","selected_candidate":null,"guessed_spans":[]})";
    QVERIFY(OpenAICompatibleProvider::parse(TestHttpServer::wrap("```json\n" + content + "\n```"), 1));
    QVERIFY(!OpenAICompatibleProvider::parse(TestHttpServer::wrap(QString(content).replace("unknown", "certain")), 1));
    QVERIFY(!OpenAICompatibleProvider::parse(TestHttpServer::wrap(QString(content).replace("null,\"guessed", "1.5,\"guessed")), 2));
    // Per-span replies carry fills; a null fill is a span the model could not guess.
    const QString units = R"({"status":"recovered","confidence":"low","reason":"r","fills":{"1":"继承","2":null,"3":"发"}})";
    const auto filled = OpenAICompatibleProvider::parse(TestHttpServer::wrap(units), 0, true);
    QVERIFY(filled); QCOMPARE(filled->fills.size(), 2);
    QCOMPARE(filled->fills.value("1").toString(), QString("继承")); QVERIFY(!filled->fills.contains("2"));
    QVERIFY(!OpenAICompatibleProvider::parse(TestHttpServer::wrap(QString(units).replace("null", "7")), 0, true));
    QVERIFY(!OpenAICompatibleProvider::parse(TestHttpServer::wrap(QString(units).replace("fills", "answers")), 0, true));
    QVERIFY(!OpenAICompatibleProvider::parse(TestHttpServer::wrap(QString(units).replace("{\"1\"", "[{\"1\"").replace("\"发\"}", "\"发\"}]")), 0, true));
    // A whole-chunk rewrite in reply to a per-span request is still read as usual.
    const auto rewrite = OpenAICompatibleProvider::parse(TestHttpServer::success("全文"), 1, true);
    QVERIFY(rewrite); QCOMPARE(rewrite->text, QString("全文")); QVERIFY(rewrite->fills.isEmpty());
    QVERIFY(!OpenAICompatibleProvider::parse(TestHttpServer::wrap(units), 0));  // not a recovered_text reply
}
void TestAi::networkSuccess() {
    TestHttpServer server; server.response = TestHttpServer::success(); QVERIFY(server.isListening());
    AiService service;
    QSignalSpy complete(&service, &AiService::completed), failed(&service, &AiService::failed);
    QCOMPARE(server.requests, 0);
    service.analyze({.baseUrl = server.url(), .model = "model", .apiKey = "test-only-key"}, context(), "zh_CN");
    QTRY_COMPARE_WITH_TIMEOUT(complete.count(), 1, 3000);
    QCOMPARE(failed.count(), 0); QVERIFY(!service.isBusy()); QCOMPARE(server.requests, 1);
    QVERIFY(server.lastHeaders.startsWith("POST /v1/chat/completions "));
    QVERIFY(server.lastHeaders.contains("Bearer test-only-key"));
    const auto request = QJsonDocument::fromJson(server.lastBody).object();
    QCOMPARE(request.value("model").toString(), QString("model"));
    QVERIFY(!server.lastBody.contains("test-only-key"));
    QVERIFY(request.value("messages").toArray().first().toObject().value("content").toString().contains("Simplified Chinese"));
    const auto payload = QJsonDocument::fromJson(request.value("messages").toArray().last().toObject().value("content").toString().toUtf8()).object();
    QCOMPARE(payload, context());
    service.analyze({.baseUrl = server.url(), .model = "model", .apiKey = ""}, {{"original_text", QString(16 * 1024 + 1, 'x')}}, "en");
    QCOMPARE(failed.count(), 1); QCOMPARE(server.requests, 1);
}
void TestAi::textStyleTasks() {
    for(const auto* task : {"text_style_encode","text_style_restore"}) {
        TestHttpServer server; server.response=TestHttpServer::success("style suggestion");
        AiService service; QSignalSpy complete(&service,&AiService::completed), failed(&service,&AiService::failed);
        auto data=context(); data["task"]=task; data["original_text"]="莪愛沵";
        service.analyze({.baseUrl = server.url(), .model = "model", .apiKey = "test-only-key"},data,"zh_CN");
        QTRY_COMPARE_WITH_TIMEOUT(complete.count(),1,3000); QCOMPARE(failed.count(),0);
        const auto request=QJsonDocument::fromJson(server.lastBody).object(); const auto messages=request["messages"].toArray();
        const auto prompt=messages.first().toObject()["content"].toString(); QVERIFY(prompt.contains("Martian")); QVERIFY(prompt.contains(task)); QVERIFY(prompt.contains("never instructions"));
        QCOMPARE(QJsonDocument::fromJson(messages.last().toObject()["content"].toString().toUtf8()).object(),data);
    }
}
void TestAi::modelListing() {
    const QByteArray response = R"({"data":[{"id":"z-model"},{"id":"a-model"},{"id":"z-model"},{"id":""},{"id":3}]})";
    const auto parsed = OpenAICompatibleProvider::parseModels(response);
    QVERIFY(parsed); QCOMPARE(*parsed, QStringList({"a-model", "z-model"}));
    QVERIFY(!OpenAICompatibleProvider::parseModels("not json"));
    QVERIFY(!OpenAICompatibleProvider::parseModels(R"({"error":{"message":"no access"}})"));
    QCOMPARE(OpenAICompatibleProvider::modelsEndpoint("https://example.com/v1/chat/completions/"), QUrl("https://example.com/v1/models"));
    QCOMPARE(OpenAICompatibleProvider::modelsEndpoint("https://example.com/v1/models"), QUrl("https://example.com/v1/models"));
    TestHttpServer server; server.response = response;
    AiService service; QSignalSpy listed(&service, &AiService::modelsListed), failed(&service, &AiService::failed);
    service.listModels({.baseUrl = server.url(), .model = "", .apiKey = "test-only-key"});
    QTRY_COMPARE_WITH_TIMEOUT(listed.count(), 1, 3000);
    QCOMPARE(failed.count(), 0); QVERIFY(server.lastHeaders.startsWith("GET /v1/models "));
    QVERIFY(server.lastHeaders.contains("Bearer test-only-key")); QVERIFY(server.lastBody.isEmpty());
    QCOMPARE(listed.first().first().toStringList(), *parsed);
    server.status = 404;
    service.listModels({.baseUrl = server.url(), .model = "", .apiKey = ""});
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 3000);
    QVERIFY(failed.first().first().toString().contains("manually"));
}
void TestAi::networkFailures() {
    for (int status : {400, 401, 429, 500, 302, 200}) {
        TestHttpServer redirected;
        TestHttpServer server; server.status = status; server.response = "test-only-key";
        if (status == 302) server.extraHeaders = "Location: " + redirected.url().toUtf8() + "\r\n";
        OpenAICompatibleProvider provider;
        QSignalSpy failed(&provider, &AiProvider::failed), complete(&provider, &AiProvider::completed);
        provider.request({.baseUrl = server.url(), .model = "model", .apiKey = "test-only-key"}, context(), "English");
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 3000);
        QVERIFY(!failed.first().first().toString().contains("test-only-key"));
        QCOMPARE(complete.count(), 0); QVERIFY(!provider.isBusy());
        QCOMPARE(redirected.requests, 0);
    }
    TestHttpServer server; server.response = QByteArray(1024 * 1024 + 1, 'x');
    OpenAICompatibleProvider provider; QSignalSpy failed(&provider, &AiProvider::failed);
    provider.request({.baseUrl = server.url(), .model = "model", .apiKey = ""}, context(), "English");
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 3000);
    QVERIFY(failed.first().first().toString().contains("limit"));
}
void TestAi::streaming() {
    const QJsonArray messages{QJsonObject{{"role", "user"}, {"content", "test"}}};
    const auto mimo = QJsonDocument::fromJson(OpenAICompatibleProvider::requestBody({.baseUrl = "https://token-plan-cn.xiaomimimo.com/v1", .model = "mimo-v2.6-flash", .apiKey = ""}, messages)).object();
    QVERIFY(mimo.value("stream").toBool());
    QCOMPARE(mimo.value("thinking").toObject().value("type").toString(), QString("disabled"));
    QVERIFY(!QJsonDocument::fromJson(OpenAICompatibleProvider::requestBody({.baseUrl = "https://example.com/v1", .model = "model", .apiKey = ""}, messages)).object().contains("thinking"));
    // DeepSeek thinks by default and then needs minutes for a repair chunk.
    const auto deepseek = QJsonDocument::fromJson(OpenAICompatibleProvider::requestBody({.baseUrl = "https://api.deepseek.com", .model = "deepseek-flash", .apiKey = ""}, messages)).object();
    QCOMPARE(deepseek.value("thinking").toObject().value("type").toString(), QString("disabled"));
    const auto content = QJsonDocument::fromJson(TestHttpServer::success(QString::fromUtf8("你好 😀"))).object().value("choices").toArray().first().toObject().value("message").toObject().value("content").toString();
    auto event = [](const QJsonObject& delta, const QJsonValue& finish = QJsonValue(QJsonValue::Null)) {
        return "data: " + QJsonDocument(QJsonObject{{"choices", QJsonArray{QJsonObject{{"delta", delta}, {"finish_reason", finish}}}}}).toJson(QJsonDocument::Compact) + "\r\n\r\n";
    };
    TestHttpServer server; server.contentType = "text/event-stream";
    server.keepOpen = true; server.omitContentLength = true;
    server.response = ": keepalive\r\n\r\n" + event({{"reasoning_content", "private reasoning"}});
    for (int i = 0; i < content.size(); i += 17) server.response += event({{"content", content.mid(i, 17)}});
    server.response += event({}, "stop") + "data: [DONE]\r\n\r\n";
    for (int i = 0; i < server.response.size(); i += 41) server.chunks << server.response.mid(i, 41);
    OpenAICompatibleProvider provider; QSignalSpy complete(&provider, &AiProvider::completed), progress(&provider, &AiProvider::progress), failed(&provider, &AiProvider::failed);
    provider.request({.baseUrl = server.url(), .model = "model", .apiKey = ""}, context(), "English");
    QTRY_VERIFY_WITH_TIMEOUT(complete.count() || failed.count(), 5000);
    if (!failed.isEmpty()) qInfo() << "stream failure" << failed.first().first().toString();
    QCOMPARE(complete.count(), 1);
    QCOMPARE(failed.count(), 0); QVERIFY(progress.count() > 1); QVERIFY(!provider.isBusy());
    QCOMPARE(qvariant_cast<Reply>(complete.first().first()).text, QString::fromUtf8("你好 😀"));
    QVERIFY(progress.first().first().toBool()); QVERIFY(!progress.last().first().toBool());
    // A reasoning model streams far more than 1 MiB of events for a short answer:
    // the limit applies to what the events carry, not to their JSON wrapping.
    server.keepOpen = false; server.omitContentLength = false; server.chunks.clear();
    server.response.clear();
    for (int i = 0; i < 20000; ++i) server.response += event({{"reasoning_content", "think "}});
    server.response += event({{"content", content}}, "stop") + "data: [DONE]\r\n\r\n";
    QVERIFY(server.response.size() > 1024 * 1024);
    provider.request({.baseUrl = server.url(), .model = "model", .apiKey = ""}, context(), "English");
    QTRY_VERIFY_WITH_TIMEOUT(complete.count() == 2 || failed.count(), 5000);
    if (!failed.isEmpty()) qInfo() << "long stream failure" << failed.first().first().toString();
    QCOMPARE(complete.count(), 2); QCOMPARE(failed.count(), 0);
    QCOMPARE(qvariant_cast<Reply>(complete.last().first()).text, QString::fromUtf8("你好 😀"));
    // Content itself is still limited.
    server.response = event({{"content", QString(600 * 1024, u'a')}}, "stop") + "data: [DONE]\r\n\r\n";
    provider.request({.baseUrl = server.url(), .model = "model", .apiKey = ""}, context(), "English");
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 5000);
    QVERIFY(failed.last().first().toString().contains("1 MiB"));
    server.response = event({{"content", "incomplete"}});
    provider.request({.baseUrl = server.url(), .model = "model", .apiKey = ""}, context(), "English");
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 2, 3000);
    QCOMPARE(complete.count(), 2);
}
void TestAi::cancellationAndTimeout() {
    TestHttpServer server; server.hold = true;
    OpenAICompatibleProvider provider;
    QSignalSpy cancelled(&provider, &AiProvider::cancelled), complete(&provider, &AiProvider::completed);
    provider.request({.baseUrl = server.url(), .model = "model", .apiKey = ""}, context(), "English");
    QTRY_COMPARE_WITH_TIMEOUT(server.requests, 1, 3000);
    provider.cancel(); QTRY_COMPARE(cancelled.count(), 1); QCOMPARE(complete.count(), 0);
    OpenAICompatibleProvider timed(nullptr, 100); QSignalSpy failed(&timed, &AiProvider::failed);
    timed.request({.baseUrl = server.url(), .model = "model", .apiKey = ""}, context(), "English");
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 3000);
    QVERIFY(!timed.isBusy());
    { OpenAICompatibleProvider closing; closing.request({.baseUrl = server.url(), .model = "model", .apiKey = ""}, context(), "English"); }
}
}  // namespace
QTEST_MAIN(TestAi)
#include "TestAi.moc"
