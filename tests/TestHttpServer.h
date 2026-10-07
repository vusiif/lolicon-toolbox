#pragma once
#include <QTcpServer>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QRegularExpression>
#include <memory>
#include <QTimer>
// Loopback only. No live service, credentials, or model billing in tests.
class TestHttpServer : public QTcpServer {
public:
    int requests = 0;
    int status = 200;
    bool hold = false;
    bool keepOpen = false, omitContentLength = false;
    QByteArray contentType = "application/json";
    QList<QByteArray> chunks;
    QByteArray response, lastBody, lastHeaders, extraHeaders;
    explicit TestHttpServer(QObject* parent = nullptr) : QTcpServer(parent) {
        listen(QHostAddress::LocalHost);
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (hasPendingConnections()) {
                auto* socket = nextPendingConnection();
                auto buffer = std::make_shared<QByteArray>();
                auto handled = std::make_shared<bool>(false);
                connect(socket, &QTcpSocket::readyRead, this, [this, socket, buffer, handled] {
                    *buffer += socket->readAll();
                    const auto end = buffer->indexOf("\r\n\r\n");
                    if (*handled || end < 0) return;
                    const auto header = buffer->left(end);
                    const auto match = QRegularExpression("Content-Length: *(\\d+)", QRegularExpression::CaseInsensitiveOption).match(QString::fromLatin1(header));
                    const auto length = match.hasMatch() ? match.captured(1).toInt() : 0;
                    if (buffer->size() < end + 4 + length) return;
                    *handled = true; ++requests;
                    lastHeaders = header; lastBody = buffer->mid(end + 4, length);
                    if (hold) return;
                    socket->write("HTTP/1.1 " + QByteArray::number(status) + " Result\r\nContent-Type: " + contentType + "\r\n" +
                        (omitContentLength ? QByteArray() : "Content-Length: " + QByteArray::number(response.size()) + "\r\n") + extraHeaders + "Connection: close\r\n\r\n");
                    if (chunks.isEmpty()) { socket->write(response); if (!keepOpen) socket->disconnectFromHost(); }
                    else {
                        auto* timer = new QTimer(socket); timer->setTimerType(Qt::PreciseTimer);
                        auto index = std::make_shared<int>(0);
                        connect(timer, &QTimer::timeout, socket, [socket, timer, index, parts = chunks, keep = keepOpen] {
                            if (!socket->isOpen()) { timer->stop(); return; }
                            socket->write(parts[(*index)++]);
                            if (*index == parts.size()) { timer->stop(); if (!keep) socket->disconnectFromHost(); }
                        });
                        timer->start(10);
                    }
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }
    QString url() const { return QString("http://127.0.0.1:%1/v1").arg(serverPort()); }
    static QByteArray success(const QString& text = "AI suggestion") {
        const QJsonObject result{{"status", "recovered"}, {"confidence", "medium"}, {"recovered_text", text},
                                 {"reason", "Semantic suggestion; missing characters cannot be verified."},
                                 {"selected_candidate", 1}, {"guessed_spans", QJsonArray{}}};
        return wrap(QString::fromUtf8(QJsonDocument(result).toJson(QJsonDocument::Compact)));
    }
    static QByteArray wrap(const QString& content) {
        return QJsonDocument(QJsonObject{{"choices", QJsonArray{QJsonObject{{"finish_reason", "stop"}, {"message", QJsonObject{{"content", content}}}}}}}).toJson();
    }
};
