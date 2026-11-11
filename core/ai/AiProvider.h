#pragma once
#include <QObject>
#include <QJsonObject>
#include <QStringList>
namespace ai {
// thinking: let a reasoning model think (MiMo and DeepSeek requests turn it off
// otherwise) and allow it ten minutes. Not stored; only the measurement tools set it.
struct Configuration { QString baseUrl; QString model; QString apiKey; bool thinking = false; };
struct Reply {
    QString text, reason, confidence, status;
    QStringList guesses;
    QJsonObject fills;  // lossy_gbk_units: unit id -> its characters (strings only)
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
    void progress(bool thinking, int characters);
};
}
Q_DECLARE_METATYPE(ai::Reply)
