#include "onlinelyrics.h"
#include <QJsonArray>
#include <QJsonObject>
#include <algorithm>
#include <functional>
#include <QJsonDocument>
#include <QTcpServer>
#include <QTcpSocket>
#include <QUrlQuery>
#include <QtTest>

class OnlineLyricsTests : public QObject {
    Q_OBJECT
    static QVariantMap song(QString name = "Song", QString artist = "Singer", int duration = 200) {
        return {{"title", name}, {"artist", artist}, {"duration", duration * 1000}};
    }
    static QVariantMap candidate(QString name = "Song", QString artist = "Singer", int duration = 200) {
        return {{"id", 17},
                {"trackName", name},
                {"artistName", artist},
                {"duration", duration},
                {"syncedLyrics", "[00:01.00]Fixture lyric"}};
    }
    struct Server {
        QTcpServer server;
        QList<QUrl> requests;
        int status = 200;
        std::function<QByteArray(const QUrl &)> response;
        Server() {
            QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
                auto socket = server.nextPendingConnection();
                QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    auto bytes = socket->property("request").toByteArray() + socket->readAll();
                    socket->setProperty("request", bytes);
                    if (!bytes.contains("\r\n\r\n") || socket->property("handled").toBool())
                        return;
                    socket->setProperty("handled", true);
                    QUrl url("http://localhost" + QString::fromUtf8(bytes.split(' ').value(1)));
                    requests << url;
                    const auto body = response ? response(url) : QByteArray("[]");
                    socket->write(
                        "HTTP/1.1 " + QByteArray::number(status) +
                        " Test\r\nContent-Type: application/json\r\nRetry-After: 2\r\nContent-Length: " +
                        QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
            });
        }
        QUrl api() const {
            return QUrl(QString("http://127.0.0.1:%1/api/").arg(server.serverPort()));
        }
    };
  private slots:
    void titleCleaningAndFallbackPlan() {
        QCOMPARE(OnlineLyricsSearch::cleanTitle("【Hi-Res无损音质】《我本将心向明月》- 演唱者"),
                 QString("我本将心向明月"));
        QCOMPARE(OnlineLyricsSearch::cleanTitle("Мой мармеладный"), QString("Мой мармеладный"));
        const auto urls = OnlineLyricsSearch::plan(QUrl("https://lrclib.net/api/"), song());
        QVERIFY(urls.size() >= 4 && urls.size() <= 5);
        QCOMPARE(urls.first().path(), QString("/api/get"));
        QVERIFY(std::any_of(urls.begin(), urls.end(), [](const QUrl &url) {
            const QUrlQuery q(url);
            return q.hasQueryItem("track_name") && !q.hasQueryItem("artist_name");
        }));
        const auto direct =
            OnlineLyricsSearch::plan(QUrl("https://lrclib.net/api/"), song(), "https://lrclib.net/tracks/42");
        QCOMPARE(direct.size(), 1);
        QCOMPARE(direct[0].path(), QString("/api/get/42"));
        QVERIFY(
            OnlineLyricsSearch::plan(QUrl("https://lrclib.net/api/"), song(), "https://example.com/tracks/42")
                .isEmpty());
    }
    void rankingUsesIdentityBeforeDurationAndDeduplicates() {
        auto good = candidate();
        auto wrong = candidate("Other song", "Singer", 200);
        wrong["id"] = 18;
        auto close = candidate("Song", "Other singer", 199);
        close["id"] = 19;
        auto results = OnlineLyricsSearch::ranked(song(), {wrong, close, good, good});
        QCOMPARE(results.size(), 3);
        QCOMPARE(results.first().value("id").toInt(), 17);
        QVERIFY(results.first().value("automatic").toBool());
        QVERIFY(!results.last().value("automatic").toBool());
        auto noDuration = OnlineLyricsSearch::ranked(song("Song", "Singer", 0), {good});
        QVERIFY(!noDuration.first().value("automatic").toBool());
        QVERIFY(!noDuration.first().value("difference").isValid());
    }
    void networkFallsBackAndMergesCandidates() {
        Server server;
        QVERIFY(server.server.listen(QHostAddress::LocalHost));
        server.response = [](const QUrl &url) {
            const QUrlQuery q(url);
            if (url.path().endsWith("get"))
                return QByteArray("{}");
            if (q.hasQueryItem("artist_name"))
                return QByteArray("[]");
            return QJsonDocument(QJsonArray{QJsonObject::fromVariantMap(candidate())}).toJson();
        };
        OnlineLyricsSearch search(nullptr, server.api());
        QSignalSpy done(&search, &OnlineLyricsSearch::finished);
        search.search(song());
        QVERIFY(done.wait(5000));
        QCOMPARE(done.size(), 1);
        const auto options = qvariant_cast<QList<QVariantMap>>(done.first()[0]);
        QCOMPARE(options.size(), 1);
        QVERIFY(server.requests.size() >= 4);
        QVERIFY(done.first()[1].toString().isEmpty());
    }
    void rateLimitStopsAndHonorsCooldown() {
        Server server;
        QVERIFY(server.server.listen(QHostAddress::LocalHost));
        server.status = 429;
        OnlineLyricsSearch search(nullptr, server.api());
        QSignalSpy done(&search, &OnlineLyricsSearch::finished);
        search.search(song());
        QVERIFY(done.wait(3000));
        QCOMPARE(server.requests.size(), 1);
        QVERIFY(done.last()[1].toString().contains("2"));
        search.search(song());
        QVERIFY(done.wait(1000));
        QCOMPARE(server.requests.size(), 1);
    }
    void temporaryBusyResponseRetriesAfterRequestedDelay() {
        Server server;
        QVERIFY(server.server.listen(QHostAddress::LocalHost));
        server.response = [&server](const QUrl &) {
            server.status = server.requests.size() == 1 ? 503 : 200;
            return QJsonDocument(QJsonArray{QJsonObject::fromVariantMap(candidate())}).toJson();
        };
        OnlineLyricsSearch search(nullptr, server.api());
        QSignalSpy done(&search, &OnlineLyricsSearch::finished);
        QElapsedTimer elapsed;
        elapsed.start();
        search.search(song());
        QVERIFY(done.wait(6000));
        QVERIFY(elapsed.elapsed() >= 1900);
        QVERIFY(server.requests.size() > 1);
        QVERIFY(server.requests[1].path().endsWith("/search"));
        QCOMPARE(qvariant_cast<QList<QVariantMap>>(done.first()[0]).size(), 1);
        QVERIFY(done.first()[1].toString().isEmpty());
    }
    void canceledResultsNeverFinishAndDirectUrlIsSingleRequest() {
        Server server;
        QVERIFY(server.server.listen(QHostAddress::LocalHost));
        server.response = [](const QUrl &) {
            return QJsonDocument(QJsonObject::fromVariantMap(candidate())).toJson();
        };
        OnlineLyricsSearch search(nullptr, server.api());
        QSignalSpy done(&search, &OnlineLyricsSearch::finished);
        search.search(song());
        search.cancel();
        QTest::qWait(150);
        QCOMPARE(done.size(), 0);
        search.search(song(), "https://lrclib.net/api/get/17");
        QVERIFY(done.wait(3000));
        QCOMPARE(qvariant_cast<QList<QVariantMap>>(done.first()[0]).size(), 1);
        QCOMPARE(server.requests.last().path(), QString("/api/get/17"));
    }
};
QTEST_MAIN(OnlineLyricsTests)
#include "online_lyrics_tests.moc"
