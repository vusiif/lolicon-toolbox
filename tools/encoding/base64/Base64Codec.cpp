#include "Base64Codec.h"

#include <QByteArray>

QString Base64Codec::encode(const QString& input)
{
    return input.toUtf8().toBase64();
}

std::optional<QString> Base64Codec::decode(const QString& input)
{
    if (input.isEmpty()) {
        return QString();
    }

    QByteArray result = QByteArray::fromBase64(input.toUtf8(), QByteArray::AbortOnBase64DecodingErrors);
    if (result.isEmpty()) {
        return std::nullopt;
    }
    return QString::fromUtf8(result);
}