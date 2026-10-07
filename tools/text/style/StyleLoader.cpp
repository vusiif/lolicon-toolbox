#include "TextStyleEngine.h"
#include <QFile>
#include <QResource>
#include <QJsonDocument>
#include <QJsonArray>
#include <QStringDecoder>
#include <QTextBoundaryFinder>
#include <QRegularExpression>
#include <QCryptographicHash>
#include <QSet>
#include <cmath>
static void initializeStyles() { Q_INIT_RESOURCE(styles); }
namespace textstyle {
namespace {
struct Invalid { Error error; QString path, reason; };
void require(bool condition, const QString& path, const QString& reason) { if (!condition) throw Invalid{Error::InvalidStyle, path, reason}; }
QJsonObject object(const QJsonValue& value, const QString& path, const QStringList& fields) {
    require(value.isObject(), path, "must be an object"); const auto result = value.toObject();
    for (const auto& field : fields) require(result.contains(field), path + '.' + field, "required field missing");
    for (const auto& key : result.keys()) require(fields.contains(key), path + '.' + key, "unknown field");
    return result;
}
bool forbidden(char32_t c) {
    return c <= 0x1f || (c >= 0x7f && c <= 0x9f) || c == 0x2028 || c == 0x2029 || c == 0x061c || c == 0x200e || c == 0x200f ||
           (c >= 0x202a && c <= 0x202e) || (c >= 0x2066 && c <= 0x206f) || c == 0x200b || c == 0xfeff ||
           (c >= 0xfdd0 && c <= 0xfdef) || (c & 0xffff) >= 0xfffe;
}
QString string(const QJsonValue& value, const QString& path, int limit = 2 * 1024 * 1024, bool empty = false) {
    require(value.isString(), path, "must be a string"); const auto result = value.toString();
    require((empty || !result.isEmpty()) && result.size() <= limit && result.isValidUtf16(), path, "empty, too long or invalid Unicode string");
    for (auto c : result.toUcs4()) require(!forbidden(c), path, "forbidden Unicode scalar");
    return result;
}
QString id(const QJsonValue& value, const QString& path) {
    auto result = string(value, path, 64); static const QRegularExpression valid("^[a-z][a-z0-9_]{0,63}$");
    require(valid.match(result).hasMatch(), path, "invalid stable ID"); return result;
}
int number(const QJsonValue& value, const QString& path, int low, int high) {
    require(value.isDouble(), path, "must be an integer"); const double x = value.toDouble();
    require(std::isfinite(x) && std::floor(x) == x && x >= low && x <= high, path, "integer out of range"); return int(x);
}
QJsonArray array(const QJsonValue& value, const QString& path, int max) {
    require(value.isArray(), path, "must be an array"); const auto result = value.toArray(); require(result.size() <= max, path, "too many items"); return result;
}
QStringList ids(const QJsonValue& value, const QString& path, int max) {
    QStringList result; int index = 0;
    for (const auto& item : array(value, path, max)) { const auto key = id(item, path + '[' + QString::number(index++) + ']'); require(!result.contains(key), path, "duplicate ID"); result.append(key); }
    return result;
}
QJsonObject localized(const QJsonValue& value, const QString& path, bool empty) {
    const auto result = object(value, path, {"en", "zh_CN", "zh_TW"});
    for (const auto& key : result.keys()) string(result[key], path + '.' + key, 2 * 1024 * 1024, empty); return result;
}
int graphemes(const QString& text) { QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text); int count = 0; while (finder.toNextBoundary() >= 0) ++count; return count; }
}
bool white(char32_t c) { return (c >= 9 && c <= 13) || c == 32 || c == 0x85 || c == 0xa0 || c == 0x1680 || (c >= 0x2000 && c <= 0x200a) || c == 0x2028 || c == 0x2029 || c == 0x202f || c == 0x205f || c == 0x3000; }
Loaded Loader::parse(const QByteArray& bytes, const QString& resource) {
    try {
        require(bytes.size() <= 2 * 1024 * 1024 && !bytes.startsWith(QByteArray::fromHex("efbbbf")), "$", "resource too large or BOM present");
        QStringDecoder utf8(QStringDecoder::Utf8, QStringConverter::Flag::Stateless); const QString decoded = utf8(bytes);
        require(!utf8.hasError(), "$", "invalid UTF-8");
        QJsonParseError syntax; const auto doc = QJsonDocument::fromJson(bytes, &syntax);
        require(syntax.error == QJsonParseError::NoError && doc.isObject(), "$", "invalid JSON object");
        const auto top = object(doc.object(), "$", {"schema_version", "metadata", "rule_groups", "presets", "rules", "decorators"});
        require(top["schema_version"].isDouble() && top["schema_version"].toDouble() == std::floor(top["schema_version"].toDouble()), "schema_version", "must be an integer");
        if (top["schema_version"].toDouble() != 1) throw Invalid{Error::UnsupportedSchema, "schema_version", "unsupported schema version"};
        auto style = std::make_shared<Definition>();
        const auto meta = object(top["metadata"], "metadata", {"id", "version", "name", "description", "license", "sources"});
        style->id = id(meta["id"], "metadata.id"); style->version = string(meta["version"], "metadata.version");
        static const QRegularExpression version("^(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)\\.(0|[1-9][0-9]*)$");
        require(version.match(style->version).hasMatch(), "metadata.version", "expected major.minor.patch");
        style->names = localized(meta["name"], "metadata.name", false); style->descriptions = localized(meta["description"], "metadata.description", true);
        string(meta["license"], "metadata.license"); const auto sources = array(meta["sources"], "metadata.sources", 2 * 1024 * 1024); require(!sources.isEmpty(), "metadata.sources", "source attribution required");
        for (int i = 0; i < sources.size(); ++i) { const auto path = QString("metadata.sources[%1]").arg(i); const auto source = object(sources[i], path, {"label", "url", "license", "notes"}); for (const auto& field : source.keys()) string(source[field], path + '.' + field, 2 * 1024 * 1024, field == "url" || field == "notes"); }
        const auto groups = ids(top["rule_groups"], "rule_groups", 16); QSet<QString> seenIDs, seenSources, usedGroups;
        const auto rules = array(top["rules"], "rules", 10000);
        for (int i = 0; i < rules.size(); ++i) {
            const auto path = QString("rules[%1]").arg(i); const auto row = object(rules[i], path, {"id", "kind", "group", "source", "candidates"});
            Rule rule; rule.id = id(row["id"], path + ".id"); require(!seenIDs.contains(rule.id), path, "duplicate rule ID"); seenIDs.insert(rule.id);
            rule.group = id(row["group"], path + ".group"); require(groups.contains(rule.group), path + ".group", "unknown group"); usedGroups.insert(rule.group);
            rule.source = string(row["source"], path + ".source", 256); require(!seenSources.contains(rule.source), path, "duplicate source"); seenSources.insert(rule.source);
            const auto scalars = rule.source.toUcs4(); require(!white(scalars.first()) && !white(scalars.last()), path + ".source", "source has edge whitespace");
            const int count = graphemes(rule.source); const auto kind = string(row["kind"], path + ".kind");
            require(count <= 64 && ((kind == "character" && count == 1) || (kind == "phrase" && count >= 2)), path + ".kind", "kind does not match grapheme length");
            const auto choices = array(row["candidates"], path + ".candidates", 16); require(!choices.isEmpty(), path, "no candidates"); QSet<QString> seen;
            for (int n = 0; n < choices.size(); ++n) { const auto p = path + QString(".candidates[%1]").arg(n); const auto item = object(choices[n], p, {"text", "weight"}); const auto text = string(item["text"], p + ".text", 128); require(text != rule.source && !seen.contains(text), p, "identity or duplicate candidate"); seen.insert(text); rule.choices.append({text, uint64_t(number(item["weight"], p + ".weight", 1, 1000000))}); }
            style->rules.append(std::move(rule));
        }
        QSet<QString> decorators;
        const auto decoration = array(top["decorators"], "decorators", 64);
        for (int i = 0; i < decoration.size(); ++i) {
            const auto path = QString("decorators[%1]").arg(i); const auto row = object(decoration[i], path, {"id", "prefix", "suffix", "weight"});
            Decorator d; d.id = id(row["id"], path + ".id"); require(!decorators.contains(d.id), path, "duplicate decorator ID"); decorators.insert(d.id);
            d.prefix = string(row["prefix"], path + ".prefix", 32, true); d.suffix = string(row["suffix"], path + ".suffix", 32, true); require(!d.prefix.isEmpty() || !d.suffix.isEmpty(), path, "empty wrapper");
            d.weight = number(row["weight"], path + ".weight", 1, 1000000); style->decorators.append(d);
        }
        const auto presets = object(top["presets"], "presets", {"light", "medium", "heavy"}); QSet<QString> referenced; bool usable = false;
        for (const auto& key : presets.keys()) {
            const auto path = "presets." + key; const auto row = object(presets[key], path, {"rule_groups", "replacement_rate_bp", "decorator_rate_bp", "decorator_ids"}); Preset p;
            p.groups = ids(row["rule_groups"], path + ".rule_groups", 16); p.decorators = ids(row["decorator_ids"], path + ".decorator_ids", 64);
            for (const auto& group : p.groups) { require(groups.contains(group), path, "unknown group reference"); referenced.insert(group); }
            for (const auto& d : p.decorators) require(decorators.contains(d), path, "unknown decorator reference");
            p.replaceRate = number(row["replacement_rate_bp"], path + ".replacement_rate_bp", 0, 10000); p.decorateRate = number(row["decorator_rate_bp"], path + ".decorator_rate_bp", 0, 10000);
            require((p.decorateRate == 0) == p.decorators.isEmpty(), path, "decorator rate/list mismatch");
            usable |= (p.replaceRate > 0 && !p.groups.isEmpty()) || p.decorateRate > 0; style->presets.insert(key, p);
        }
        for (const auto& group : groups) require(usedGroups.contains(group) && referenced.contains(group), "rule_groups", "unused group");
        require(usable && (!style->rules.isEmpty() || !style->decorators.isEmpty()), "$", "no usable capability");
        style->sha256 = QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
        return {style, Error::None, {}};
    } catch (const Invalid& error) { return {{}, error.error, resource + ": " + error.path + ": " + error.reason}; }
}
Loaded Loader::builtin() {
    static const Loaded result = [] { initializeStyles(); QFile file(":/styles/mars_classic.json"); if (!file.open(QIODevice::ReadOnly)) return Loaded{{}, Error::StyleUnavailable, "Cannot read built-in style resource"}; return parse(file.readAll(), file.fileName()); }();
    return result;
}
}
