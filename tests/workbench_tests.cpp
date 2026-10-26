#include "lyricworkflow.h"
#include "window.h"
#include <QDialogButtonBox>
#include <QTemporaryDir>
#include <QtTest>

class WorkbenchTests : public QObject {
    Q_OBJECT
  private slots:
    void independentPersistentDraftsAndAtomicCommit() {
        QTemporaryDir dir(QDir::current().filePath("workbench-test-XXXXXX"));
        qint64 a, b;
        {
            Library library(dir.path());
            a = library.addTrack({{"title", "歌曲 A"}, {"artist", "歌手 A"}, {"path", "a.flac"}, {"staged", 1}});
            b = library.addTrack({{"title", "歌曲 B"}, {"artist", "歌手 B"}, {"path", "b.flac"}});
            library.setLyrics(b, "[00:01.000]乐库原词", "原来源");
            QVERIFY(library.setWorkbenchMetadata(a, "配置歌名", "配置歌手"));
            QVERIFY(library.setWorkbenchLyrics(a, "[00:02.000]第一版", "候选一"));
            QVERIFY(library.setWorkbenchLyrics(a, "[00:03.000]第二版", "候选二"));
            QCOMPARE(library.workbenchTrack(a).value("title").toString(), QString("配置歌名"));
            QCOMPARE(library.workbenchTrack(a).value("artist").toString(), QString("配置歌手"));
            QCOMPARE(library.track(a).value("title").toString(), QString("歌曲 A"));
            QVERIFY(library.track(a).value("lrc").toString().isEmpty());
            QVERIFY(library.setWorkbenchLyrics(b, "[00:05.000]草稿新词", "来源 B"));
            QVERIFY(library.setWorkbenchOffset(a, 400));
            QCOMPARE(library.track(b).value("lrc").toString(), QString("[00:01.000]乐库原词"));
        }
        {
            Library library(dir.path());
            QCOMPARE(library.workbenchTrack(a).value("artist").toString(), QString("配置歌手"));
            QCOMPARE(library.workbenchTrack(a).value("lrc").toString(), QString("[00:03.000]第二版"));
            QVERIFY(library.saveWorkbench(a));
            const auto saved = library.track(a);
            QCOMPARE(saved.value("title").toString(), QString("配置歌名"));
            QCOMPARE(saved.value("artist").toString(), QString("配置歌手"));
            QCOMPARE(saved.value("lyric_offset").toInt(), 400);
            QVERIFY(!saved.value("staged").toBool());
            QCOMPARE(library.track(b).value("lrc").toString(), QString("[00:01.000]乐库原词"));
            QCOMPARE(library.workbenchTrack(b).value("lrc").toString(), QString("[00:05.000]草稿新词"));
            QVERIFY(library.deleteTrack(b));
            QVERIFY(library.workbenchTrack(b).isEmpty());
        }
    }
    void urlQueueResultsNeverOverwriteMetadataOrAnotherSong() {
        QTemporaryDir dir(QDir::current().filePath("workbench-test-XXXXXX"));
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        settings.setValue("lyrics/autoFetch", false);
        settings.setValue("windows/showFloat", false);
        settings.setValue("windows/showLyrics", false);
        const auto a = library.addUrl({{"title", "原 URL 歌名"}, {"artist", "原歌手"},
                                      {"url", "https://www.bilibili.com/video/BV13x41117TL?p=1"}});
        const auto b = library.addTrack({{"title", "本地歌曲"}, {"artist", "本地歌手"}, {"path", "b.flac"}});
        MainWindow window(&library, &settings);
        window.loadLibraryTrack(a);
        auto title = window.findChild<QLineEdit *>("lyricSongTitle");
        auto artist = window.findChild<QLineEdit *>("lyricSongArtist");
        title->setFocus(); title->selectAll(); QTest::keyClicks(title, "URL title");
        artist->setFocus(); artist->selectAll(); QTest::keyClicks(artist, "URL artist");
        auto queue = window.findChild<LyricQueue *>();
        const auto apply = [&] {
            QTimer::singleShot(20, queue, [queue] {
                auto dialog = queue->findChild<QDialog *>("queuedLyricReview");
                QVERIFY(dialog);
                dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->click();
            });
            queue->findChild<QPushButton *>("reviewQueuedLyrics")->click();
        };
        queue->addOnlineResults(a, {{{"id", 1}, {"trackName", "候选的其他歌名"}, {"artistName", "其他歌手"},
                                     {"syncedLyrics", "[00:01.000]第一版愛與夢想"}}});
        apply();
        QCOMPARE(title->text(), QString("URL title"));
        QCOMPARE(artist->text(), QString("URL artist"));
        QVERIFY(library.track(a).value("lrc").toString().isEmpty());
        QVERIFY(library.workbenchTrack(a).value("lrc").toString().contains("爱与梦想"));
        // Apply an older result for A while B is selected: neither B nor A's
        // configured metadata may be replaced by candidate metadata.
        window.loadLibraryTrack(b);
        apply();
        QCOMPARE(title->text(), QString("本地歌曲"));
        QCOMPARE(artist->text(), QString("本地歌手"));
        QVERIFY(library.workbenchTrack(b).value("lrc").toString().isEmpty());
        window.loadLibraryTrack(a);
        QCOMPARE(title->text(), QString("URL title"));
        window.findChild<QPushButton *>("saveWorkbenchTrack")->click();
        QCOMPARE(library.track(a).value("title").toString(), QString("URL title"));
        QCOMPARE(library.track(a).value("artist").toString(), QString("URL artist"));
        QVERIFY(library.track(a).value("lrc").toString().contains("爱与梦想"));
        QCOMPARE(library.track(a).value("storage").toString(), QString("url"));
        QVERIFY(library.track(b).value("lrc").toString().isEmpty());
        QVERIFY(!window.findChild<QLineEdit *>("settingsPath_lyrics_alignmentModel"));
        QVERIFY(!alignmentFeaturesEnabled());
    }
};
QTEST_MAIN(WorkbenchTests)
#include "workbench_tests.moc"
