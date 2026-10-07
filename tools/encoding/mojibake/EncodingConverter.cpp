#include "EncodingConverter.h"
#include <QStringConverter>
namespace mojibake {
namespace {
QString codecName(Encoding e) {
    switch(e) {
    case Encoding::Latin1: return "ISO-8859-1";
    case Encoding::ShiftJis: return "Shift_JIS";
    default: return EncodingConverter::name(e);
    }
}
std::optional<QByteArray> rawEncode(const QString& text, Encoding e) {
    if (!text.isValidUtf16()) return {};
    QStringEncoder encoder(codecName(e), QStringConverter::Flag::Stateless);
    if (!encoder.isValid()) return {};
    QByteArray bytes = encoder(text);
    if (encoder.hasError()) return {};
    return bytes;
}
std::optional<QString> rawDecode(const QByteArray& bytes, Encoding e) {
    // File markers are removed by ByteEncodingAnalyzer; preserve literal U+FEFF here.
    QStringDecoder decoder(codecName(e), QStringConverter::Flag::Stateless | QStringConverter::Flag::ConvertInitialBom);
    if (!decoder.isValid()) return {};
    QString text = decoder(bytes);
    if (decoder.hasError() || !text.isValidUtf16()) return {};
    return text;
}
}
QString EncodingConverter::name(Encoding e) {
    switch(e) {
    case Encoding::Utf8: return "UTF-8";
    case Encoding::Latin1: return "Latin-1";
    case Encoding::Windows1252: return "Windows-1252";
    case Encoding::Gbk: return "GBK";
    case Encoding::Gb18030: return "GB18030";
    case Encoding::Big5: return "Big5";
    case Encoding::ShiftJis: return "Shift-JIS";
    case Encoding::EucKr: return "EUC-KR";
    case Encoding::Utf16LE: return "UTF-16LE";
    case Encoding::Utf16BE: return "UTF-16BE";
    case Encoding::Utf32LE: return "UTF-32LE";
    case Encoding::Utf32BE: return "UTF-32BE";
    }
    return {};
}
std::optional<QByteArray> EncodingConverter::encode(const QString& text, Encoding e) {
    auto bytes = rawEncode(text, e);
    if (!bytes) return {};
    const auto restored = rawDecode(*bytes, e);
    if (!restored || *restored != text) return {};
    return bytes;
}
std::optional<QString> EncodingConverter::decode(const QByteArray& bytes, Encoding e) {
    auto text = rawDecode(bytes, e);
    if (!text) return {};
    const auto restored = rawEncode(*text, e);
    if (!restored || *restored != bytes) return {};
    return text;
}
QList<Encoding> EncodingConverter::supported(bool includeWideUnicode) {
    QList<Encoding> result;
    for (auto e : {Encoding::Utf8, Encoding::Latin1, Encoding::Windows1252, Encoding::Gbk,
                   Encoding::Gb18030, Encoding::Big5, Encoding::ShiftJis, Encoding::EucKr})
        if (encode("ASCII", e)) result.append(e);
    if (includeWideUnicode)
        for (auto e : {Encoding::Utf16LE, Encoding::Utf16BE, Encoding::Utf32LE, Encoding::Utf32BE})
            if (encode("ASCII", e)) result.append(e);
    return result;
}
}
