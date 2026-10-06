#pragma once
#include "ai/AiProvider.h"
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>
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
    static QString validate(const Configuration&);
    static QUrl endpoint(const QString& baseUrl);
    static std::optional<Reply> parse(const QByteArray&, int candidateCount);
private:
    void read();
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QTimer m_timeout;
    QByteArray m_body;
    QString m_abortReason;
    bool m_cancelled = false;
    int m_candidateCount = 0;
    int m_timeoutMs;
};
}
