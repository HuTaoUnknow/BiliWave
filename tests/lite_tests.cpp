#include "lyricworkflow.h"
#include "window.h"
#include <QDir>
#include <QFile>
#include <QMenu>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

class LiteTests : public QObject {
    Q_OBJECT
  private slots:
    void onlineOnlyWorkbenchAndNormalMusicFlow() {
        QVERIFY(lyricFeaturesEnabled());
        QVERIFY(!recognitionFeaturesEnabled());
        QTemporaryDir dir(QDir::current().filePath("lite-ui-test-XXXXXX"));
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        settings.setValue("lyrics/autoFetch", true);
        settings.setValue("windows/showLyrics", false);
        settings.setValue("windows/showFloat", false);
        MainWindow window(&library, &settings);
        window.show();
        window.navigate(0);
        QTest::qWait(80);
        QVERIFY(window.findChild<QComboBox *>("lyricTrackSelector"));
        QVERIFY(window.findChild<QPushButton *>("matchLyrics"));
        QVERIFY(window.findChild<QPushButton *>("sidebarLyrics")->isVisible());
        QVERIFY(window.findChild<QPushButton *>("navWorkbench")->isVisible());
        window.navigate(3);
        QVERIFY(window.findChild<QPushButton *>("lyricCategory0")->isVisible());
        QVERIFY(!window.findChild<QPushButton *>("lyricCategory1"));
        QVERIFY(!window.findChild<QPushButton *>("localLyricOption0")->isVisible());
        QVERIFY(!window.findChild<QPushButton *>("localLyricOption1")->isVisible());
        auto queue = window.findChild<LyricQueue *>();
        QVERIFY(queue && !queue->isVisible());
        QCOMPARE(queue->enqueue(1, {{"operation", "ocr"}}), -1);
        LyricJob job(&settings);
        QVERIFY(!job.start({{"operation", "align"}}));
        QVERIFY(!window.lyrics()->importFile(1, dir.filePath("test.lrc")));
        window.navigate(4);
        QVERIFY(!window.findChild<QLineEdit *>("settingsPath_lyrics_alignmentModel"));
        window.findChild<QPushButton *>("saveSettings")->click();
        QCOMPARE(settings.value("lyrics/source").toString(), QString("online"));
        window.navigate(0);
        const QVariantMap result{{"title", "精简版试听"},
                                 {"duration", 12000},
                                 {"bvid", "BV13x41117TL"},
                                 {"url", "https://www.bilibili.com/video/BV13x41117TL?p=1"}};
        window.showSearchResults({result}, 1, false, true);
        auto results = window.findChild<QTreeWidget *>("searchResults");
        QTest::mouseClick(results->viewport(), Qt::LeftButton, Qt::NoModifier,
                          results->visualItemRect(results->topLevelItem(0)).center());
        QVERIFY(library.stagedTracks().isEmpty());
        window.findChild<QPushButton *>("collectSearchUrl")->click();
        QCOMPARE(library.tracks().size(), 1);
        const auto id = library.tracks().first().value("id").toLongLong();
        auto playlist = library.createPlaylist("精简版歌单");
        QVERIFY(library.addToPlaylist(playlist, {id}));
        QCOMPARE(library.tracks(playlist).size(), 1);
        const auto audio = dir.filePath("local.flac");
        QProcess ffmpeg;
        ffmpeg.start(QDir(resourceRoot()).filePath("tools/ffmpeg.exe"),
                     {"-v", "error", "-n", "-f", "lavfi", "-i", "sine=frequency=440:duration=12", audio});
        QVERIFY(ffmpeg.waitForFinished(15000));
        QCOMPARE(ffmpeg.exitCode(), 0);
        QSignalSpy imported(window.imports(), &ImportService::imported);
        window.imports()->enqueue(audio);
        QVERIFY(imported.wait(15000));
        QCOMPARE(library.tracks().size(), 2);
        QVERIFY(library.stagedTracks().isEmpty());
        const auto local = imported.first().first().toLongLong();
        auto selector = window.findChild<QComboBox *>("lyricTrackSelector");
        QVERIFY(selector->findData(local) >= 0);
        QVERIFY(selector->findData(id) < 0); // URL-only songs still do not enter the workbench.
        selector->setCurrentIndex(selector->findData(local));
        window.lyrics()->cancel(); // This fixture supplies candidates without using the network.
        const QVariantMap synced{{"id", 17}, {"trackName", "在线版歌词验证"}, {"artistName", "测试歌手"},
            {"syncedLyrics", "[00:01.00]第一句\n[00:04.00]下一句"}, {"difference", 0},
            {"sourceUrl", "https://lrclib.net/api/get/17"}};
        window.lyrics()->candidates(local, {synced});
        auto use = window.findChild<QPushButton *>("useLyricCandidate");
        QVERIFY(use->isEnabled());
        use->click();
        QCOMPARE(library.track(local).value("lrc").toString(), synced.value("syncedLyrics").toString());
        QVERIFY(!window.findChild<QPushButton *>("realignLyricCandidate")->isVisible());
        window.lyrics()->candidates(local, {{{"trackName", "纯文字"}, {"plainLyrics", "没有时间轴"}}});
        QVERIFY(!use->isEnabled());
        QCOMPARE(queue->count(), 0);
        window.player()->setQueue({local}, local, true);
        QTRY_VERIFY_WITH_TIMEOUT(window.player()->media()->duration() > 11000, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(window.player()->media()->position() > 100, 15000);
        window.player()->media()->setPosition(5000);
        QTRY_VERIFY_WITH_TIMEOUT(window.player()->media()->position() >= 4900, 5000);
        QVERIFY(!window.overlay()->isVisible());
        QVERIFY(!window.lyrics()->matching());
        QDir screenshots(resourceRoot() + "/docs/screenshots/lite");
        screenshots.mkpath(".");
        for (int page : {0, 1, 2, 3, 4}) {
            window.navigate(page);
            QCoreApplication::processEvents();
            QVERIFY(window.grab().save(screenshots.filePath(QString("page-%1.png").arg(page))));
        }
        window.player()->media()->stop();
    }
};
QTEST_MAIN(LiteTests)
#include "lite_tests.moc"
