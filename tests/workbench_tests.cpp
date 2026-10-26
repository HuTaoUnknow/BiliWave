#include "lyricworkflow.h"
#include "window.h"
#include <QDialogButtonBox>
#include <QTemporaryDir>
#include <QtTest>

class WorkbenchTests : public QObject {
    Q_OBJECT
  private slots:
    void playbackQueueKeepsItsFullHeightWhenEntriesChange() {
        QTemporaryDir dir(QDir::current().filePath("queue-layout-test-XXXXXX"));
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        settings.setValue("lyrics/autoFetch", false);
        settings.setValue("windows/showFloat", false);
        settings.setValue("windows/showLyrics", false);
        MainWindow window(&library, &settings);
        window.resize(1500, 950);
        window.show();
        window.navigate(2);
        auto pane = window.findChild<QWidget *>("playbackQueuePane");
        auto view = window.findChild<QTreeWidget *>("playbackQueue");
        QCoreApplication::processEvents();
        const auto initial = view->geometry();
        QVERIFY(initial.height() > pane->height() * .8);
        QCOMPARE(initial.bottom(), pane->height() - 1);
        const auto id = library.addTrack({{"title", "队列歌曲"}, {"path", dir.filePath("song.flac")}});
        window.player()->setQueue({id}, id, false);
        QCoreApplication::processEvents();
        QCOMPARE(view->geometry(), initial);
        window.player()->enqueue({id});
        QCoreApplication::processEvents();
        QCOMPARE(view->geometry(), initial);
        QCOMPARE(view->topLevelItemCount(), 2);
        for (auto label : pane->findChildren<QLabel *>()) {
            QVERIFY(!label->text().contains("首待播"));
            QVERIFY(!label->text().contains("从下向上"));
            QVERIFY(!label->text().contains("越靠下"));
        }
    }
    void urlCollectionHasBothPathsAndImportClearsWorkspace() {
        QTemporaryDir dir(QDir::current().filePath("collection-test-XXXXXX"));
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        settings.setValue("lyrics/autoFetch", false);
        settings.setValue("windows/showFloat", false);
        settings.setValue("windows/showLyrics", false);
        MainWindow window(&library, &settings);
        window.show();
        auto jobs = window.findChild<QListWidget *>("downloadJobs");
        auto selector = window.findChild<QComboBox *>("lyricTrackSelector");
        const auto collect = [&](const QString &video, const QString &title) {
            window.navigate(0);
            window.showSearchResults({{{"title", title}, {"url", "https://www.bilibili.com/video/" + video}}},
                                     1, false, true);
            auto results = window.findChild<QTreeWidget *>("searchResults");
            results->topLevelItem(0)->setSelected(true);
            window.findChild<QPushButton *>("collectSearchUrl")->click();
        };
        collect("BV13x41117TL", "URL 配词");
        QCOMPARE(library.tracks().size(), 0);
        QCOMPARE(library.stagedTracks().size(), 1);
        QCOMPARE(jobs->count(), 1);
        const auto id = jobs->item(0)->data(Qt::UserRole).toLongLong();
        auto card = jobs->itemWidget(jobs->item(0));
        QVERIFY(card->findChild<QLabel *>("downloadTaskTitle")->text().contains("[URL]"));
        QVERIFY(card->findChild<QPushButton *>("collectDownloadedTrack")->isEnabled());
        card->findChild<QPushButton *>("openDownloadedTrack")->click();
        QCOMPARE(selector->currentData().toLongLong(), id);
        window.findChild<QLineEdit *>("lyricSongTitle")->setText("保存后的歌名");
        window.findChild<QLineEdit *>("lyricSongArtist")->setText("保存后的歌手");
        QVERIFY(library.setWorkbenchLyrics(id, "[00:01.000]這是歌詞", "测试"));
        window.findChild<QPushButton *>("commitWithLyrics")->click();
        QCOMPARE(jobs->count(), 0);
        QCOMPARE(library.tracks().size(), 1);
        QCOMPARE(library.track(id).value("storage").toString(), QString("url"));
        QCOMPARE(library.track(id).value("title").toString(), QString("保存后的歌名"));
        QCOMPARE(library.track(id).value("artist").toString(), QString("保存后的歌手"));
        QVERIFY(library.track(id).value("lrc").toString().contains("这是歌词"));
        QCOMPARE(selector->currentData().toLongLong(), 0);
        QVERIFY(window.findChild<QLineEdit *>("lyricSongTitle")->text().isEmpty());
        QVERIFY(window.findChild<QLineEdit *>("lyricSongArtist")->text().isEmpty());
        QCOMPARE(window.findChild<QListWidget *>("timedLyricPreview")->count(), 0);
        QVERIFY(window.findChild<QLabel *>("lyricMatchStatus")->text().isEmpty());
        collect("BV1bK411W797", "URL 直接收录");
        QCOMPARE(jobs->count(), 1);
        jobs->itemWidget(jobs->item(0))->findChild<QPushButton *>("collectDownloadedTrack")->click();
        QCOMPARE(jobs->count(), 0);
        QCOMPARE(library.tracks().size(), 2);
        QCOMPARE(selector->currentData().toLongLong(), 0);
        collect("BV1rGp96VEFc", "URL 放弃");
        QCOMPARE(jobs->count(), 1);
        jobs->itemWidget(jobs->item(0))->findChild<QPushButton *>("discardDownloadedTrack")->click();
        QCOMPARE(jobs->count(), 0);
        QVERIFY(library.stagedTracks().isEmpty());
        QCOMPARE(library.tracks().size(), 2);
    }
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
