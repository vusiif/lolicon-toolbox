#pragma once
#include "ai/AiProvider.h"
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>
#include <QJsonArray>
#include <optional>
class QNetworkReply;
namespace ai {
class OpenAICompatibleProvider : public AiProvider {
    Q_OBJECT
public:
    explicit OpenAICompatibleProvider(QObject* parent = nullptr, int timeoutMs = 90000);
    ~OpenAICompatibleProvider() override;
    bool isBusy() const override { return !m_reply.isNull(); }
    void request(const Configuration&, const QJsonObject&, const QString& language) override;
    void cancel() override;
    void listModels(const Configuration&) override;
    static QString validate(const Configuration&);
    static QUrl endpoint(const QString& baseUrl);
    static QUrl modelsEndpoint(const QString& baseUrl);
    static std::optional<QStringList> parseModels(const QByteArray&);
    static std::optional<Reply> parse(const QByteArray&, int candidateCount);
    static QByteArray requestBody(const Configuration&, const QJsonArray& messages);
private:
    void read();
    void readEvents(const QByteArray& bytes);
    void send(const Configuration&, const QByteArray& body, bool models);
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QTimer m_timeout;
    QByteArray m_body;
    QString m_abortReason;
    bool m_cancelled = false;
    int m_candidateCount = 0;
    int m_timeoutMs;
    bool m_listingModels = false;
    QByteArray m_eventBuffer;
    QString m_streamText, m_finishReason;
    int m_reasoningCharacters = 0;
    bool m_streaming = false, m_streamDone = false;
};
}
