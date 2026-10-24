#include "desktopintegration.h"
#include "window.h"
#include <QApplication>
#include <QCommandLineParser>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QTimer>
#include <memory>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    app.setApplicationName(recognitionFeaturesEnabled() ? "BiliWave" : "BiliWave Lite");
    app.setOrganizationName("BiliWave");
    app.setApplicationVersion(BILI_VERSION);
    app.setStyle("Fusion");
    app.setWindowIcon(QIcon(":/biliwave/icon.png"));
    QCommandLineParser parser;
    parser.setApplicationDescription("BiliWave desktop audio library");
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({"data", "Portable data directory", "directory"});
    parser.addOption({"capture", "Capture main window and overlays, then exit", "directory"});
    parser.addOption({"capture-delay", "Milliseconds before capturing the UI", "milliseconds", "1800"});
    parser.addOption({"search", "Open the search module with a keyword or Bilibili video", "query"});
    parser.addOption({"import", "Import a local file or Bilibili URL", "input"});
    parser.addOption({"startup", "Start in the system tray"});
    if (lyricFeaturesEnabled()) {
        parser.addOption({"fetch-lyrics", "Fetch lyrics for track ID", "id"});
    }
    parser.addOption(
        {"smoke", "Play first imported/existing track, check decoding and seeking, then exit", "report"});
    parser.process(app);
    const bool fetchLyrics = lyricFeaturesEnabled() && parser.isSet("fetch-lyrics");
    const auto directory = parser.isSet("data") ? QDir(parser.value("data")).absolutePath()
                                                : QDir(resourceRoot()).filePath("data");
    DesktopIntegration::ApplicationInstance instance;
    if (!parser.isSet("capture") && !parser.isSet("smoke") && !fetchLyrics) {
        QString error;
        const auto claimed = instance.claim(directory,
                                            {{"startup", parser.isSet("startup")},
                                             {"search", parser.value("search")},
                                             {"import", parser.value("import")}},
                                            &error);
        if (claimed == DesktopIntegration::ApplicationInstance::Notified)
            return 0;
        if (claimed == DesktopIntegration::ApplicationInstance::Failed) {
            QMessageBox::critical(nullptr, "无法启动 BiliWave", error);
            return 1;
        }
    }
    QDir().mkpath(directory);
    QSettings settings(QDir(directory).filePath("settings.ini"), QSettings::IniFormat);
    if (!lyricFeaturesEnabled() || parser.isSet("smoke") || parser.isSet("capture") || fetchLyrics) {
        settings.setValue("lyrics/autoFetch", false);
        settings.setValue("lyrics/matchAfterDownload", false);
    }
    Library library(directory);
    if (!library.isOpen()) {
        QMessageBox::critical(nullptr, "无法打开歌曲库", library.error());
        return 1;
    }
    MainWindow window(&library, &settings);
    QObject::connect(&instance, &DesktopIntegration::ApplicationInstance::commandRequested, &window,
                     [&window](const QJsonObject &request) {
                         if (request["startup"].toBool())
                             return;
                         window.showMainWindow();
                         const auto query = request["search"].toString();
                         if (!query.isEmpty()) {
                             window.navigate(0);
                             window.findChild<QLineEdit *>("onlineSearchInput")->setText(query);
                             window.findChild<QPushButton *>("onlineSearchButton")->click();
                         }
                         const auto input = request["import"].toString();
                         if (!input.isEmpty())
                             window.imports()->enqueue(input);
                     });
    if (!parser.isSet("startup") || parser.isSet("capture") || parser.isSet("smoke") || !window.hideToTray())
        window.show();
    if (parser.isSet("search"))
        QTimer::singleShot(0, &window, [&] {
            window.navigate(0);
            window.findChild<QLineEdit *>("onlineSearchInput")->setText(parser.value("search"));
            window.findChild<QPushButton *>("onlineSearchButton")->click();
        });
    if (parser.isSet("capture"))
        QTimer::singleShot(qMax(0, parser.value("capture-delay").toInt()), &window, [&] {
            QDir out(parser.value("capture"));
            QDir().mkpath(out.absolutePath());
            window.grab().save(out.filePath("library.png"));
            window.floatBar()->grab().save(out.filePath("float-bar.png"));
            window.overlay()->grab().save(out.filePath("lyrics.png"));
            window.exitApplication();
        });
    if (fetchLyrics) {
        QObject::connect(window.lyrics(), &LyricService::candidates, &window,
                         [&](qint64, const QList<QVariantMap> &items) {
                             QFile log(QDir(directory).filePath("online-candidates.json"));
                             if (log.open(QIODevice::WriteOnly)) {
                                 QJsonArray arr;
                                 for (const auto &item : items)
                                     arr << QJsonObject::fromVariantMap(item);
                                 log.write(QJsonDocument(arr).toJson());
                             }
                         });
        QTimer::singleShot(300, &window,
                           [&] { window.lyrics()->fetch(parser.value("fetch-lyrics").toLongLong(), true); });
    }
    if (parser.isSet("smoke")) {
        auto report = std::make_shared<QJsonObject>();
        auto started = std::make_shared<bool>(false);
        auto seeking = std::make_shared<bool>(false);
        (*report)["decoded"] = false;
        (*report)["seeked"] = false;
        auto finishTimer = new QTimer(&window);
        finishTimer->setSingleShot(true);
        auto start = [&window, &library, started, finishTimer, report] {
            if (*started)
                return;
            const auto tracks = library.tracks();
            if (tracks.isEmpty())
                return;
            *started = true;
            window.player()->setQueue({tracks.first().value("id").toLongLong()});
            (*report)["track"] = QJsonObject::fromVariantMap(tracks.first());
            finishTimer->start(8000);
        };
        QObject::connect(window.imports(), &ImportService::imported, &window, [start](qint64) { start(); });
        QObject::connect(window.player()->media(), &QMediaPlayer::positionChanged, &window,
                         [&window, report, seeking](qint64 pos) {
                             if (pos > 100)
                                 (*report)["decoded"] = true;
                             if (!*seeking && pos > 200 && window.player()->media()->duration() > 1000) {
                                 *seeking = true;
                                 window.player()->media()->setPosition(window.player()->media()->duration() /
                                                                       2);
                             } else if (*seeking && pos >= window.player()->media()->duration() / 2)
                                 (*report)["seeked"] = true;
                         });
        QObject::connect(window.player(), &Player::failure, &window,
                         [report](const QString &e) { (*report)["error"] = e; });
        QTimer::singleShot(500, &window, start);
        auto finish = [&, report] {
            (*report)["tracks"] = int(library.tracks().size());
            (*report)["duration"] = window.player()->media()->duration();
            QFile output(parser.value("smoke"));
            if (output.open(QIODevice::WriteOnly))
                output.write(QJsonDocument(*report).toJson());
            const bool ok = report->value("decoded").toBool() && report->value("seeked").toBool();
            window.exitApplication();
            app.exit(ok ? 0 : 2);
        };
        QObject::connect(finishTimer, &QTimer::timeout, &window, finish);
        QObject::connect(window.imports(), &ImportService::status, &window,
                         [finishTimer, report](const QString &, const QString &, const QString &state) {
                             (*report)["import_status"] = state;
                             if (state.startsWith(QStringLiteral("失败")))
                                 finishTimer->start(200);
                         });
        QTimer::singleShot(240000, &window, finish);
    }
    if (parser.isSet("import"))
        QTimer::singleShot(0, &window, [&] { window.imports()->enqueue(parser.value("import")); });
    return app.exec();
}
