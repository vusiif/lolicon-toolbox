#pragma once
#include <QObject>
#include <QJsonObject>
#include <QUrl>
#include <QStringList>
namespace ai {
struct Configuration { QString baseUrl; QString model; QString apiKey; };
struct Reply {
    QString text, reason, confidence, status;
    QStringList guesses;
    int selectedCandidate = -1;
};
class AiProvider : public QObject {
    Q_OBJECT
public:
    using QObject::QObject;
    virtual bool isBusy() const = 0;
    virtual void request(const Configuration&, const QJsonObject&, const QString& language) = 0;
    virtual void cancel() = 0;
    virtual void listModels(const Configuration&) = 0;
signals:
    void completed(const ai::Reply& reply);
    void failed(const QString& message);
    void cancelled();
    void modelsListed(const QStringList& models);
};
}
Q_DECLARE_METATYPE(ai::Reply)
