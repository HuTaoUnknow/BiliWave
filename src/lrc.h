#pragma once
#include <QList>
#include <QPair>
#include <QString>
struct LyricLine {
    qint64 ms;
    QString text;
};
class Lrc {
  public:
    static QList<LyricLine> parse(const QString &text);
    static int indexAt(const QList<LyricLine> &lines, qint64 position);
    static QPair<QString, QString> bilingualLines(const QString &text);
    static QString serialize(const QList<LyricLine> &lines);
    static QString simplifiedChinese(const QString &text, bool enabled = true);
    static bool isBilingual(const QList<LyricLine> &lines);
    static QPair<QString, QString> displayAt(const QList<LyricLine> &lines, qint64 position, bool bilingual);
};
