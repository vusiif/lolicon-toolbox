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
void TestAi::networkFailures() {
    for (int status : {401, 429, 500, 302, 200}) {
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
