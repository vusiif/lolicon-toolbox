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
signals:
    void completed(const ai::Reply&);
    void failed(const QString&);
    void cancelled();
private:
    AiProvider* m_provider;
};
}
