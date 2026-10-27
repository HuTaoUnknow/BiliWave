#include "lyricworkflow.h"
#include "window.h"
#include <QDialogButtonBox>
#include <QTemporaryDir>
#include <QScrollBar>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMenu>
#include <QButtonGroup>
#include <QtTest>

class WorkbenchTests : public QObject {
    Q_OBJECT
  private slots:
    void unassignedFilterAndPlaylistQueueActions() {
        QTemporaryDir dir(QDir::current().filePath("library-actions-test-XXXXXX"));
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        settings.setValue("lyrics/autoFetch", false);
        settings.setValue("windows/showFloat", false);
        settings.setValue("windows/showLyrics", false);
        QList<qint64> ids;
        for (int i = 0; i < 4; ++i) {
            QFile file(dir.filePath(QString::number(i) + ".wav"));
            QVERIFY(file.open(QIODevice::WriteOnly)); file.write("RIFF"); file.close();
            ids << library.addTrack({{"title", QString("歌曲 %1").arg(i)}, {"path", file.fileName()}, {"duration", 60000}});
        }
        const auto first = library.createPlaylist("第一张歌单"), second = library.createPlaylist("第二张歌单");
        QVERIFY(library.addToPlaylist(first, {ids[0], ids[1]}));
        QVERIFY(library.addToPlaylist(second, {ids[1], ids[2]}));
        MainWindow window(&library, &settings);
        window.resize(1500, 950); window.show(); window.navigate(1);
        auto cards = window.findChild<PlaylistView *>("playlistList");
        auto available = window.findChild<TrackView *>("availableTracks");
        auto pages = window.findChild<QStackedWidget *>("libraryPlaylistPages");
        auto unassigned = window.findChild<QCheckBox *>("showUnassignedTracks");
        QTest::qWait(40);
        QCOMPARE(available->topLevelItemCount(), 4);
        QTest::mouseClick(cards->viewport(), Qt::LeftButton, Qt::NoModifier, cards->visualItemRect(cards->item(0)).center());
        QCOMPARE(pages->currentIndex(), 1);
        QCOMPARE(available->topLevelItemCount(), 2); // C belongs to another list, D belongs to none.
        unassigned->setChecked(true);
        QCOMPARE(available->topLevelItemCount(), 1);
        QCOMPARE(available->topLevelItem(0)->data(0, Qt::UserRole).toLongLong(), ids[3]);
        QVERIFY(library.addToPlaylist(second, {ids[3]}));
        QCOMPARE(available->topLevelItemCount(), 0);
        QVERIFY(library.removeFromPlaylist(second, {ids[3]}));
        QCOMPARE(available->topLevelItemCount(), 1);
        window.player()->setQueue({ids[3]}, ids[3], false);
        window.findChild<QPushButton *>("libraryPlaylistSequential")->click();
        QCOMPARE(window.player()->queue(), QList<qint64>({ids[3], ids[0], ids[1]}));
        QCOMPARE(window.player()->currentId(), ids[3]);
        window.findChild<QPushButton *>("libraryPlaylistShuffle")->click();
        QCOMPARE(window.player()->queue().size(), 5);
        const auto shuffled = window.player()->queue().mid(3);
        QCOMPARE(QSet<qint64>(shuffled.begin(), shuffled.end()), QSet<qint64>({ids[0], ids[1]}));
        QCOMPARE(window.player()->currentId(), ids[3]);
        QVERIFY(window.findChild<QPushButton *>("libraryPlaylistSequential")->isVisible());
        QVERIFY(window.findChild<QPushButton *>("libraryPlaylistShuffle")->isVisible());
        QVERIFY(window.grab().save(QDir(resourceRoot()).filePath(recognitionFeaturesEnabled()
            ? ".cache/library-playlist-detail-full.png" : ".cache/library-playlist-detail-lite.png")));
        window.findChild<QPushButton *>("playlistBack")->click();
        QCOMPARE(pages->currentIndex(), 0);
        QTest::qWait(30);
        QSignalSpy requested(cards, &PlaylistView::queueRequested);
        auto rect = cards->visualItemRect(cards->item(0));
        QTest::mouseClick(cards->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(rect.right() - 50, rect.bottom() - 30));
        QCOMPARE(requested.size(), 1);
        QCOMPARE(requested.last()[0].toLongLong(), first);
        QCOMPARE(requested.last()[1].toBool(), false);
        QCOMPARE(pages->currentIndex(), 0); // A small action must not open the playlist.
        QCOMPARE(window.player()->queue().size(), 7);
        QTest::mouseClick(cards->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(rect.right() - 18, rect.bottom() - 30));
        QCOMPARE(requested.size(), 2);
        QCOMPARE(requested.last()[1].toBool(), true);
        QCOMPARE(window.player()->queue().size(), 9);
        QCOMPARE(pages->currentIndex(), 0);
        QVERIFY(window.grab().save(QDir(resourceRoot()).filePath(recognitionFeaturesEnabled()
            ? ".cache/library-actions-full.png" : ".cache/library-actions-lite.png")));
        window.navigate(4);
        QCOMPARE(window.findChild<QStackedWidget *>("settingsSections")->count(), 5);
        window.findChild<QPushButton *>("settingsCategory4")->click();
        auto workbench = window.findChild<QWidget *>("settingsSection4");
        QVERIFY(workbench->isAncestorOf(window.findChild<QCheckBox *>("onlineSource_msr")));
        QVERIFY(workbench->isAncestorOf(window.findChild<QButtonGroup *>("settingsLyricSource")->button(0)));
        QVERIFY(window.findChild<QCheckBox *>("onlineSource_msr")->isVisible());
        QVERIFY(window.grab().save(QDir(resourceRoot()).filePath(recognitionFeaturesEnabled()
            ? ".cache/workbench-settings-full.png" : ".cache/workbench-settings-lite.png")));
        window.findChild<QPushButton *>("settingsCategory0")->click();
        for (const auto &name : {"generalDuckingDivider", "generalThemeDivider", "generalAudioOutputDivider"})
            QVERIFY(window.findChild<QFrame *>(name));
        QVERIFY(window.grab().save(QDir(resourceRoot()).filePath(recognitionFeaturesEnabled()
            ? ".cache/general-settings-full.png" : ".cache/general-settings-lite.png")));
        QCOMPARE(window.findChild<QPushButton *>("lyricPreviewBackward")->text(), QString("←"));
        QCOMPARE(window.findChild<QPushButton *>("lyricPreviewForward")->text(), QString("→"));
    }
    void onlineSourceChoicesAndFiveResultQueue() {
        QTemporaryDir dir(QDir::current().filePath("source-options-test-XXXXXX"));
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        settings.setValue("lyrics/autoFetch", false);
        settings.setValue("windows/showFloat", false);
        settings.setValue("windows/showLyrics", false);
        MainWindow window(&library, &settings);
        window.show();
        window.navigate(4);
        for (const auto &source : LyricsSourcesSearch::sources())
            QVERIFY(window.findChild<QCheckBox *>("onlineSource_" + source.first));
        auto priority = window.findChild<QButtonGroup *>("settingsPrimaryOnlineSource");
        QVERIFY(priority && priority->buttons().size() == 6);
        for (auto button : priority->buttons())
            if (button->property("value").toString() == "qq") button->click();
        QCOMPARE(LyricsSourcesSearch::selectedSources(&settings).first(), QString("qq"));
        window.findChild<QCheckBox *>("onlineSource_qq")->setChecked(false);
        QVERIFY(!LyricsSourcesSearch::selectedSources(&settings).contains("qq"));
        const auto id = library.addTrack({{"title", "Fixture"}, {"artist", "Configured artist"},
                                         {"path", dir.filePath("fixture.flac")}});
        window.loadLibraryTrack(id);
        auto menu = window.findChild<QPushButton *>("onlineSourceOptions")->menu();
        QVERIFY(menu);
        const auto preferred = menu->actions().first()->menu();
        for (auto action : preferred->actions()) if (action->data().toString() == "netease") action->trigger();
        QCOMPARE(LyricsSourcesSearch::selectedSources(&settings).first(), QString("netease"));
        QList<QVariantMap> candidates;
        for (int i = 0; i < 7; ++i) candidates << QVariantMap{
            {"id", i}, {"provider", "Fixture source"}, {"trackName", "Fixture"},
            {"artistName", "Other artist"}, {"syncedLyrics", QString("[00:01.00]Fixture %1").arg(i)}};
        window.lyrics()->candidates(id, candidates);
        QCOMPARE(window.findChild<QPushButton *>("matchLyrics")->text(), QString("继续搜索"));
        auto queue = window.findChild<LyricQueue *>();
        QCOMPARE(queue->findChild<QListWidget *>("lyricQueueTasks")->count(), 5);
        QCOMPARE(library.workbenchTrack(id).value("artist").toString(), QString("Configured artist"));
        window.navigate(4);
        QVERIFY(window.findChild<QCheckBox *>("onlineSource_netease")->isChecked());
    }
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
        QTest::qWait(50);
        QCOMPARE(view->geometry(), initial);
        const auto rowBottom = [&] {
            const auto rect = view->visualItemRect(view->topLevelItem(view->topLevelItemCount() - 1));
            return view->viewport()->mapTo(view, rect.bottomLeft()).y();
        };
        const int bottom = rowBottom();
        QVERIFY(qAbs(bottom - view->contentsRect().bottom()) <= 1);
        window.player()->enqueue({id});
        QTest::qWait(50);
        QCOMPARE(view->geometry(), initial);
        QCOMPARE(view->topLevelItemCount(), 2);
        QCOMPARE(rowBottom(), bottom);
        QVERIFY(view->viewport()->mapTo(view, QPoint()).y() > view->height() / 2);
        QVERIFY(window.grab().save(QDir(resourceRoot()).filePath(
            recognitionFeaturesEnabled() ? ".cache/queue-bottom-full.png" : ".cache/queue-bottom-lite.png")));
        // The unused area above the rows remains a drop target.
        QMimeData mime;
        mime.setData(TrackMime, "[\"" + QByteArray::number(id) + "\"]");
        QDragEnterEvent enter(QPoint(20, 20), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view, &enter);
        QVERIFY(enter.isAccepted());
        QDropEvent drop(QPointF(20, 20), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(view, &drop);
        QVERIFY(drop.isAccepted());
        QCOMPARE(window.player()->queue().size(), 3);
        QList<qint64> many;
        for (int i = 0; i < 40; ++i)
            many << id;
        window.player()->enqueue(many);
        QTest::qWait(50);
        QVERIFY(view->verticalScrollBar()->maximum() > 0);
        QCOMPARE(view->verticalScrollBar()->value(), view->verticalScrollBar()->maximum());
        QCOMPARE(rowBottom(), bottom);
        window.player()->setQueue({id}, id, false);
        QTest::qWait(50);
        QCOMPARE(rowBottom(), bottom);
        QCOMPARE(view->geometry(), initial);
        window.resize(1400, 700);
        QTest::qWait(50);
        QVERIFY(qAbs(rowBottom() - view->contentsRect().bottom()) <= 1);
        window.resize(1500, 1000);
        QTest::qWait(50);
        QVERIFY(qAbs(rowBottom() - view->contentsRect().bottom()) <= 1);
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
