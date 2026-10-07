#include <QtTest>
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
};
void TestAi::configuration() {
    QVERIFY(OpenAICompatibleProvider::validate({"https://example.com/v1", "model", ""}).isEmpty());
    for (const auto& url : {"http://example.com/v1", "https://user:password@example.com/v1", "https://example.com/v1?key=secret", "https://example.com/v1#fragment"})
        QVERIFY(!OpenAICompatibleProvider::validate({url, "model", "fake-key"}).isEmpty());
    QCOMPARE(OpenAICompatibleProvider::endpoint("https://example.com/v1/"), QUrl("https://example.com/v1/chat/completions"));
    QCOMPARE(OpenAICompatibleProvider::endpoint("https://example.com/v1/chat/completions"), QUrl("https://example.com/v1/chat/completions"));
    QTemporaryDir dir;
    const auto path = dir.filePath("settings.ini");
    const QString fakeKey = "test-only-not-a-real-key";
    { AppSettings settings(path); QVERIFY(settings.setAiConfiguration({"https://example.com/v1", "model", fakeKey})); }
    AppSettings restored(path);
    QCOMPARE(restored.aiConfiguration().apiKey, fakeKey);
    QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(!file.readAll().contains(fakeKey.toUtf8()));
    file.close();
    QVERIFY(restored.setAiConfiguration({"https://example.com/v1", "model", ""}));
    QCOMPARE(AppSettings(path).aiConfiguration().apiKey, QString());
}
void TestAi::parsing() {
    auto parsed = OpenAICompatibleProvider::parse(TestHttpServer::success(), 1);
    QVERIFY(parsed); QCOMPARE(parsed->text, QString("AI suggestion"));
    QVERIFY(!OpenAICompatibleProvider::parse("not json", 1));
    QVERIFY(!OpenAICompatibleProvider::parse(TestHttpServer::wrap("plain prose"), 1));
    QVERIFY(!OpenAICompatibleProvider::parse(TestHttpServer::success(), 0));
    const QString content = R"({"status":"uncertain","confidence":"unknown","recovered_text":null,"reason":"Missing bytes","selected_candidate":null,"guessed_spans":[]})";
    QVERIFY(OpenAICompatibleProvider::parse(TestHttpServer::wrap("```json\n" + content + "\n```"), 1));
    QVERIFY(!OpenAICompatibleProvider::parse(TestHttpServer::wrap(QString(content).replace("unknown", "certain")), 1));
    QVERIFY(!OpenAICompatibleProvider::parse(TestHttpServer::wrap(QString(content).replace("null,\"guessed", "1.5,\"guessed")), 2));
}
void TestAi::networkSuccess() {
    TestHttpServer server; server.response = TestHttpServer::success(); QVERIFY(server.isListening());
    AiService service;
    QSignalSpy complete(&service, &AiService::completed), failed(&service, &AiService::failed);
    QCOMPARE(server.requests, 0);
    service.analyze({server.url(), "model", "test-only-key"}, context(), "zh_CN");
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
    service.analyze({server.url(), "model", ""}, {{"original_text", QString(16 * 1024 + 1, 'x')}}, "en");
    QCOMPARE(failed.count(), 1); QCOMPARE(server.requests, 1);
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
    service.listModels({server.url(), "", "test-only-key"});
    QTRY_COMPARE_WITH_TIMEOUT(listed.count(), 1, 3000);
    QCOMPARE(failed.count(), 0); QVERIFY(server.lastHeaders.startsWith("GET /v1/models "));
    QVERIFY(server.lastHeaders.contains("Bearer test-only-key")); QVERIFY(server.lastBody.isEmpty());
    QCOMPARE(listed.first().first().toStringList(), *parsed);
    server.status = 404;
    service.listModels({server.url(), "", ""});
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
        provider.request({server.url(), "model", "test-only-key"}, context(), "English");
        QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 3000);
        QVERIFY(!failed.first().first().toString().contains("test-only-key"));
        QCOMPARE(complete.count(), 0); QVERIFY(!provider.isBusy());
        QCOMPARE(redirected.requests, 0);
    }
    TestHttpServer server; server.response = QByteArray(1024 * 1024 + 1, 'x');
    OpenAICompatibleProvider provider; QSignalSpy failed(&provider, &AiProvider::failed);
    provider.request({server.url(), "model", ""}, context(), "English");
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 3000);
    QVERIFY(failed.first().first().toString().contains("limit"));
}
void TestAi::streaming() {
    const QJsonArray messages{QJsonObject{{"role", "user"}, {"content", "test"}}};
    const auto mimo = QJsonDocument::fromJson(OpenAICompatibleProvider::requestBody({"https://token-plan-cn.xiaomimimo.com/v1", "mimo-v2.6-flash", ""}, messages)).object();
    QVERIFY(mimo.value("stream").toBool());
    QCOMPARE(mimo.value("thinking").toObject().value("type").toString(), QString("disabled"));
    QVERIFY(!QJsonDocument::fromJson(OpenAICompatibleProvider::requestBody({"https://example.com/v1", "model", ""}, messages)).object().contains("thinking"));
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
    provider.request({server.url(), "model", ""}, context(), "English");
    QTRY_VERIFY_WITH_TIMEOUT(complete.count() || failed.count(), 5000);
    if (!failed.isEmpty()) qInfo() << "stream failure" << failed.first().first().toString();
    QCOMPARE(complete.count(), 1);
    QCOMPARE(failed.count(), 0); QVERIFY(progress.count() > 1); QVERIFY(!provider.isBusy());
    QCOMPARE(qvariant_cast<Reply>(complete.first().first()).text, QString::fromUtf8("你好 😀"));
    QVERIFY(progress.first().first().toBool()); QVERIFY(!progress.last().first().toBool());
    server.keepOpen = false; server.omitContentLength = false; server.chunks.clear();
    server.response = event({{"content", "incomplete"}});
    provider.request({server.url(), "model", ""}, context(), "English");
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 3000);
    QCOMPARE(complete.count(), 1);
}
void TestAi::cancellationAndTimeout() {
    TestHttpServer server; server.hold = true;
    OpenAICompatibleProvider provider;
    QSignalSpy cancelled(&provider, &AiProvider::cancelled), complete(&provider, &AiProvider::completed);
    provider.request({server.url(), "model", ""}, context(), "English");
    QTRY_COMPARE_WITH_TIMEOUT(server.requests, 1, 3000);
    provider.cancel(); QTRY_COMPARE(cancelled.count(), 1); QCOMPARE(complete.count(), 0);
    OpenAICompatibleProvider timed(nullptr, 100); QSignalSpy failed(&timed, &AiProvider::failed);
    timed.request({server.url(), "model", ""}, context(), "English");
    QTRY_COMPARE_WITH_TIMEOUT(failed.count(), 1, 3000);
    QVERIFY(!timed.isBusy());
    { OpenAICompatibleProvider closing; closing.request({server.url(), "model", ""}, context(), "English"); }
}
QTEST_MAIN(TestAi)
#include "TestAi.moc"
