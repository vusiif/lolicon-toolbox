#pragma once
#include "AiProvider.h"
namespace ai {
class AiService : public QObject {
    Q_OBJECT
public:
    explicit AiService(QObject* parent = nullptr);
    bool isBusy() const;
    void analyze(const Configuration&, const QJsonObject&, const QString& locale);
    void cancel();
    static QString validate(const Configuration&);
    void listModels(const Configuration&);
signals:
    void completed(const ai::Reply&);
    void failed(const QString&);
    void cancelled();
    void modelsListed(const QStringList& models);
private:
    AiProvider* m_provider;
};
}
