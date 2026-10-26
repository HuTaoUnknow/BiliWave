#include "lrc.h"
#include <QFile>
#include <QHash>
#include <QRegularExpression>
#include <QResource>
#include <QVector>
#include <algorithm>

static void initializeChineseResources() {
    // Keep the dictionary resources linked when bili_core is a static library.
    Q_INIT_RESOURCE(chinese_lyrics);
}

namespace {
class ChineseDictionary {
    struct Node {
        QHash<QChar, int> next;
        QString replacement;
    };
    QVector<Node> m_nodes{Node{}};

  public:
    ChineseDictionary() {
        initializeChineseResources();
        // Phrase entries override individual characters and preserve cases such
        // as 乾隆 / 乾坤 while converting 乾淨 to 干净.
        for (const auto &path : {":/biliwave/chinese/TSCharacters.txt", ":/biliwave/chinese/TSPhrases.txt"}) {
            QFile file(path);
            if (!file.open(QIODevice::ReadOnly))
                qFatal("Missing embedded Chinese conversion dictionary: %s", path);
            while (!file.atEnd()) {
                const auto line = QString::fromUtf8(file.readLine()).trimmed();
                const auto tab = line.indexOf('\t');
                if (line.startsWith('#') || tab <= 0)
                    continue;
                const auto key = line.left(tab);
                const auto value = line.mid(tab + 1).section(' ', 0, 0);
                if (value.isEmpty())
                    continue;
                int node = 0;
                for (const auto character : key) {
                    int next = m_nodes[node].next.value(character, -1);
                    if (next < 0) {
                        next = int(m_nodes.size());
                        m_nodes[node].next.insert(character, next);
                        m_nodes.append(Node{});
                    }
                    node = next;
                }
                m_nodes[node].replacement = value;
            }
        }
    }
    QString convert(const QString &text) const {
        QString converted;
        converted.reserve(text.size());
        for (qsizetype start = 0; start < text.size();) {
            int node = 0, match = -1;
            qsizetype end = start;
            for (qsizetype cursor = start; cursor < text.size(); ++cursor) {
                node = m_nodes[node].next.value(text[cursor], -1);
                if (node < 0)
                    break;
                if (!m_nodes[node].replacement.isEmpty()) {
                    match = node;
                    end = cursor + 1;
                }
            }
            if (match >= 0) {
                converted += m_nodes[match].replacement;
                start = end;
            } else
                converted += text[start++];
        }
        return converted;
    }
};
} // namespace

QString Lrc::simplifiedChinese(const QString &text, bool enabled) {
    if (!enabled || text.isEmpty())
        return text;
    static const ChineseDictionary dictionary;
    return dictionary.convert(text);
}

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
