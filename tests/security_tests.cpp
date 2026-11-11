#include "bilihosts.h"
#include "searchservice.h"
#include "wbi.h"
#include <QJsonArray>
#include <QJsonObject>
#include <QNetworkProxy>
#include <QSettings>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>

namespace {
const QString kImg = "7cd084941338484aae1ad9425b84077c";
const QString kSub = "4932caff0ff746eab6f01bf08b70ac45";
const QString kMixin = "ea1db124af3c7062474693fa704f4ff8";
const QByteArray kNav =
    "{\"code\":-101,\"message\":\"账号未登录\",\"ttl\":1,\"data\":{\"isLogin\":false,\"wbi_img\":{"
    "\"img_url\":\"https://i0.hdslb.com/bfs/wbi/7cd084941338484aae1ad9425b84077c.png\","
    "\"sub_url\":\"https://i0.hdslb.com/bfs/wbi/4932caff0ff746eab6f01bf08b70ac45.png\"}}}";
const QByteArray kNavNext =
    "{\"code\":0,\"data\":{\"wbi_img\":{"
    "\"img_url\":\"https://i0.hdslb.com/bfs/wbi/aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa.png\","
    "\"sub_url\":\"https://i0.hdslb.com/bfs/wbi/bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb.png\"}}}";

QByteArray searchBody() {
    return "{\"code\":0,\"message\":\"OK\",\"data\":{\"numPages\":1,\"pagesize\":20,\"result\":[{"
           "\"bvid\":\"BV1xx411c7mD\",\"title\":\"歌曲\",\"duration\":\"1:02\","
           "\"pic\":\"//i0.hdslb.com/bfs/archive/cover.jpg\",\"author\":\"UP\"}]}}";
}
QByteArray videoBody() {
    return "{\"code\":0,\"data\":{\"bvid\":\"BV1xx411c7mD\",\"title\":\"分P\","
           "\"pic\":\"https://i0.hdslb.com/bfs/archive/cover.jpg\",\"owner\":{\"name\":\"UP\"},"
           "\"pages\":[{\"page\":1,\"part\":\"第一P\",\"duration\":60,"
           "\"first_frame\":\"https://evil.example/frame.jpg\"}]}}";
}

struct Fixture {
    QTcpServer server;
    QStringList targets;
    int navCount = 0;
    std::function<QByteArray(const QString &)> response;
    Fixture() {
        QObject::connect(&server, &QTcpServer::newConnection, &server, [this] {
            auto socket = server.nextPendingConnection();
            QObject::connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            QObject::connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                auto bytes = socket->property("request").toByteArray() + socket->readAll();
                socket->setProperty("request", bytes);
                if (!bytes.contains("\r\n\r\n") || socket->property("handled").toBool())
                    return;
                socket->setProperty("handled", true);
                const auto target = QString::fromUtf8(bytes.split(' ').value(1));
                targets << target;
                if (target.contains("/x/web-interface/nav"))
                    ++navCount;
                const auto body = response ? response(target) : QByteArray("{}");
                socket->write("HTTP/1.1 200 OK\r\nContent-Type: application/json\r\nContent-Length: " +
                              QByteArray::number(body.size()) + "\r\nConnection: close\r\n\r\n" + body);
                socket->disconnectFromHost();
            });
        });
    }
    bool listen() {
        return server.listen(QHostAddress::LocalHost);
    }
    QUrl root() const {
        return QUrl(QString("http://127.0.0.1:%1").arg(server.serverPort()));
    }
    QUrl nav() const {
        return QUrl(QString("http://127.0.0.1:%1/x/web-interface/nav").arg(server.serverPort()));
    }
};

QList<QPair<QString, QString>> searchParams() {
    return {{"search_type", "video"},
            {"keyword", "测试"},
            {"page", "1"},
            {"page_size", "20"},
            {"highlight", "0"}};
}
} // namespace

class SecurityTests : public QObject {
    Q_OBJECT
  private slots:
    void init() {
        QNetworkProxy::setApplicationProxy(QNetworkProxy(QNetworkProxy::NoProxy));
    }
    void mixinAndSignatureMatchPublishedVector() {
        QCOMPARE(WbiSigner::mixinKey(kImg, kSub), kMixin);
        QCOMPARE(
            WbiSigner::keyFromUrl("https://i0.hdslb.com/bfs/wbi/7cd084941338484aae1ad9425b84077c.png?x=1"),
            kImg);
        QCOMPARE(
            WbiSigner::signQuery({{"foo", "114"}, {"bar", "514"}, {"zab", "1919810"}, {"w_rid", "stale"}},
                                 kMixin, 1702204169),
            QString("bar=514&foo=114&wts=1702204169&zab=1919810&w_rid=8f6f2b5b3d485fe1886cec6a0be8c5d4"));
        QCOMPARE(WbiSigner::signQuery({{"foo", "one one four"}, {"bar", "五一四"}, {"baz", "1919810"}},
                                      kMixin, 1702204169),
                 QString("bar=%E4%BA%94%E4%B8%80%E5%9B%9B&baz=1919810&foo=one%20one%20four&wts=1702204169&"
                         "w_rid=04e50b58980e3e3cee8cbc0cc4c1c530"));
        QCOMPARE(WbiSigner::signQuery({{"foo", "114!'()*"}}, kMixin, 1702204169),
                 QString("foo=114&wts=1702204169&w_rid=da7cbb7b38c9206d34a42c8a7b3164e6"));
        QString img, sub, error;
        QVERIFY(WbiSigner::keysFromNav(kNav, &img, &sub, &error));
        QCOMPARE(img, kImg);
        QCOMPARE(sub, kSub);
        QVERIFY(!WbiSigner::keysFromNav("{}", &img, &sub, &error));
        QVERIFY(error.contains("签名口令"));
    }
    void cacheExpiresAtNextBeijingMidnight() {
        QNetworkAccessManager network;
        WbiSigner signer(&network);
        const qint64 fetched = 1702204169;
        signer.setKeysForTest(kImg, kSub, fetched);
        QCOMPARE(signer.expiresAt(), qint64(1702224000));
        QVERIFY(signer.cacheFresh(fetched));
        QVERIFY(signer.cacheFresh(1702223999));
        QVERIFY(!signer.cacheFresh(1702224000));
    }
    void signedSearchUsesFixtureAndRefreshesAfterExpiry() {
        Fixture server;
        QVERIFY(server.listen());
        server.response = [&server](const QString &target) {
            if (target.contains("/nav"))
                return server.navCount <= 1 ? kNav : kNavNext;
            if (target.contains("/wbi/search/type"))
                return searchBody();
            return QByteArray(R"({"code":-1,"message":"unexpected"})");
        };
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        SearchService service(&settings);
        service.setApiRootForTest(server.root());
        qint64 now = 1702204169;
        service.wbi().setNavUrlForTest(server.nav());
        service.wbi().setClockForTest([&now] { return now; });
        QList<QVariantMap> items;
        QObject::connect(&service, &SearchService::results, &service,
                         [&](const QList<QVariantMap> &rows, int, bool, bool) { items = rows; });
        service.search("测试", 1);
        QTRY_VERIFY_WITH_TIMEOUT(!items.isEmpty(), 3000);
        QCOMPARE(server.navCount, 1);
        QVERIFY(!server.targets.join("\n").contains("/bfs/wbi/"));
        const auto first = server.targets.last();
        QVERIFY(first.startsWith("/x/web-interface/wbi/search/type?"));
        QCOMPARE(first.mid(first.indexOf('?') + 1), WbiSigner::signQuery(searchParams(), kMixin, now));
        QCOMPARE(items.first().value("thumbnail").toString(),
                 QString("https://i0.hdslb.com/bfs/archive/cover.jpg"));
        items.clear();
        service.search("测试", 1);
        QTRY_VERIFY_WITH_TIMEOUT(!items.isEmpty(), 3000);
        QCOMPARE(server.navCount, 1);
        now += 48 * 3600;
        items.clear();
        service.search("测试", 1);
        QTRY_VERIFY_WITH_TIMEOUT(!items.isEmpty(), 3000);
        QCOMPARE(server.navCount, 2);
        const auto refreshed = server.targets.last();
        const auto nextMixin =
            WbiSigner::mixinKey(QString(32, QLatin1Char('a')), QString(32, QLatin1Char('b')));
        QCOMPARE(refreshed.mid(refreshed.indexOf('?') + 1),
                 WbiSigner::signQuery(searchParams(), nextMixin, now));
        QVERIFY(refreshed != first);
    }
    void rejectedSignatureRefreshesOnce() {
        Fixture server;
        QVERIFY(server.listen());
        int searches = 0;
        server.response = [&](const QString &target) {
            if (target.contains("/nav"))
                return kNav;
            ++searches;
            if (searches == 1)
                return QByteArray(R"({"code":0,"message":"0","ttl":1,"data":{"v_voucher":"voucher_test"}})");
            return videoBody();
        };
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        SearchService service(&settings);
        service.setApiRootForTest(server.root());
        service.wbi().setNavUrlForTest(server.nav());
        qint64 now = 1702204169;
        service.wbi().setClockForTest([&now] { return now; });
        QList<QVariantMap> items;
        bool video = false;
        QObject::connect(&service, &SearchService::results, &service,
                         [&](const QList<QVariantMap> &rows, int, bool, bool isVideo) {
                             items = rows;
                             video = isVideo;
                         });
        service.search("BV1xx411c7mD");
        QTRY_VERIFY_WITH_TIMEOUT(!items.isEmpty(), 3000);
        QCOMPARE(searches, 2);
        QCOMPARE(server.navCount, 2);
        QVERIFY(video);
        QCOMPARE(items.first().value("thumbnail").toString(),
                 QString("https://i0.hdslb.com/bfs/archive/cover.jpg"));
        QVERIFY(server.targets.filter("/x/web-interface/view").size() >= 2);
        QVERIFY(server.targets.last().contains("w_rid="));
        QVERIFY(server.targets.last().contains("bvid=BV1xx411c7mD"));
    }
    void missingNavReportsWithoutResults() {
        Fixture server;
        QVERIFY(server.listen());
        server.response = [](const QString &) { return QByteArray("not-json"); };
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        SearchService service(&settings);
        service.setApiRootForTest(server.root());
        service.wbi().setNavUrlForTest(server.nav());
        QStringList messages;
        int results = 0;
        QObject::connect(&service, &SearchService::message, &service,
                         [&](const QString &text) { messages << text; });
        QObject::connect(&service, &SearchService::results, &service, [&] { ++results; });
        service.search("测试");
        QTRY_VERIFY_WITH_TIMEOUT(std::any_of(messages.begin(), messages.end(),
                                             [](const QString &text) { return text.contains("签名"); }),
                                 3000);
        QCOMPARE(results, 0);
    }
    void coverAndAudioHostsStayOnBilibiliCdn() {
        QString reason;
        QCOMPARE(BiliHosts::coverUrl("//i0.hdslb.com/bfs/archive/cover.jpg"),
                 QString("https://i0.hdslb.com/bfs/archive/cover.jpg"));
        QCOMPARE(BiliHosts::coverUrl("http://i2.hdslb.com/bfs/archive/a.jpg"),
                 QString("https://i2.hdslb.com/bfs/archive/a.jpg"));
        QVERIFY(BiliHosts::coverUrl("https://archive.biliimg.com/bfs/archive/a.jpg")
                    .startsWith("https://archive.biliimg.com/"));
        QCOMPARE(BiliHosts::coverUrl("https://evil.example/a.jpg?token=secret", &reason), QString());
        QVERIFY(reason.contains("evil.example"));
        QVERIFY(!reason.contains("secret"));
        QCOMPARE(BiliHosts::coverUrl("https://i0.hdslb.com.evil.com/a.jpg"), QString());
        QCOMPARE(BiliHosts::coverUrl("file:///etc/passwd"), QString());
        QCOMPARE(BiliHosts::coverUrl("https://169.254.169.254/latest/meta-data"), QString());
        QCOMPARE(BiliHosts::coverUrl("https://user@i0.hdslb.com/a.jpg"), QString());
        const QUrl audio("https://upos-hz-mirrorakam.akamaized.net/upgcxcode/a.m4s?upsig=secret");
        QVERIFY(BiliHosts::audioUrl(audio));
        QVERIFY(BiliHosts::audioUrl(QUrl("https://upos-sz-mirrorcos.bilivideo.com/a.m4s")));
        QVERIFY(BiliHosts::audioUrl(QUrl("https://cn-hbxy-dx-v-01.bilivideo.com/a.m4s")));
        QVERIFY(BiliHosts::audioUrl(QUrl("https://xy.mcdn.bilivideo.cn/a.m4s")));
        QVERIFY(BiliHosts::audioUrl(QUrl("https://edge.mcdn.bilivideo.net/a.m4s")));
        QVERIFY(BiliHosts::audioUrl(QUrl("https://upos-sz-mirrorhw.acgvideo.com/a.m4s")));
        QVERIFY(!BiliHosts::audioUrl(QUrl("http://upos-sz-mirrorcos.bilivideo.com/a.m4s"), &reason));
        QVERIFY(reason.contains("HTTPS"));
        QVERIFY(!BiliHosts::audioUrl(QUrl("https://evil.example/a.m4s?upsig=secret"), &reason));
        QVERIFY(reason.contains("evil.example"));
        QVERIFY(!reason.contains("secret"));
        QVERIFY(!reason.contains("upsig"));
        QVERIFY(!BiliHosts::audioUrl(QUrl("https://video.akamaized.net/a.m4s")));
        QVERIFY(!BiliHosts::audioUrl(QUrl("https://www.bilibili.com/video/BV1xx411c7mD")));
        QVERIFY(!BiliHosts::audioUrl(QUrl("https://i0.hdslb.com/bfs/archive/a.jpg")));
        QVERIFY(!BiliHosts::audioUrl(QUrl("https://127.0.0.1/a.m4s"), &reason));
        QVERIFY(reason.contains("IP") || reason.contains("本机"));
        QVERIFY(!BiliHosts::audioUrl(QUrl("https://10.1.2.3/a.m4s")));
        QVERIFY(!BiliHosts::audioUrl(QUrl("https://192.168.0.2/a.m4s")));
        QVERIFY(!BiliHosts::audioUrl(QUrl("https://[::1]/a.m4s")));
        QVERIFY(
            !BiliHosts::audioUrl(QUrl("https://user:pass@upos-sz-mirrorcos.bilivideo.com/a.m4s"), &reason));
        QVERIFY(reason.contains("登录"));
        QVERIFY(!BiliHosts::audioUrl(QUrl("https://upos-sz-mirrorcos.bilivideo.com:8443/a.m4s"), &reason));
        QVERIFY(reason.contains("端口"));
        QVERIFY(!BiliHosts::audioUrl(QUrl("https://bilivideo.com.evil.example/a.m4s")));
        QVERIFY(!BiliHosts::audioUrl(QUrl("https://notupos.akamaized.net/a.m4s")));
        QJsonObject hostile{{"numPages", 1},
                            {"pagesize", 20},
                            {"result", QJsonArray{QJsonObject{{"bvid", "BV1xx411c7mD"},
                                                              {"title", "歌曲"},
                                                              {"duration", "0:01"},
                                                              {"pic", "https://cdn.example/cover.jpg"},
                                                              {"author", "UP"}}}}};
        QCOMPARE(SearchService::parseSearch(hostile, 1).items.first().value("thumbnail").toString(),
                 QString());
        QJsonObject video{{"bvid", "BV1xx411c7mD"},
                          {"title", "分 P"},
                          {"pic", "https://i1.hdslb.com/bfs/archive/ok.jpg"},
                          {"pages", QJsonArray{QJsonObject{{"page", 1},
                                                           {"part", "一"},
                                                           {"duration", 3},
                                                           {"first_frame", "http://127.0.0.1/frame.jpg"}}}}};
        QCOMPARE(SearchService::parseVideo(video).items.first().value("thumbnail").toString(),
                 QString("https://i1.hdslb.com/bfs/archive/ok.jpg"));
    }
};
QTEST_MAIN(SecurityTests)
#include "security_tests.moc"
