#include "lrc.h"
#include <QRegularExpression>
#include <algorithm>
QList<LyricLine> Lrc::parse(const QString &text) {
    QList<LyricLine> out;
    qint64 offset = 0;
    const QRegularExpression offsetRx("\\[offset:([+-]?\\d+)\\]", QRegularExpression::CaseInsensitiveOption);
    auto om = offsetRx.match(text);
    if (om.hasMatch())
        offset = om.captured(1).toLongLong();
    const QRegularExpression timeRx("\\[(\\d+):(\\d{1,2})(?:[.:](\\d{1,3}))?\\]");
    for (const auto &line : text.split('\n')) {
        auto matches = timeRx.globalMatch(line);
        QList<qint64> stamps;
        int end = 0;
        while (matches.hasNext()) {
            const auto m = matches.next();
            if (m.captured(2).toInt() >= 60)
                continue;
            QString fraction = m.captured(3);
            while (fraction.size() < 3)
                fraction += '0';
            stamps << m.captured(1).toLongLong() * 60000 + m.captured(2).toInt() * 1000 + fraction.toInt() +
                          offset;
            end = m.capturedEnd();
        }
        const auto words = line.mid(end).trimmed();
        for (auto ms : stamps)
            out << LyricLine{ms, words};
    }
    std::stable_sort(out.begin(), out.end(), [](const auto &a, const auto &b) { return a.ms < b.ms; });
    QList<LyricLine> grouped;
    for (int i = 0; i < out.size();) {
        int end = i;
        QStringList words;
        bool hasClear = false;
        while (end < out.size() && out[end].ms == out[i].ms) {
            words << out[end].text;
            hasClear = hasClear || out[end].text.isEmpty();
            ++end;
        }
        const auto pair = bilingualLines(words.join('\n'));
        if (!hasClear && !pair.first.isEmpty())
            grouped << LyricLine{out[i].ms, pair.first + '\n' + pair.second};
        else
            for (int j = i; j < end; ++j)
                grouped << out[j];
        i = end;
    }
    return grouped;
}
QPair<QString, QString> Lrc::bilingualLines(const QString &text) {
    static const QRegularExpression han("[\\x{3400}-\\x{9fff}]");
    static const QRegularExpression foreign("[\\p{Latin}\\p{Cyrillic}]");
    QStringList original, translation;
    for (const auto &line : text.split('\n', Qt::SkipEmptyParts)) {
        if (line.contains(han))
            translation << line.trimmed();
        else if (line.contains(foreign))
            original << line.trimmed();
    }
    if (original.isEmpty() || translation.isEmpty())
        return {};
    return {original.join(' '), translation.join(' ')};
}
QString Lrc::serialize(const QList<LyricLine> &lines) {
    QStringList output;
    for (const auto &line : lines) {
        const auto ms = qMax(qint64(0), line.ms);
        const auto stamp = QString("[%1:%2.%3]")
                               .arg(ms / 60000, 2, 10, QLatin1Char('0'))
                               .arg(ms / 1000 % 60, 2, 10, QLatin1Char('0'))
                               .arg(ms % 1000, 3, 10, QLatin1Char('0'));
        for (const auto &part : line.text.split('\n'))
            output << stamp + part;
    }
    return output.join('\n');
}
int Lrc::indexAt(const QList<LyricLine> &lines, qint64 pos) {
    auto it = std::upper_bound(lines.begin(), lines.end(), pos,
                               [](qint64 ms, const LyricLine &l) { return ms < l.ms; });
    return int(it - lines.begin()) - 1;
}
