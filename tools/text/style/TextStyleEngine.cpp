#include "TextStyleEngine.h"
#include <QTextBoundaryFinder>
#include <QElapsedTimer>
#include <QCryptographicHash>
#include <QSet>
#include <algorithm>
#include <stdexcept>
namespace textstyle {
uint64_t Random::next() { if (m_check) m_check(); ++m_calls; m_state += UINT64_C(0x9E3779B97F4A7C15); auto z = m_state; z = (z ^ (z >> 30)) * UINT64_C(0xBF58476D1CE4E5B9); z = (z ^ (z >> 27)) * UINT64_C(0x94D049BB133111EB); return z ^ (z >> 31); }
uint64_t Random::sample(uint64_t b, const std::function<uint64_t()>& raw) { if (!b) throw std::invalid_argument("zero random bound"); const auto threshold = (uint64_t(0) - b) % b; for (;;) { const auto x = raw(); if (x >= threshold) return x % b; } }
uint64_t Random::bounded(uint64_t b) { return sample(b, [this] { return next(); }); }
bool Random::gate(int rate) { return rate == 10000 || (rate > 0 && bounded(10000) < uint64_t(rate)); }
int Random::choose(const QList<uint64_t>& weights) { if (weights.size() == 1) return 0; uint64_t sum = 0; for (auto weight : weights) sum += weight; auto n = bounded(sum); for (int i = 0; i < weights.size(); ++i) { if (n < weights[i]) return i; n -= weights[i]; } throw std::logic_error("invalid weights"); }
QString Engine::normalize(QString text) { return text.replace("\r\n", "\n").replace('\r', '\n').replace(QChar::LineSeparator, '\n').replace(QChar::ParagraphSeparator, '\n'); }
bool Engine::seed(const QString& input, uint64_t& value) {
    auto text = input; while (text.startsWith(' ')) text.remove(0, 1); while (text.endsWith(' ')) text.chop(1); if (text.isEmpty()) return false;
    uint64_t result = 0;
    for (auto c : text) { if (c < '0' || c > '9') return false; const auto digit = uint64_t(c.unicode() - '0'); if (result > (UINT64_MAX - digit) / 10) return false; result = result * 10 + digit; }
    value = result; return true;
}
bool Engine::userSeed(const QString& input, UserSeed& result) {
    if (input.isEmpty() || input.size() > 64) return false;
    QString canonical; canonical.reserve(input.size()); bool letters = false;
    for (auto c : input) {
        if (c >= 'A' && c <= 'Z') c = QChar(c.unicode() + ('a' - 'A'));
        if (c >= 'a' && c <= 'z') letters = true;
        else if (c < '0' || c > '9') return false;
        canonical.append(c);
    }
    UserSeed parsed;
    if (!letters) {
        if (!seed(canonical, parsed.value)) return false;
        parsed.text = QString::number(parsed.value); parsed.mapping = "decimal-u64-v1";
    } else {
        const auto bytes = QCryptographicHash::hash(QByteArray("toolbox-text-style-seed-v1:") + canonical.toLatin1(), QCryptographicHash::Sha256);
        for (int i = 0; i < 8; ++i) parsed.value = (parsed.value << 8) | static_cast<unsigned char>(bytes[i]);
        parsed.text = canonical; parsed.mapping = "ascii-alnum-sha256be64-v1";
    }
    result = std::move(parsed); return true;
}
QString errorName(Error e) { switch(e) {
    case Error::None: return "None"; case Error::StyleUnavailable: return "StyleUnavailable"; case Error::InvalidStyle: return "InvalidStyle";
    case Error::UnsupportedSchema: return "UnsupportedSchema"; case Error::InvalidPreset: return "InvalidPreset"; case Error::InvalidSeed: return "InvalidSeed";
    case Error::InvalidUnicodeInput: return "InvalidUnicodeInput"; case Error::InputTooLarge: return "InputTooLarge"; case Error::OutputTooLarge: return "OutputTooLarge";
    case Error::Cancelled: return "Cancelled"; case Error::Timeout: return "Timeout"; default: return "InternalError";
} }
Result Engine::transform(const QString& input, std::shared_ptr<const Definition> style, const QString& presetID, uint64_t seedValue, const Control& control) {
    try {
        QElapsedTimer clock; clock.start();
        auto check = [&] { if (control.cancelled && control.cancelled->load()) throw Error::Cancelled; if ((control.elapsed ? control.elapsed() : clock.elapsed()) >= control.budgetMs) throw Error::Timeout; };
        check(); if (!style) throw Error::StyleUnavailable; if (!style->presets.contains(presetID)) throw Error::InvalidPreset;
        if (input.size() > MaxInputUnits) throw Error::InputTooLarge; if (!input.isValidUtf16()) throw Error::InvalidUnicodeInput;
        const auto text = normalize(input); const auto utf8 = text.toUtf8(); if (utf8.size() > MaxInputBytes) throw Error::InputTooLarge; check();
        const auto preset = style->presets[presetID]; QHash<char32_t, QList<const Rule*>> index;
        for (const auto& rule : style->rules) { check(); if (preset.groups.contains(rule.group)) index[rule.source.toUcs4().first()].append(&rule); }
        for (auto it = index.begin(); it != index.end(); ++it) std::sort(it->begin(), it->end(), [](auto a, auto b) { return a->source.size() > b->source.size(); });
        QList<const Decorator*> wrappers; QList<uint64_t> wrapperWeights;
        for (const auto& id : preset.decorators) for (const auto& d : style->decorators) if (d.id == id) { wrappers.append(&d); wrapperWeights.append(d.weight); break; }
        Random random(seedValue, check); Result result; result.stats.inputBytes = utf8.size(); qsizetype outBytes = 0, outUnits = 0;
        auto append = [&](QString& buffer, QStringView chunk) { const auto bytes = chunk.toUtf8().size(); if (outBytes + bytes > MaxOutputBytes || outUnits + chunk.size() > MaxOutputUnits) throw Error::OutputTooLarge; buffer.append(chunk); outBytes += bytes; outUnits += chunk.size(); };
        qsizetype start = 0;
        do {
            check(); const auto newline = text.indexOf('\n', start); const auto end = newline < 0 ? text.size() : newline;
            const auto line = text.mid(start, end - start); QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, line);
            QList<qsizetype> boundaries{0}; QList<bool> spaces; QSet<qsizetype> ends; qsizetype prev = 0, boundary;
            while ((boundary = finder.toNextBoundary()) >= 0) { check(); boundaries.append(boundary); ends.insert(boundary); bool blank = true; for (auto c : QStringView(line).mid(prev, boundary - prev).toUcs4()) blank &= white(c); spaces.append(blank); prev = boundary; }
            int first = 0, last = spaces.size(); while (first < last && spaces[first]) ++first; while (last > first && spaces[last - 1]) --last;
            const auto bodyStart = boundaries[first], bodyEnd = boundaries[last]; QString leading, body, trailing;
            append(leading, QStringView(line).left(bodyStart));
            int token = first;
            while (token < last) {
                check(); const auto pos = boundaries[token]; const auto scalar = QChar::isHighSurrogate(line[pos].unicode()) ? QChar::surrogateToUcs4(line[pos], line[pos + 1]) : char32_t(line[pos].unicode());
                const Rule* match = nullptr;
                const auto rules = index.constFind(scalar);
                if (rules != index.cend()) for (auto* rule : *rules) { check(); const auto stop = pos + rule->source.size(); if (stop <= bodyEnd && ends.contains(stop) && QStringView(line).mid(pos, rule->source.size()) == rule->source) { match = rule; break; } }
                if (!match) { append(body, QStringView(line).mid(pos, boundaries[token + 1] - pos)); ++token; continue; }
                ++result.stats.matched;
                if (random.gate(preset.replaceRate)) { QList<uint64_t> weights; for (const auto& c : match->choices) weights.append(c.weight); append(body, match->choices[random.choose(weights)].text); ++result.stats.replaced; }
                else append(body, match->source);
                const auto stop = pos + match->source.size(); while (boundaries[token] < stop) ++token;
            }
            append(trailing, QStringView(line).mid(bodyEnd));
            QString prefix, suffix;
            if (first < last && random.gate(preset.decorateRate)) { const auto* wrapper = wrappers[random.choose(wrapperWeights)]; append(prefix, wrapper->prefix); append(suffix, wrapper->suffix); ++result.stats.decorated; }
            result.output += leading; result.output += prefix; result.output += body; result.output += suffix; result.output += trailing;
            if (newline >= 0) append(result.output, QStringView(u"\n")); start = end + 1;
        } while (start <= text.size());
        check(); result.stats.outputBytes = outBytes; result.randomCalls = random.calls();
        result.metadata = {{"reproduction_schema_version", 1}, {"tool_id", "text.style_converter"}, {"engine_version", "1.0.0"},
            {"rng_id", "splitmix64-v1"}, {"normalization_id", "line-endings-lf-v1"}, {"runtime_profile", QString("Qt-%1/QTextBoundaryFinder-EGC-v1").arg(qVersion())},
            {"style_id", style->id}, {"style_version", style->version}, {"style_sha256", style->sha256}, {"preset_id", presetID}, {"seed", QString::number(seedValue)},
            {"input_sha256", QString::fromLatin1(QCryptographicHash::hash(utf8, QCryptographicHash::Sha256).toHex())}};
        return result;
    } catch (Error error) { Result result; result.error = error; return result; }
    catch (...) { Result result; result.error = Error::InternalError; return result; }
}
}
