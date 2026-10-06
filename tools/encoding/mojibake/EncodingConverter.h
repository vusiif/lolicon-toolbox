#pragma once
#include <QByteArray>
#include <QString>
#include <QList>
#include <optional>

namespace mojibake {
enum class Encoding { Utf8, Latin1, Windows1252, Gbk, Gb18030, Big5, ShiftJis, EucKr };
class EncodingConverter {
public:
    static QString name(Encoding encoding);
    static QList<Encoding> supported();
    // Success means exact round-trip: no replacement, best-fit or dropped bytes.
    static std::optional<QByteArray> encode(const QString& text, Encoding encoding);
    static std::optional<QString> decode(const QByteArray& bytes, Encoding encoding);
};
}
