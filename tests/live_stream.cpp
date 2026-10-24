#include "library.h"
#include "player.h"
#include "services.h"
#include "streamservice.h"
#include <QApplication>
#include <QAudioBuffer>
#include <QAudioBufferOutput>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

int main(int argc, char **argv) {
    QApplication app(argc, argv);
    QDir work(QDir::current().filePath(".cache/stream-live"));
    work.mkpath(".");
    Library library(work.filePath("data"));
    QSettings settings(work.filePath("settings.ini"), QSettings::IniFormat);
    Player player(&library);
    StreamService stream(&settings);
    QAudioBufferOutput buffers;
    player.media()->setAudioBufferOutput(&buffers);
    player.audio()->setVolume(0);
    const auto id = library.addUrl({{"title", "URL 在线验证"},
                                    {"url", "https://www.bilibili.com/video/BV1bK411W797?p=2"},
                                    {"duration", 98000}});
    int resolutions = 0;
    bool decoded = false, seeked = false, restarted = false;
    QString failure;
    auto finish = [&](bool ok) {
        const auto t = library.track(id);
        QJsonObject result{{"success", ok},
                           {"decoded", decoded},
                           {"seeked", seeked},
                           {"fresh_resolutions", resolutions},
                           {"source", t.value("source").toString()},
                           {"storage", t.value("storage").toString()},
                           {"original", t.value("original").toString()},
                           {"duration", player.media()->duration()},
                           {"media_directory_created", QFileInfo::exists(work.filePath("data/media"))},
                           {"error", failure}};
        QFile file(QDir::current().filePath("docs/url-stream-live-result.json"));
        if (file.open(QIODevice::WriteOnly))
            file.write(QJsonDocument(result).toJson());
        player.setQueue({});
        stream.cancel();
        app.exit(ok ? 0 : 2);
    };
    QObject::connect(&player, &Player::streamRequested, &stream, [&](quint64 token, const QString &url) {
        ++resolutions;
        stream.resolve(token, url);
    });
    QObject::connect(&stream, &StreamService::ready, &player, &Player::acceptStream);
    QObject::connect(&player, &Player::failure, &app, [&](const QString &text) {
        failure = text;
        finish(false);
    });
    QObject::connect(&buffers, &QAudioBufferOutput::audioBufferReceived, &app,
                     [&](const QAudioBuffer &buffer) {
                         if (buffer.isValid() && buffer.sampleCount() > 0)
                             decoded = true;
                     });
    QObject::connect(player.media(), &QMediaPlayer::positionChanged, &app, [&](qint64 position) {
        if (decoded && !seeked && position > 300) {
            seeked = true;
            player.media()->setPosition(10000);
        } else if (seeked && !restarted && position > 10200) {
            restarted = true;
            player.jumpTo(id);
        } else if (restarted && resolutions >= 2 && position > 300 && position < 10000)
            finish(true);
    });
    QTimer::singleShot(90000, &app, [&] {
        failure = "timeout";
        finish(false);
    });
    QTimer::singleShot(0, &app, [&] {
        player.setMode(Player::Sequential);
        player.setQueue({id});
    });
    return app.exec();
}
