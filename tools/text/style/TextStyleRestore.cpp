#include "TextStyleRestore.h"
#include <QElapsedTimer>
#include <QTextBoundaryFinder>
#include <QSet>
#include <algorithm>
namespace textstyle {
Restoration Restore::run(const QString& input, std::shared_ptr<const Definition> style, bool removeWrappers, const Control& control) {
    try {
        QElapsedTimer timer; timer.start();
        auto check = [&] {
            if (control.cancelled && control.cancelled->load()) throw Error::Cancelled;
            if ((control.elapsed ? control.elapsed() : timer.elapsed()) >= control.budgetMs) throw Error::Timeout;
        };
        check(); if (!style) throw Error::StyleUnavailable;
        if (input.size() > Engine::MaxInputUnits) throw Error::InputTooLarge;
        if (!input.isValidUtf16()) throw Error::InvalidUnicodeInput;
        const auto normalized = Engine::normalize(input);
        if (normalized.toUtf8().size() > Engine::MaxInputBytes) throw Error::InputTooLarge;
        struct Mapping { QString text; QStringList sources; };
        QHash<QString, QStringList> reverse;
        for (const auto& rule : style->rules) for (const auto& choice : rule.choices) {
            check(); auto& sources = reverse[choice.text]; if (!sources.contains(rule.source)) sources.append(rule.source);
        }
        QHash<char32_t, QList<Mapping>> index;
        for (auto it = reverse.cbegin(); it != reverse.cend(); ++it) { check(); index[it.key().toUcs4().first()].append({it.key(), it.value()}); }
        for (auto it = index.begin(); it != index.end(); ++it)
            std::sort(it->begin(), it->end(), [](const auto& a, const auto& b) { return a.text.size() != b.text.size() ? a.text.size() > b.text.size() : a.text < b.text; });
        // Four bounded policies: conservative, then three positional alternatives.
        // These are possibilities, not probability-ranked or context-verified results.
        QStringList outputs{QString(), QString(), QString(), QString()}; QList<qsizetype> sizes{0,0,0,0};
        Restoration result;
        auto append = [&](int slot, QStringView text) {
            const auto bytes = text.toUtf8().size();
            if (sizes[slot] + bytes > Engine::MaxOutputBytes || outputs[slot].size() + text.size() > Engine::MaxOutputUnits) throw Error::OutputTooLarge;
            outputs[slot].append(text); sizes[slot] += bytes;
        };
        qsizetype start = 0;
        do {
            check(); const auto newline = normalized.indexOf('\n', start), end = newline < 0 ? normalized.size() : newline;
            const auto line = normalized.mid(start, end - start); QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, line);
            QList<qsizetype> boundaries{0}; QSet<qsizetype> ends{0}; qsizetype boundary;
            while ((boundary = finder.toNextBoundary()) >= 0) { check(); boundaries.append(boundary); ends.insert(boundary); }
            auto blank = [&](int at) { for (auto c : QStringView(line).mid(boundaries[at],boundaries[at+1]-boundaries[at]).toUcs4()) if (!white(c)) return false; return true; };
            int first = 0, last = boundaries.size()-1;
            while (first < last && blank(first)) ++first; while (last > first && blank(last-1)) --last;
            auto bodyStart = boundaries[first], bodyEnd = boundaries[last];
            if (removeWrappers && first < last) for (const auto& wrapper : style->decorators) {
                check(); const auto body = QStringView(line).mid(bodyStart,bodyEnd-bodyStart);
                if (body.size() >= wrapper.prefix.size()+wrapper.suffix.size() && body.startsWith(wrapper.prefix) && body.endsWith(wrapper.suffix) &&
                    ends.contains(bodyStart+wrapper.prefix.size()) && ends.contains(bodyEnd-wrapper.suffix.size())) {
                    bodyStart += wrapper.prefix.size(); bodyEnd -= wrapper.suffix.size(); ++result.wrappers; break;
                }
            }
            for (int slot=0;slot<4;++slot) append(slot,QStringView(line).left(boundaries[first]));
            int token = first; while (boundaries[token] < bodyStart) ++token;
            while (boundaries[token] < bodyEnd) {
                check(); const auto pos = boundaries[token]; const auto scalar = QChar::isHighSurrogate(line[pos].unicode()) ? QChar::surrogateToUcs4(line[pos],line[pos+1]) : char32_t(line[pos].unicode());
                const Mapping* match = nullptr; const auto found = index.constFind(scalar);
                if (found != index.cend()) for (const auto& row : *found) {
                    check(); if (pos+row.text.size()<=bodyEnd && ends.contains(pos+row.text.size()) && QStringView(line).mid(pos,row.text.size())==row.text) { match=&row; break; }
                }
                if (!match) { for (int slot=0;slot<4;++slot) append(slot,QStringView(line).mid(pos,boundaries[token+1]-pos)); ++token; continue; }
                ++result.matched; const bool ambiguous = match->sources.size()>1;
                if (ambiguous) {
                    ++result.ambiguous;
                    if (result.ambiguities.size()<32) { QJsonArray choices; for (const auto& source : match->sources) choices.append(source); result.ambiguities.append(QJsonObject{{"offset_utf16",start+pos},{"text",match->text},{"sources",choices}}); }
                }
                append(0,ambiguous ? match->text : match->sources.first());
                for (int slot=1;slot<4;++slot) append(slot,match->sources[qMin(slot-1,int(match->sources.size()-1))]);
                const auto stop = pos+match->text.size(); while (boundaries[token] < stop) ++token;
            }
            for (int slot=0;slot<4;++slot) { append(slot,QStringView(line).mid(boundaries[last])); if(newline>=0) append(slot,QStringView(u"\n")); }
            start=end+1;
        } while (start<=normalized.size());
        check(); outputs.append(normalized); outputs.removeDuplicates(); result.candidates=outputs; return result;
    } catch (Error error) { Restoration result; result.error=error; return result; }
    catch (...) { Restoration result; result.error=Error::InternalError; return result; }
}
}
