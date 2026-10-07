#include "AiService.h"
#include "providers/OpenAICompatibleProvider.h"
namespace ai {
AiService::AiService(QObject* parent) : QObject(parent), m_provider(new OpenAICompatibleProvider(this)) {
    connect(m_provider, &AiProvider::completed, this, &AiService::completed);
    connect(m_provider, &AiProvider::failed, this, &AiService::failed);
    connect(m_provider, &AiProvider::cancelled, this, &AiService::cancelled);
    connect(m_provider, &AiProvider::modelsListed, this, &AiService::modelsListed);
}
bool AiService::isBusy() const { return m_provider->isBusy(); }
void AiService::cancel() { m_provider->cancel(); }
QString AiService::validate(const Configuration& c) { return OpenAICompatibleProvider::validate(c); }
void AiService::listModels(const Configuration& c) { if (!isBusy()) m_provider->listModels(c); }
void AiService::analyze(const Configuration& c, const QJsonObject& context, const QString& locale) {
    if (isBusy()) return;
    const auto original = context.value("original_text").toString();
    if (original.isEmpty() || original.toUtf8().size() > 16 * 1024) {
        emit failed(tr("AI analysis requires nonempty input of at most 16 KiB. Analyze a smaller section.")); return;
    }
    m_provider->request(c, context, locale == "zh_CN" ? "Simplified Chinese" : locale == "zh_TW" ? "Traditional Chinese" : "English");
}
}
