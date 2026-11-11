#pragma once
#include <QString>
#include <QJsonObject>
#include <QHash>
#include <QList>
#include <memory>
#include <atomic>
#include <functional>
#include <cstdint>
namespace textstyle {
enum class Error { None, StyleUnavailable, InvalidStyle, UnsupportedSchema, InvalidPreset, InvalidSeed, InvalidUnicodeInput, InputTooLarge, OutputTooLarge, Cancelled, Timeout, InternalError };
struct Choice { QString text; uint64_t weight; };
struct Rule { QString id, group, source; QList<Choice> choices; };
struct Decorator { QString id, prefix, suffix; uint64_t weight; };
struct Preset { QStringList groups, decorators; int replaceRate = 0, decorateRate = 0; };
struct Definition {
    QString id, version, sha256;
    QJsonObject names, descriptions;
    QList<Rule> rules;
    QList<Decorator> decorators;
    QHash<QString, Preset> presets;
};
struct Loaded { std::shared_ptr<const Definition> style; Error error = Error::None; QString detail; };
struct Stats { uint64_t matched = 0, replaced = 0, decorated = 0, inputBytes = 0, outputBytes = 0; };
struct UserSeed { QString text, mapping; uint64_t value = 0; };
struct Result { Error error = Error::None; QString output; Stats stats; QJsonObject metadata; uint64_t randomCalls = 0; bool success() const { return error == Error::None; } };
struct Control {
    std::shared_ptr<std::atomic_bool> cancelled;
    // Clock injection supports deterministic timeout and late-completion tests.
    std::function<qint64()> elapsed;
    qint64 budgetMs = 5000;
};
class Random {
public:
    explicit Random(const uint64_t seed, std::function<void()> check = {}) : m_state(seed), m_check(std::move(check)) {}
    uint64_t next();
    uint64_t bounded(uint64_t bound);
    bool gate(int rate);
    int choose(const QList<uint64_t>& weights);
    uint64_t calls() const { return m_calls; }
    static uint64_t sample(uint64_t bound, const std::function<uint64_t()>& raw);
private:
    uint64_t m_state, m_calls = 0;
    std::function<void()> m_check;
};
class Loader {
public:
    static Loaded parse(const QByteArray& bytes, const QString& resource);
    static Loaded builtin();
};
class Engine {
public:
    static constexpr qsizetype MaxInputBytes = 256 * 1024, MaxInputUnits = 524288;
    static constexpr qsizetype MaxOutputBytes = 2 * 1024 * 1024, MaxOutputUnits = 2097152;
    static QString normalize(QString text);
    static bool seed(const QString& input, uint64_t& value);
    static bool userSeed(const QString& input, UserSeed& result);
    static Result transform(const QString& input, std::shared_ptr<const Definition> style, const QString& preset, uint64_t seed, const Control& control = {});
};
bool white(char32_t c);
QString errorName(Error error);
}
