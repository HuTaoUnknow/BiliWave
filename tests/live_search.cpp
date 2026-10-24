#include "searchservice.h"
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QTimer>
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    const auto args = app.arguments();
    if (args.size() < 3)
        return 2;
    const auto query = args[1];
    const int page = args.size() > 3 ? args[3].toInt() : 1;
    QSettings settings(QDir::current().filePath(".cache/search-probe.ini"), QSettings::IniFormat);
    SearchService service(&settings);
    QJsonObject report{{"query", query}};
    auto finish = [&](int code) {
        QFile file(args[2]);
        if (file.open(QIODevice::WriteOnly))
            file.write(QJsonDocument(report).toJson());
        app.exit(code);
    };
    QObject::connect(&service, &SearchService::message, &app,
                     [&](const QString &text) { report["message"] = text; });
    QObject::connect(&service, &SearchService::results, &app,
                     [&](const auto &items, int number, bool hasNext, bool video) {
                         QJsonArray array;
                         for (const auto &item : items)
                             array << QJsonObject::fromVariantMap(item);
                         report["items"] = array;
                         report["page"] = number;
                         report["hasNext"] = hasNext;
                         report["video"] = video;
                         report["success"] = !items.isEmpty();
                         QTimer::singleShot(0, &app, [&] { finish(0); });
                     });
    QTimer::singleShot(0, &app, [&] { service.search(query, page); });
    QTimer::singleShot(30000, &app, [&] {
        report["success"] = false;
        finish(1);
    });
    return app.exec();
}
