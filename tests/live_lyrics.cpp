#include "lrc.h"
#include "services.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
int main(int argc, char **argv) {
    QCoreApplication app(argc, argv);
    const auto dir = QDir::current().filePath(".cache/live-lyrics");
    Library library(dir);
    QSettings settings(dir + "/settings.ini", QSettings::IniFormat);
    settings.setValue("lyrics/source", "online");
    auto id = library.addTrack({{"title", "Yellow"},
                                {"artist", "Coldplay"},
                                {"duration", 268000},
                                {"path", "metadata-only-test-fixture"}});
    library.setLyrics(id, QStringLiteral(""), QStringLiteral(""));
    LyricService service(&library, &settings);
    QJsonObject report;
    report["fixture"] = "metadata-only: Yellow / Coldplay / 268s";
    QObject::connect(&service, &LyricService::message, &app,
                     [&](const QString &msg) { report["last_message"] = msg; });
    auto finish = [&](bool ok) {
        report["success"] = ok;
        report["provider"] = library.track(id).value("lyric_source").toString();
        report["timed_lines"] = int(Lrc::parse(library.track(id).value("lrc").toString()).size());
        QFile file("docs/live-lyrics-result.json");
        if (file.open(QIODevice::WriteOnly))
            file.write(QJsonDocument(report).toJson());
        app.exit(ok ? 0 : 1);
    };
    QObject::connect(&library, &Library::changed, &app, [&] {
        if (!library.track(id).value("lrc").toString().isEmpty())
            QTimer::singleShot(0, &app, [&] { finish(true); });
    });
    QObject::connect(&service, &LyricService::candidates, &app,
                     [&](qint64 track, const QList<QVariantMap> &items) {
                         for (const auto &item : items)
                             if (item.value("difference").toDouble() <= 3) {
                                 service.useCandidate(track, item);
                                 return;
                             }
                         finish(false);
                     });
    QTimer::singleShot(0, &app, [&] { service.fetch(id, true); });
    QTimer::singleShot(35000, &app, [&] { finish(false); });
    return app.exec();
}
