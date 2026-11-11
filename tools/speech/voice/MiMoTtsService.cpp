#include "MiMoTtsService.h"
#include <QHostAddress>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QtEndian>
#include <algorithm>

namespace speech {
QString MiMoTtsService::defaultBaseUrl() { return QStringLiteral("https://api.xiaomimimo.com/v1"); }
MiMoTtsService::MiMoTtsService(QObject* parent, const int timeoutMs)
    : QObject(parent), m_timeoutMs(std::max(1, timeoutMs)) {
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        if (m_reply) { m_abortReason = VoiceDesignError::Timeout; m_reply->abort(); }
    });
}
MiMoTtsService::~MiMoTtsService() {
    if (m_reply) { static_cast<void>(m_reply->disconnect(this)); m_reply->abort(); }
}
QString MiMoTtsService::validate(const ai::Configuration& config, const VoiceDesignRequest& input) {
    const QUrl url(config.baseUrl.trimmed(), QUrl::StrictMode);
    if (const bool local = url.host() == "localhost" || QHostAddress(url.host()).isLoopback(); !url.isValid() || url.host().isEmpty() || !url.userInfo().isEmpty() || url.hasQuery() || url.hasFragment() ||
        (url.scheme() != "https" && !(url.scheme() == "http" && local)))
        return tr("Use an HTTPS API address. HTTP is allowed only for localhost. Credentials, query strings and fragments are not allowed in the address.");
    if (config.apiKey.trimmed().isEmpty() || config.apiKey.size() > 8192 ||
        std::ranges::any_of(config.apiKey, [](const QChar c) { return c.category() == QChar::Other_Control; }))
        return tr("Enter a valid API key in Settings.");
    if (config.model.trimmed().isEmpty() || config.model.size() > 256 ||
        std::ranges::any_of(config.model, [](const QChar c) { return c.isSpace() || c.category() == QChar::Other_Control; }))
        return tr("Choose a voice design model in Settings.");
    if (input.voiceDescription.size() > MaxDescriptionBytes || input.voiceDescription.toUtf8().size() > MaxDescriptionBytes)
        return tr("Voice description exceeds the 8 KiB UTF-8 limit.");
    if (input.previewText.size() > MaxPreviewBytes || input.previewText.toUtf8().size() > MaxPreviewBytes)
        return tr("Preview text exceeds the 16 KiB UTF-8 limit.");
    if (input.voiceDescription.trimmed().isEmpty() || input.previewText.trimmed().isEmpty())
        return tr("Enter both a voice description and preview text.");
    return {};
}
QUrl MiMoTtsService::endpoint(const QString& baseUrl) {
    QUrl url(baseUrl.trimmed());
    auto path = url.path();
    while (path.endsWith('/')) path.chop(1);
    if (!path.endsWith("/chat/completions")) path += "/chat/completions";
    url.setPath(path);
    return url;
}
QByteArray MiMoTtsService::requestBody(const VoiceDesignRequest& input, const QString& model) {
    return QJsonDocument(QJsonObject{
        {"model", model.trimmed()}, {"stream", false},
        {"messages", QJsonArray{
            QJsonObject{{"role", "user"}, {"content", input.voiceDescription}},
            QJsonObject{{"role", "assistant"}, {"content", input.previewText}}}},
        {"audio", QJsonObject{{"format", "wav"}, {"optimize_text_preview", false}}}
    }).toJson(QJsonDocument::Compact);
}
std::optional<WavInfo> MiMoTtsService::wavInfo(const QByteArray& audio) {
    if (audio.size() < 44 || audio.size() > MaxAudioBytes || audio.first(4) != "RIFF" || audio.mid(8, 4) != "WAVE") return {};
    const auto u32 = [&audio](const qsizetype offset) { return qFromLittleEndian<quint32>(audio.constData() + offset); };
    const auto u16 = [&audio](const qsizetype offset) { return qFromLittleEndian<quint16>(audio.constData() + offset); };
    if (static_cast<quint64>(u32(4)) + 8 != static_cast<quint64>(audio.size())) return {};
    bool formatFound = false, dataFound = false;
    quint16 blockAlign = 0;
    WavInfo info;
    qsizetype offset = 12;
    while (offset < audio.size()) {
        if (audio.size() - offset < 8) return {};
        const auto length = u32(offset + 4);
        const quint64 next = static_cast<quint64>(offset) + 8 + length + (length & 1);
        if (next > static_cast<quint64>(audio.size())) return {};
        if (const auto tag = audio.mid(offset, 4); tag == "fmt ") {
            if (formatFound || length < 16) return {};
            const auto type = u16(offset + 8), channels = u16(offset + 10), bits = u16(offset + 22);
            const auto sampleRate = u32(offset + 12), byteRate = u32(offset + 16);
            blockAlign = u16(offset + 20);
            // V1 accepts standard PCM or IEEE float WAV; other codecs need explicit support.
            if ((type != 1 && type != 3) || channels == 0 || channels > 8 || sampleRate == 0 || sampleRate > 384000 ||
                (type == 1 && bits != 8 && bits != 16 && bits != 24 && bits != 32) ||
                (type == 3 && bits != 32 && bits != 64) || blockAlign != channels * (bits / 8) ||
                byteRate != static_cast<quint64>(sampleRate) * blockAlign) return {};
            formatFound = true;
            info.format = type; info.channels = channels; info.sampleRate = sampleRate; info.bitsPerSample = bits;
        } else if (tag == "data") {
            if (dataFound || length == 0) return {};
            dataFound = true; info.dataBytes = length;
        }
        offset = static_cast<qsizetype>(next);
    }
    if (!formatFound || !dataFound || info.dataBytes % blockAlign != 0) return {};
    return info;
}
std::optional<QByteArray> MiMoTtsService::parseAudio(const QByteArray& response) {
    if (response.isEmpty() || response.size() > MaxResponseBytes) return {};
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(response, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject() || document.object().contains("error")) return {};
    const auto choices = document.object().value("choices").toArray();
    if (choices.size() != 1 || !choices.first().isObject()) return {};
    const auto choice = choices.first().toObject();
    if (choice.contains("finish_reason") && choice.value("finish_reason").toString() != "stop") return {};
    const auto data = choice.value("message").toObject().value("audio").toObject().value("data");
    if (!data.isString()) return {};
    const auto encodedText = data.toString();
    if (encodedText.isEmpty() || encodedText.size() > (MaxAudioBytes + 2) / 3 * 4) return {};
    // Reject non-ASCII, invalid padding and noncanonical Base64 rather than repair it.
    const auto encoded = encodedText.toLatin1();
    const auto decoded = QByteArray::fromBase64Encoding(encoded, QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded || QString::fromLatin1(encoded) != encodedText || decoded.decoded.toBase64() != encoded || !validWav(decoded.decoded)) return {};
    return decoded.decoded;
}
bool MiMoTtsService::generate(const ai::Configuration& config, const VoiceDesignRequest& input) {
    if (isBusy()) return false;
    if (const auto error = validate(config, input); !error.isEmpty()) { emit failed(VoiceDesignError::InvalidInput, error, input.revision); return false; }
    const auto body = requestBody(input, config.model);
    if (body.size() > MaxRequestBytes) {
        emit failed(VoiceDesignError::InvalidInput, tr("Voice design request is too large."), input.revision); return false;
    }
    m_snapshot = input; m_model = config.model.trimmed(); m_body.clear(); m_abortReason.reset(); m_cancelled = false;
    QNetworkRequest request(endpoint(config.baseUrl));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Accept", "application/json");
    request.setRawHeader("Authorization", "Bearer " + config.apiKey.toUtf8());
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    request.setTransferTimeout(m_timeoutMs);
    auto* reply = m_network.post(request, body);
    m_reply = reply;
    reply->setReadBufferSize(64 * 1024);
    connect(reply, &QNetworkReply::readyRead, this, &MiMoTtsService::read);
    connect(reply, &QNetworkReply::finished, this, [this, reply] { finish(reply); });
    m_timeout.start(m_timeoutMs);
    return true;
}
void MiMoTtsService::read() {
    if (!m_reply || !m_reply->isOpen() || m_abortReason || m_cancelled) return;
    m_body += m_reply->read(MaxResponseBytes + 1 - m_body.size());
    if (m_body.size() > MaxResponseBytes) { m_abortReason = VoiceDesignError::ResponseTooLarge; m_reply->abort(); }
}
void MiMoTtsService::finish(QNetworkReply* reply) {
    if (m_reply != reply) return;
    read();
    // read() may abort, but QNetworkReply emits finished only once.
    m_timeout.stop();
    const auto status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto networkError = reply->error();
    m_reply.clear(); reply->deleteLater();
    const auto snapshot = m_snapshot;
    const auto body = std::move(m_body); m_body.clear(); m_snapshot = {};
    const auto fail = [this, &snapshot](const VoiceDesignError code, const QString& message) { emit failed(code, message, snapshot.revision); };
    if (m_cancelled) { emit cancelled(snapshot.revision); return; }
    if (m_abortReason == VoiceDesignError::ResponseTooLarge) { fail(*m_abortReason, tr("Voice design response exceeded the 34 MiB limit.")); return; }
    if (m_abortReason == VoiceDesignError::Timeout || networkError == QNetworkReply::TimeoutError) {
        fail(VoiceDesignError::Timeout, tr("Voice design request timed out.")); return;
    }
    // Server bodies and Qt errorString can contain submitted text, URLs or keys.
    if (status == 401 || status == 403) { fail(VoiceDesignError::Authorization, tr("API authorization failed. Check the API key and voice model permissions.")); return; }
    if (status == 429) { fail(VoiceDesignError::RateLimit, tr("API rate limit or quota exceeded.")); return; }
    if (status != 200) {
        fail(status ? VoiceDesignError::Http : VoiceDesignError::Network,
            status ? tr("Voice design service returned HTTP %1.").arg(status) : tr("Could not connect to the voice design service.")); return;
    }
    if (networkError != QNetworkReply::NoError) { fail(VoiceDesignError::Network, tr("Voice design connection failed or the response was incomplete.")); return; }
    const auto audio = parseAudio(body);
    if (!audio) { fail(VoiceDesignError::InvalidResponse, tr("The service returned invalid audio. Expected a complete PCM or float WAV response.")); return; }
    emit completed(VoiceDesignResult{.audioData = *audio, .mimeType = "audio/wav", .modelId = m_model, .snapshot = snapshot, .createdAt = QDateTime::currentDateTimeUtc()});
}
void MiMoTtsService::cancel() {
    if (m_reply) { m_cancelled = true; m_reply->abort(); }
}
}
