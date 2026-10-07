#include <QtTest>
#include <QSignalSpy>
#include <QtEndian>
#include "MiMoTtsService.h"
#include "TestHttpServer.h"
using namespace speech;
namespace {
QByteArray wav() {
    QByteArray bytes("RIFF\0\0\0\0WAVEfmt ", 16);
    const auto append32 = [&bytes](quint32 value) { char raw[4]; qToLittleEndian(value, raw); bytes.append(raw, 4); };
    const auto append16 = [&bytes](quint16 value) { char raw[2]; qToLittleEndian(value, raw); bytes.append(raw, 2); };
    append32(16); append16(1); append16(1); append32(24000); append32(48000); append16(2); append16(16);
    bytes.append("data", 4); append32(480); bytes.append(QByteArray(480, '\0'));
    qToLittleEndian<quint32>(bytes.size() - 8, bytes.data() + 4);
    return bytes;
}
QByteArray response(const QByteArray& audio = wav()) {
    return QJsonDocument(QJsonObject{{"choices", QJsonArray{QJsonObject{
        {"finish_reason", "stop"}, {"message", QJsonObject{{"audio", QJsonObject{{"data", QString::fromLatin1(audio.toBase64())}}}}}
    }}}}).toJson(QJsonDocument::Compact);
}
VoiceDesignRequest input(quint64 revision = 7) { return {QStringLiteral("低沉、温暖的成年男性声音。"), QStringLiteral("你好，欢迎来到工具箱。\nHello!"), revision}; }
ai::Configuration config(const TestHttpServer& server) { return {server.url(), "deliberately-not-the-voice-model", "test-only-key"}; }
}
class TestVoiceDesign : public QObject {
    Q_OBJECT
private slots:
    void requestContract() {
        const auto request = input();
        const auto body = QJsonDocument::fromJson(MiMoTtsService::requestBody(request)).object();
        QCOMPARE(body.value("model").toString(), MiMoTtsService::modelId());
        QCOMPARE(body.value("stream"), QJsonValue(false));
        const auto messages = body.value("messages").toArray();
        QCOMPARE(messages.size(), 2);
        QCOMPARE(messages[0].toObject().value("role").toString(), "user");
        QCOMPARE(messages[0].toObject().value("content").toString(), request.voiceDescription);
        QCOMPARE(messages[1].toObject().value("role").toString(), "assistant");
        QCOMPARE(messages[1].toObject().value("content").toString(), request.previewText);
        QCOMPARE(body.value("audio").toObject().value("format").toString(), "wav");
        QCOMPARE(body.value("audio").toObject().value("optimize_text_preview"), QJsonValue(false));
        QVERIFY(!body.contains("voice") && !body.contains("thinking") && !body.contains("response_format"));
        QCOMPARE(MiMoTtsService::endpoint(MiMoTtsService::defaultBaseUrl()).toString(), "https://api.xiaomimimo.com/v1/chat/completions");
        QCOMPARE(MiMoTtsService::endpoint("https://example.com/v1/chat/completions///").path(), "/v1/chat/completions");
    }
    void inputValidation() {
        TestHttpServer server;
        auto settings = config(server); auto request = input();
        QVERIFY(MiMoTtsService::validate(settings, request).isEmpty());
        settings.model.clear(); // Shared text-model selection must not affect this tool.
        QVERIFY(MiMoTtsService::validate(settings, request).isEmpty());
        for (const auto& url : {"http://example.com/v1", "https://key@example.com/v1", "https://example.com/v1?key=secret", "https://example.com/#key", "file:///test"}) {
            settings.baseUrl = url; QVERIFY(!MiMoTtsService::validate(settings, request).isEmpty());
        }
        settings = config(server); settings.apiKey = "secret\nheader";
        QVERIFY(!MiMoTtsService::validate(settings, request).isEmpty());
        settings.apiKey.clear(); QVERIFY(!MiMoTtsService::validate(settings, request).isEmpty());
        settings = config(server); request.voiceDescription = "  \n ";
        QVERIFY(!MiMoTtsService::validate(settings, request).isEmpty());
        request = input(); request.previewText.clear(); QVERIFY(!MiMoTtsService::validate(settings, request).isEmpty());
        request = input(); request.voiceDescription = QString(MiMoTtsService::MaxDescriptionBytes / 3 + 1, QChar(0x58f0));
        QVERIFY(!MiMoTtsService::validate(settings, request).isEmpty());
        request.voiceDescription = QString(MiMoTtsService::MaxDescriptionBytes, 'x');
        request.previewText = QString(MiMoTtsService::MaxPreviewBytes, 'y');
        QVERIFY(MiMoTtsService::validate(settings, request).isEmpty());
        request.previewText += 'y'; QVERIFY(!MiMoTtsService::validate(settings, request).isEmpty());
        MiMoTtsService service; QSignalSpy failed(&service, &MiMoTtsService::failed);
        QVERIFY(!service.generate(settings, request)); QCOMPARE(failed.size(), 1);
        QVERIFY(!service.isBusy()); QCOMPARE(server.requests, 0);
    }
    void audioValidation() {
        const auto audio = wav();
        QVERIFY(MiMoTtsService::validWav(audio));
        QCOMPARE(*MiMoTtsService::parseAudio(response(audio)), audio);
        for (int offset : {0, 4, 8, 16, 20, 22, 24, 28, 32, 34, 40}) {
            auto broken = audio; broken[offset] ^= char(0x7f);
            QVERIFY2(!MiMoTtsService::validWav(broken), qPrintable(QString::number(offset)));
        }
        QVERIFY(!MiMoTtsService::validWav(audio.first(audio.size() - 1)));
        QVERIFY(!MiMoTtsService::validWav(audio + 'x'));
        QVERIFY(!MiMoTtsService::validWav({}));
        auto empty = audio.first(44); qToLittleEndian<quint32>(36, empty.data() + 4); qToLittleEndian<quint32>(0, empty.data() + 40);
        QVERIFY(!MiMoTtsService::validWav(empty));
        auto padded = audio;
        QByteArray unknown("JUNK\1\0\0\0x\0", 10); padded.insert(12, unknown);
        qToLittleEndian<quint32>(padded.size() - 8, padded.data() + 4);
        QVERIFY(MiMoTtsService::validWav(padded)); // Ancillary chunks and odd-size padding are legitimate.
    }
    void malformedResponses() {
        const QList<QByteArray> invalid{
            {}, "not json", "[]", "{}", "{\"choices\":[]}", "{\"choices\":[{}]}",
            "{\"choices\":[{\"message\":{}}]}", "{\"choices\":[{\"message\":{\"audio\":{}}}]}",
            "{\"choices\":[{\"message\":{\"audio\":{\"data\":42}}}]}"
        };
        for (const auto& body : invalid) QVERIFY(!MiMoTtsService::parseAudio(body));
        for (const auto& encoded : {QString(), QStringLiteral("???="), QStringLiteral("AAAA"), QStringLiteral("汉字"), QString::fromLatin1(wav().toBase64()) + "\n"}) {
            auto object = QJsonDocument::fromJson(response()).object();
            const QJsonObject message{{"audio", QJsonObject{{"data", encoded}}}};
            object["choices"] = QJsonArray{QJsonObject{{"message", message}}};
            QVERIFY(!MiMoTtsService::parseAudio(QJsonDocument(object).toJson()));
        }
        for (const auto& reason : {"length", "content_filter", "error"}) {
            auto object = QJsonDocument::fromJson(response()).object(); auto choice = object["choices"].toArray()[0].toObject();
            choice["finish_reason"] = reason; object["choices"] = QJsonArray{choice};
            QVERIFY(!MiMoTtsService::parseAudio(QJsonDocument(object).toJson()));
        }
        auto object = QJsonDocument::fromJson(response()).object(); object["error"] = "private server content";
        QVERIFY(!MiMoTtsService::parseAudio(QJsonDocument(object).toJson()));
        QVERIFY(!MiMoTtsService::parseAudio(response(QByteArray("not a WAV"))));
    }
    void transportAndSnapshot() {
        TestHttpServer server; server.response = response();
        MiMoTtsService service; QSignalSpy done(&service, &MiMoTtsService::completed), failed(&service, &MiMoTtsService::failed);
        auto request = input(); QVERIFY(service.generate(config(server), request));
        request.voiceDescription = "edited after submit"; request.revision = 999;
        QVERIFY(!service.generate(config(server), request));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 3000);
        QCOMPARE(failed.size(), 0); QCOMPARE(server.requests, 1); QVERIFY(!service.isBusy());
        const auto result = qvariant_cast<VoiceDesignResult>(done[0][0]);
        QCOMPARE(result.audioData, wav()); QCOMPARE(result.snapshot.voiceDescription, input().voiceDescription);
        QCOMPARE(result.snapshot.previewText, input().previewText); QCOMPARE(result.snapshot.revision, quint64(7));
        QCOMPARE(result.modelId, MiMoTtsService::modelId()); QCOMPARE(result.mimeType, "audio/wav");
        QVERIFY(result.createdAt.isValid()); QCOMPARE(result.createdAt.timeSpec(), Qt::UTC);
        QVERIFY(server.lastHeaders.startsWith("POST /v1/chat/completions HTTP/1.1"));
        QVERIFY(server.lastHeaders.contains("Bearer test-only-key"));
        QCOMPARE(QJsonDocument::fromJson(server.lastBody), QJsonDocument::fromJson(MiMoTtsService::requestBody(input())));
    }
    void httpFailures_data() {
        QTest::addColumn<int>("status"); QTest::addColumn<VoiceDesignError>("code");
        QTest::newRow("401") << 401 << VoiceDesignError::Authorization;
        QTest::newRow("403") << 403 << VoiceDesignError::Authorization;
        QTest::newRow("429") << 429 << VoiceDesignError::RateLimit;
        QTest::newRow("400") << 400 << VoiceDesignError::Http;
        QTest::newRow("500") << 500 << VoiceDesignError::Http;
        QTest::newRow("redirect") << 302 << VoiceDesignError::Http;
    }
    void httpFailures() {
        QFETCH(int, status); QFETCH(VoiceDesignError, code);
        TestHttpServer destination, server; server.status = status;
        server.response = "secret-test-only-key submitted private text";
        server.extraHeaders = "Location: " + destination.url().toUtf8() + "\r\n";
        MiMoTtsService service; QSignalSpy failed(&service, &MiMoTtsService::failed), done(&service, &MiMoTtsService::completed);
        QVERIFY(service.generate(config(server), input()));
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 3000);
        QCOMPARE(qvariant_cast<VoiceDesignError>(failed[0][0]), code); QCOMPARE(failed[0][2].toULongLong(), quint64(7));
        QVERIFY(!failed[0][1].toString().contains("secret")); QVERIFY(!failed[0][1].toString().contains("private"));
        QCOMPARE(done.size(), 0); QCOMPARE(destination.requests, 0); QVERIFY(!service.isBusy());
    }
    void timeoutCancelRestart() {
        TestHttpServer held; held.hold = true;
        MiMoTtsService service(nullptr, 120);
        QSignalSpy failed(&service, &MiMoTtsService::failed), cancelled(&service, &MiMoTtsService::cancelled), done(&service, &MiMoTtsService::completed);
        QVERIFY(service.generate(config(held), input(10)));
        QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 3000);
        QCOMPARE(qvariant_cast<VoiceDesignError>(failed[0][0]), VoiceDesignError::Timeout);
        QCOMPARE(failed[0][2].toULongLong(), quint64(10));
        QVERIFY(service.generate(config(held), input(11)));
        QTRY_COMPARE_WITH_TIMEOUT(held.requests, 2, 3000);
        service.cancel(); service.cancel();
        QTRY_COMPARE(cancelled.size(), 1); QCOMPARE(cancelled[0][0].toULongLong(), quint64(11));
        TestHttpServer success; success.response = response();
        QVERIFY(service.generate(config(success), input(12)));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 3000);
        QTest::qWait(180); QCOMPARE(done.size(), 1); QCOMPARE(failed.size(), 1);
        QCOMPARE(qvariant_cast<VoiceDesignResult>(done[0][0]).snapshot.revision, quint64(12));
    }
    void cancelledLateResponse() {
        TestHttpServer old; old.response = response(); old.chunks.append(old.response.first(10));
        for (int i = 0; i < 20; ++i) old.chunks.append(QByteArray());
        old.chunks.append(old.response.mid(10));
        MiMoTtsService service; QSignalSpy done(&service, &MiMoTtsService::completed), cancelled(&service, &MiMoTtsService::cancelled);
        QVERIFY(service.generate(config(old), input(1)));
        QTRY_COMPARE(old.requests, 1); service.cancel();
        QTRY_COMPARE(cancelled.size(), 1);
        TestHttpServer next; next.response = response(); QVERIFY(service.generate(config(next), input(2)));
        QTRY_COMPARE(done.size(), 1); QTest::qWait(60);
        QCOMPARE(done.size(), 1); QCOMPARE(qvariant_cast<VoiceDesignResult>(done[0][0]).snapshot.revision, quint64(2));
    }
    void responseLimit() {
        TestHttpServer server; server.response = QByteArray(MiMoTtsService::MaxResponseBytes + 1, 'x');
        MiMoTtsService service; QSignalSpy failed(&service, &MiMoTtsService::failed);
        QVERIFY(service.generate(config(server), input())); QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 5000);
        QCOMPARE(qvariant_cast<VoiceDesignError>(failed[0][0]), VoiceDesignError::ResponseTooLarge);
    }
    void invalidAudioTransport() {
        TestHttpServer server; server.response = response("bad audio");
        MiMoTtsService service; QSignalSpy failed(&service, &MiMoTtsService::failed), done(&service, &MiMoTtsService::completed);
        QVERIFY(service.generate(config(server), input())); QTRY_COMPARE(failed.size(), 1);
        QCOMPARE(qvariant_cast<VoiceDesignError>(failed[0][0]), VoiceDesignError::InvalidResponse); QCOMPARE(done.size(), 0);
    }
    void incompleteResponseAndConnectionFailure() {
        TestHttpServer server; server.response = response(); server.chunks = {server.response.first(30)};
        MiMoTtsService service; QSignalSpy failed(&service, &MiMoTtsService::failed), done(&service, &MiMoTtsService::completed);
        QVERIFY(service.generate(config(server), input(30))); QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 1, 3000);
        QCOMPARE(qvariant_cast<VoiceDesignError>(failed[0][0]), VoiceDesignError::Network);
        const auto settings = config(server); server.close();
        // Windows may retry a refused TCP connection before reporting the error.
        QVERIFY(service.generate(settings, input(31))); QTRY_COMPARE_WITH_TIMEOUT(failed.size(), 2, 8000);
        QCOMPARE(qvariant_cast<VoiceDesignError>(failed[1][0]), VoiceDesignError::Network);
        QCOMPARE(failed[1][2].toULongLong(), quint64(31)); QCOMPARE(done.size(), 0);
    }
    void destructionDuringRequest() {
        TestHttpServer server; server.hold = true;
        auto service = std::make_unique<MiMoTtsService>();
        int terminalSignals = 0;
        connect(service.get(), &MiMoTtsService::cancelled, this, [&] { ++terminalSignals; });
        connect(service.get(), &MiMoTtsService::failed, this, [&] { ++terminalSignals; });
        QVERIFY(service->generate(config(server), input())); QTRY_COMPARE(server.requests, 1);
        service.reset(); QTest::qWait(50); QCOMPARE(terminalSignals, 0);
    }
};
QTEST_GUILESS_MAIN(TestVoiceDesign)
#include "TestVoiceDesign.moc"
