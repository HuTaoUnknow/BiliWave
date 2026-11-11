#include "streamservice.h"
#include <QDateTime>
#include <QTimeZone>
#include <QDir>
#include <QSettings>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>

class StreamCacheTests : public QObject {
    Q_OBJECT
  private slots:
    void expiryParsesDeadlineAndIgnoresDurations() {
        const auto fetched = QDateTime::fromSecsSinceEpoch(1'700'000'000, QTimeZone(QTimeZone::UTC));
        const QUrl deadline("https://upos.bilivideo.com/a.m4s?deadline=4102444800&e=token&expires=3600");
        QCOMPARE(ResolvedStreamCache::expiryFromUpstream(deadline, fetched),
                 QDateTime::fromSecsSinceEpoch(4'102'444'800, QTimeZone(QTimeZone::UTC)));
        const QUrl millis("https://cdn.example/a.m4s?expires=4102444800000&deadline=4200000000");
        QCOMPARE(ResolvedStreamCache::expiryFromUpstream(millis, fetched),
                 QDateTime::fromSecsSinceEpoch(4'102'444'800, QTimeZone(QTimeZone::UTC)));
        const QUrl bare("https://cdn.example/a.m4s");
        QCOMPARE(ResolvedStreamCache::expiryFromUpstream(bare, fetched), fetched.addSecs(20 * 60));
        const auto soon = fetched.addSecs(10);
        QVERIFY(!ResolvedStreamCache::stillValid(soon, fetched, 30));
        QVERIFY(ResolvedStreamCache::stillValid(fetched.addSecs(31), fetched, 30));
        QVERIFY(!ResolvedStreamCache::stillValid({}, fetched, 30));
        QVERIFY(ResolvedStreamCache::upstreamStatusExpired(401));
        QVERIFY(ResolvedStreamCache::upstreamStatusExpired(403));
        QVERIFY(ResolvedStreamCache::upstreamStatusExpired(410));
        QVERIFY(!ResolvedStreamCache::upstreamStatusExpired(200));
        QVERIFY(!ResolvedStreamCache::upstreamStatusExpired(404));

        ResolvedStreamCache cache;
        const ResolvedStreamCache::Entry entry{deadline, {{"Referer", "https://www.bilibili.com/"}},
                                               QDateTime::fromSecsSinceEpoch(4'102'444'800, QTimeZone(QTimeZone::UTC))};
        cache.store("https://www.bilibili.com/video/BV1xx411c7mD", entry);
        const auto hit = cache.takeIfValid("https://www.bilibili.com/video/BV1xx411c7mD", fetched);
        QVERIFY(hit.has_value());
        QCOMPARE(hit->upstream, deadline);
        cache.store("page", {bare, {}, fetched.addSecs(-1)});
        QVERIFY(!cache.takeIfValid("page", fetched));
        QVERIFY(!cache.takeIfValid("page", fetched));
        cache.store("page", entry);
        cache.invalidate("page");
        QVERIFY(!cache.takeIfValid("page", fetched));
    }
    void resolveReusesCachedUpstreamUntilItExpires() {
        QTemporaryDir dir(QDir::current().filePath("stream-cache-XXXXXX"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        settings.setValue("tools/yt-dlp", dir.filePath("missing-yt-dlp"));
        StreamService service(&settings);
        const QString source = "https://www.bilibili.com/video/BV1xx411c7mD?p=1";
        const QUrl upstream("https://upos-sz-mirrorcos.bilivideo.com/audio.m4s?deadline=4102444800");
        const auto expiry = QDateTime::fromSecsSinceEpoch(4'102'444'800, QTimeZone(QTimeZone::UTC));
        service.rememberResolved(source, upstream, {{"Referer", "https://www.bilibili.com/"}}, expiry);
        QSignalSpy ready(&service, &StreamService::ready);
        service.resolve(4, source);
        QCOMPARE(ready.size(), 1);
        QCOMPARE(ready.first().at(0).toULongLong(), 4ull);
        QVERIFY(ready.first().at(2).toString().isEmpty());
        QVERIFY(ready.first().at(1).toUrl().toString().startsWith("http://127.0.0.1:"));
        QVERIFY(ready.first().at(1).toUrl().path().endsWith("/audio.m4a"));
        QCOMPARE(service.upstream(), upstream);
        QCOMPARE(service.resolvedExpiry(), expiry);
        QVERIFY(!service.resolving());

        service.resolve(5, source);
        QCOMPARE(ready.size(), 2);
        QCOMPARE(ready.last().at(0).toULongLong(), 5ull);
        QVERIFY(ready.last().at(2).toString().isEmpty());
        QVERIFY(ready.last().at(1).toUrl() != ready.first().at(1).toUrl());
        QVERIFY(!service.resolving());

        service.rememberResolved(source, upstream, {}, QDateTime::currentDateTimeUtc().addSecs(-60));
        service.resolve(6, source);
        QTRY_VERIFY_WITH_TIMEOUT(ready.size() >= 3, 3000);
        bool sawResolver = false;
        for (int i = 2; i < ready.size(); ++i) {
            QCOMPARE(ready.at(i).at(0).toULongLong(), 6ull);
            if (ready.at(i).at(2).toString().contains("yt-dlp"))
                sawResolver = true;
        }
        QVERIFY(sawResolver);
        QVERIFY(!service.resolving());
    }
    void upstreamRejectionDropsTheCachedUrl() {
        QTemporaryDir dir(QDir::current().filePath("stream-reject-XXXXXX"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        settings.setValue("tools/yt-dlp", dir.filePath("missing-yt-dlp"));
        QTcpServer origin;
        QVERIFY(origin.listen(QHostAddress::LocalHost, 0));
        connect(&origin, &QTcpServer::newConnection, &origin, [&origin] {
            while (origin.hasPendingConnections()) {
                auto socket = origin.nextPendingConnection();
                connect(socket, &QTcpSocket::readyRead, socket, [socket] {
                    socket->readAll();
                    socket->write("HTTP/1.1 403 Forbidden\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
        StreamService service(&settings);
        const QString source = "https://www.bilibili.com/video/BV1xx411c7mD";
        const QUrl upstream(QString("http://127.0.0.1:%1/audio.m4s?deadline=4102444800").arg(origin.serverPort()));
        service.rememberResolved(source, upstream, {{"Referer", "https://www.bilibili.com/"}},
                                 QDateTime::fromSecsSinceEpoch(4'102'444'800, QTimeZone(QTimeZone::UTC)));
        QSignalSpy ready(&service, &StreamService::ready);
        service.resolve(1, source);
        QCOMPARE(ready.size(), 1);
        const auto playback = ready.first().at(1).toUrl();
        QSignalSpy rejected(&service, &StreamService::upstreamRejected);
        QTcpSocket client;
        client.connectToHost(playback.host(), quint16(playback.port()));
        QTRY_VERIFY(client.state() == QAbstractSocket::ConnectedState);
        const auto path = playback.path().toUtf8();
        client.write("GET " + path + " HTTP/1.1\r\nHost: 127.0.0.1\r\nConnection: close\r\n\r\n");
        QTRY_COMPARE_WITH_TIMEOUT(rejected.count(), 1, 3000);
        QCOMPARE(service.resolvedExpiry(), QDateTime::fromSecsSinceEpoch(0, QTimeZone(QTimeZone::UTC)));

        const auto before = ready.size();
        service.resolve(2, source);
        QTRY_VERIFY_WITH_TIMEOUT(ready.size() > before, 3000);
        QVERIFY(ready.last().at(2).toString().contains("yt-dlp") ||
                ready.last().at(2).toString().contains("无法获取"));
    }
};
QTEST_MAIN(StreamCacheTests)
#include "stream_cache_tests.moc"
