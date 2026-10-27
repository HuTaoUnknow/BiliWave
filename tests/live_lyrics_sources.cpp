#include "onlinelyrics.h"
#include "lrc.h"
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif
static qint64 memoryBytes() {
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        return qint64(counters.WorkingSetSize);
#endif
    return 0;
}
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() < 4) return 2;
    LyricsSourcesSearch search;
    const auto baseline = memoryBytes();
    QElapsedTimer elapsed; elapsed.start();
    QObject::connect(&search, &LyricsSourcesSearch::finished, &app,
                     [&](const QList<QVariantMap> &rows, const QString &error) {
        auto report = QJsonObject::fromVariantMap(search.statistics());
        report["elapsed_ms"] = elapsed.elapsed();
        report["working_set_before"] = baseline;
        report["working_set_after"] = memoryBytes();
        report["error"] = error;
        QJsonArray results;
        for (const auto &c : rows) {
            auto summary = QJsonObject::fromVariantMap(c);
            summary.remove("syncedLyrics"); summary.remove("plainLyrics");
            summary["timed_lines"] = int(Lrc::parse(c.value("syncedLyrics").toString()).size());
            results << summary;
        }
        report["candidates"] = results;
        QFile output(args[3]);
        if (output.open(QIODevice::WriteOnly)) output.write(QJsonDocument(report).toJson());
        app.exit(rows.isEmpty() ? 1 : 0);
    });
    search.search({{"id", 49}, {"title", args[1]}, {"artist", args.value(4)}, {"duration", 226218}}, {}, args[2].split(','));
    return app.exec();
}
