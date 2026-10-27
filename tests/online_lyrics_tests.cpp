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
        QCOMPARE(OnlineLyricsSearch::cleanTitle("《明日方舟》EP - 无名策"), QString("无名策"));
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
    void fiveAtATimeUsesCacheAndDoesNotContactBackupsUntilExhausted() {
        Server primary, backup;
        QVERIFY(primary.server.listen(QHostAddress::LocalHost));
        QVERIFY(backup.server.listen(QHostAddress::LocalHost));
        primary.response = [](const QUrl &) {
            QJsonArray rows;
            for (int i = 0; i < 12; ++i) {
                auto c = candidate(); c["id"] = i + 1;
                c["syncedLyrics"] = QString("[00:01.00]Fixture %1").arg(i);
                rows << QJsonObject::fromVariantMap(c);
            }
            return QJsonDocument(rows).toJson();
        };
        LyricsSourcesSearch search(nullptr, {{"lrclib", primary.api()}, {"qq", backup.api()}});
        QSignalSpy done(&search, &LyricsSourcesSearch::finished);
        QSet<QString> ids;
        for (const auto count : {5, 5, 2}) {
            search.search(song(), {}, {"lrclib", "qq"});
            QVERIFY(done.wait(3000));
            const auto rows = qvariant_cast<QList<QVariantMap>>(done.last()[0]);
            QCOMPARE(rows.size(), count);
            for (const auto &row : rows) {
                QVERIFY(!ids.contains(row.value("id").toString()));
                ids.insert(row.value("id").toString());
            }
            QCOMPARE(primary.requests.size(), 1);
            QCOMPARE(backup.requests.size(), 0);
        }
        search.search(song(), {}, {"lrclib", "qq"});
        QVERIFY(done.wait(3000));
        QCOMPARE(backup.requests.size(), 1);
        QVERIFY(!search.hasMore());
        search.search(song(), {}, {"lrclib", "qq"});
        QVERIFY(done.wait(1000));
        QCOMPARE(backup.requests.size(), 1);
        QCOMPARE(primary.requests.size(), 1);
    }
    void noResultsFallsBackWithRequestSpacingAndRateLimitStopsEverything() {
        Server primary, backup;
        QVERIFY(primary.server.listen(QHostAddress::LocalHost));
        QVERIFY(backup.server.listen(QHostAddress::LocalHost));
        backup.response = [](const QUrl &) { return QJsonDocument(QJsonArray{QJsonObject::fromVariantMap(candidate())}).toJson(); };
        LyricsSourcesSearch search(nullptr, {{"netease", primary.api()}, {"lrclib", backup.api()}});
        QSignalSpy done(&search, &LyricsSourcesSearch::finished);
        QElapsedTimer elapsed; elapsed.start();
        search.search(song(), {}, {"netease", "lrclib"});
        QVERIFY(done.wait(5000));
        QCOMPARE(qvariant_cast<QList<QVariantMap>>(done.last()[0]).size(), 1);
        QVERIFY(elapsed.elapsed() >= 950);
        QCOMPARE(primary.requests.size(), 1);
        QCOMPARE(backup.requests.size(), 1);
        Server limited, unused;
        QVERIFY(limited.server.listen(QHostAddress::LocalHost));
        QVERIFY(unused.server.listen(QHostAddress::LocalHost));
        limited.status = 429;
        LyricsSourcesSearch blocked(nullptr, {{"lrclib", limited.api()}, {"qq", unused.api()}});
        QSignalSpy stopped(&blocked, &LyricsSourcesSearch::finished);
        blocked.search(song(), {}, {"lrclib", "qq"});
        QVERIFY(stopped.wait(3000));
        QVERIFY(stopped.last()[1].toString().contains("60"));
        blocked.search(song(), {}, {"lrclib", "qq"});
        QVERIFY(stopped.wait(1000));
        QCOMPARE(limited.requests.size(), 1);
        QCOMPARE(unused.requests.size(), 0);
    }
    void neteasePagesFetchOnlyFiveLyricsAndRetainProvider() {
        Server server;
        QVERIFY(server.server.listen(QHostAddress::LocalHost));
        server.response = [](const QUrl &url) {
            if (url.path().endsWith("lyric")) {
                const auto id = QUrlQuery(url).queryItemValue("id");
                return QJsonDocument(QJsonObject{{"code", 200}, {"lrc", QJsonObject{{"lyric", "[00:01.00]Fixture " + id}}}}).toJson();
            }
            QJsonArray rows;
            const int start = QUrlQuery(url).queryItemValue("offset").toInt();
            for (int i = start; i < start + (start == 0 ? 5 : 2); ++i)
                rows << QJsonObject{{"id", i + 100}, {"name", "Song"}, {"duration", 200000}, {"artists", QJsonArray{QJsonObject{{"name", "Singer"}}}}};
            return QJsonDocument(QJsonObject{{"code", 200}, {"result", QJsonObject{{"songs", rows}}}}).toJson();
        };
        LyricsSourcesSearch search(nullptr, {{"netease", server.api()}});
        QSignalSpy done(&search, &LyricsSourcesSearch::finished);
        search.search(song(), {}, {"netease"});
        QVERIFY(done.wait(10000));
        auto rows = qvariant_cast<QList<QVariantMap>>(done.last()[0]);
        QCOMPARE(rows.size(), 5);
        QCOMPARE(server.requests.size(), 6);
        QCOMPARE(rows.first().value("provider").toString(), QString("网易云音乐"));
        QVERIFY(rows.first().value("sourceUrl").toString().contains("music.163.com"));
        search.search(song(), {}, {"netease"});
        QVERIFY(done.wait(7000));
        rows = qvariant_cast<QList<QVariantMap>>(done.last()[0]);
        QCOMPARE(rows.size(), 2);
        QCOMPARE(server.requests.size(), 9);
        QVERIFY(!search.hasMore());
    }
    void sirenCatalogCachesAndBudgetResumesWithoutRepeatingDetailRequest() {
        Server server;
        QVERIFY(server.server.listen(QHostAddress::LocalHost));
        server.response = [&server](const QUrl &url) {
            if (url.path().endsWith("songs")) {
                QJsonArray songs;
                for (int i = 1; i <= 5; ++i) songs << QJsonObject{{"cid", QString::number(i)}, {"name", "Song"}, {"artists", QJsonArray{"Singer"}}};
                return QJsonDocument(QJsonObject{{"code", 0}, {"data", QJsonObject{{"list", songs}}}}).toJson();
            }
            if (url.path().contains("/song/")) {
                const auto id = url.path().split('/').last();
                return QJsonDocument(QJsonObject{{"code", 0}, {"data", QJsonObject{{"name", "Song"}, {"artists", QJsonArray{"Singer"}}, {"lyricUrl", server.api().resolved(QUrl("lyrics/" + id)).toString()}}}}).toJson();
            }
            return QByteArray("[00:01.00]Fixture ") + url.path().split('/').last().toUtf8();
        };
        LyricsSourcesSearch search(nullptr, {{"msr", server.api()}});
        QSignalSpy done(&search, &LyricsSourcesSearch::finished);
        search.search(song(), {}, {"msr"});
        QVERIFY(done.wait(12000));
        QCOMPARE(search.statistics().value("requests").toInt(), 8);
        QCOMPARE(qvariant_cast<QList<QVariantMap>>(done.last()[0]).size(), 3);
        search.search(song(), {}, {"msr"});
        QVERIFY(done.wait(6000));
        const auto rows = qvariant_cast<QList<QVariantMap>>(done.last()[0]);
        QCOMPARE(rows.size(), 2);
        QCOMPARE(search.statistics().value("requests").toInt(), 3);
        QVERIFY(!rows.first().contains("_lyricUrl"));
        QCOMPARE(server.requests.size(), 11);
        QVERIFY(!search.hasMore());
        search.search(song("Unrelated"), {}, {"msr"});
        QVERIFY(done.wait(1000));
        QCOMPARE(server.requests.size(), 11); // cached catalog, no matching title
    }
};
QTEST_MAIN(OnlineLyricsTests)
#include "online_lyrics_tests.moc"
