#pragma once

#include <QString>
#include <optional>

class Base64Codec
{
public:
    static QString encode(const QString& input);
    static std::optional<QString> decode(const QString& input);
};
