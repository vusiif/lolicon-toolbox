#include "Base64Codec.h"

#include <QByteArray>

QString Base64Codec::encode(const QString& input)
{
    return input.toUtf8().toBase64();
}

std::optional<QString> Base64Codec::decode(const QString& input)
{
    QByteArray result = QByteArray::fromBase64(input.toUtf8());
    if (result.isEmpty() && !input.isEmpty()) {
        return std::nullopt;
    }
    return QString::fromUtf8(result);
}
