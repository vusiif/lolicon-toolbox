#pragma once
#include "ai/AiProvider.h"
#include <QDateTime>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTimer>
#include <optional>

class QNetworkReply;
namespace speech {
struct VoiceDesignRequest {
    QString voiceDescription;
    QString previewText;
    quint64 revision = 0; // Supplied by the page; echoed with every terminal signal.
};
struct VoiceDesignResult {
    QByteArray audioData;
    QString mimeType = QStringLiteral("audio/wav");
    QString modelId;
    VoiceDesignRequest snapshot;
    QDateTime createdAt;
};
// Format facts read from a WAV that passed validation; used for display only.
struct WavInfo {
    quint16 format = 0; // 1 = PCM, 3 = IEEE float
    quint16 channels = 0;
    quint32 sampleRate = 0;
    quint16 bitsPerSample = 0;
    quint32 dataBytes = 0;
    qint64 durationMs() const {
        const quint64 bytesPerSecond = static_cast<quint64>(sampleRate) * channels * (bitsPerSample / 8);
        return bytesPerSecond ? static_cast<qint64>(static_cast<quint64>(dataBytes) * 1000 / bytesPerSecond) : 0;
    }
};
enum class VoiceDesignError {
    InvalidInput, Authorization, RateLimit, Http, Network, Timeout,
    ResponseTooLarge, InvalidResponse
};

// VoiceDesign only. The model comes from the voice API settings (see AppSettings::voiceConfiguration).
// No shared text-model parser, streaming or persistent history.
class MiMoTtsService : public QObject {
    Q_OBJECT
public:
    static constexpr qsizetype MaxDescriptionBytes = 8 * 1024;
    static constexpr qsizetype MaxPreviewBytes = 16 * 1024;
    static constexpr qsizetype MaxRequestBytes = 128 * 1024;
    static constexpr qsizetype MaxAudioBytes = 24 * 1024 * 1024;
    static constexpr qsizetype MaxResponseBytes = 34 * 1024 * 1024;
    explicit MiMoTtsService(QObject* parent = nullptr, int timeoutMs = 90000);
    ~MiMoTtsService() override;
    static QString defaultBaseUrl();
    static QString validate(const ai::Configuration&, const VoiceDesignRequest&);
    static QUrl endpoint(const QString& baseUrl);
    static QByteArray requestBody(const VoiceDesignRequest&, const QString& model);
    static std::optional<QByteArray> parseAudio(const QByteArray& response);
    static std::optional<WavInfo> wavInfo(const QByteArray& audio);
    static bool validWav(const QByteArray& audio) { return wavInfo(audio).has_value(); }
    bool isBusy() const { return !m_reply.isNull(); }
    // A busy service rejects a second submission without disturbing the active one.
    bool generate(const ai::Configuration&, const VoiceDesignRequest&);
    void cancel();
signals:
    void completed(const speech::VoiceDesignResult& result);
    void failed(speech::VoiceDesignError code, const QString& message, quint64 revision);
    void cancelled(quint64 revision);
private:
    void read();
    void finish(QNetworkReply* reply);
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QTimer m_timeout;
    int m_timeoutMs;
    QByteArray m_body;
    VoiceDesignRequest m_snapshot;
    QString m_model;
    bool m_cancelled = false;
    std::optional<VoiceDesignError> m_abortReason;
};
}
Q_DECLARE_METATYPE(speech::VoiceDesignResult)
Q_DECLARE_METATYPE(speech::VoiceDesignError)
