#include "lyricworkflow.h"
#include "window.h"
#include "theme.h"
#include <QDialogButtonBox>
#include <QTemporaryDir>
#include <QScrollBar>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QMenu>
#include <QButtonGroup>
#include <QDoubleSpinBox>
#include <QProcess>
#include <QMessageBox>
#include <QtTest>
#include <cmath>

class WorkbenchTests : public QObject {
    Q_OBJECT
  private slots:
    void themeCatalogModeMigrationAndRendering() {
        QTemporaryDir dir(QDir::current().filePath("theme-test-XXXXXX"));
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        settings.setValue("lyrics/autoFetch", false);
        settings.setValue("windows/showFloat", false);
        settings.setValue("windows/showLyrics", false);
        settings.setValue("player/mode", 1); // Former list mode.
        const auto audio = dir.filePath("demo.wav");
        QFile file(audio); QVERIFY(file.open(QIODevice::WriteOnly)); file.write("RIFF"); file.close();
        const auto id = library.addTrack({{"title", "音乐与远方 / Music"}, {"path", audio}, {"duration", 243000}});
        const auto url = library.addUrl({{"title", "在线收藏 / URL"}, {"duration", 192000},
                                         {"url", "https://www.bilibili.com/video/BV13x41117TL?p=1"}});
        const auto playlist = library.createPlaylist("精选歌单");
        QVERIFY(library.addToPlaylist(playlist, {id,url}));
        MainWindow window(&library, &settings);
        window.resize(1320, 850); window.show();
        auto mode = window.findChild<QComboBox *>("playbackMode");
        QCOMPARE(mode->count(), 3);
        QCOMPARE(mode->currentText(), QString("顺序播放"));
        QCOMPARE(window.player()->mode(), Player::Sequential);
        mode->setCurrentIndex(1);
        QCOMPARE(window.player()->mode(), Player::Shuffle);
        QCOMPARE(settings.value("player/mode").toInt(), 2);
        mode->setCurrentIndex(2);
        QCOMPARE(window.player()->mode(), Player::SingleLoop);
        QCOMPARE(settings.value("player/mode").toInt(), 3);
        mode->setCurrentIndex(0);
        auto choices = window.findChild<QButtonGroup *>("settingsTheme");
        QCOMPARE(choices->buttons().size(), 5);
        QCOMPARE(UiTheme::choices().first().first, QString("大肥鱼"));
        QVERIFY(!window.windowIcon().isNull()); // Object-library resources remain registered.
        const auto linear = [](double c) { return c <= .04045 ? c / 12.92 : std::pow((c + .055) / 1.055, 2.4); };
        const auto luminance = [&](QColor c) { return .2126*linear(c.redF()) + .7152*linear(c.greenF()) + .0722*linear(c.blueF()); };
        const auto contrast = [&](QColor a, QColor b) {
            const double x=luminance(a), y=luminance(b); return (qMax(x,y)+.05)/(qMin(x,y)+.05);
        };
        for (const auto &theme : UiTheme::choices()) {
            window.navigate(4);
            for (auto button : choices->buttons())
                if (button->property("value").toString() == theme.second) button->click();
            QCOMPARE(window.property("uiTheme").toString(), theme.second);
            QCOMPARE(settings.value("ui/theme").toString(), theme.second);
            const auto palette = UiTheme::forWidget(&window);
            const auto foreground = palette.color(QPalette::HighlightedText);
            for (const auto &name : {"navSettings", "transportToggle"}) {
                const auto icon = window.findChild<QPushButton *>(name)->icon().pixmap(QSize(24,24), QIcon::Normal, QIcon::On).toImage();
                bool found = false;
                for (int y = 0; y < icon.height(); ++y)
                    for (int x = 0; x < icon.width(); ++x) {
                        const auto pixel = icon.pixelColor(x,y);
                        if (pixel.alpha() > 240 && qAbs(pixel.red()-foreground.red()) < 5 &&
                            qAbs(pixel.green()-foreground.green()) < 5 && qAbs(pixel.blue()-foreground.blue()) < 5) found = true;
                    }
                QVERIFY2(found, name);
            }
            QVERIFY(contrast(palette.color(QPalette::Window), palette.color(QPalette::WindowText)) > 7.);
            QVERIFY(contrast(palette.color(QPalette::Highlight), palette.color(QPalette::HighlightedText)) > 3.);
            if (theme.second != "dark" && theme.second != "pink")
                QVERIFY(contrast(palette.color(QPalette::Highlight), palette.color(QPalette::HighlightedText)) > 4.5);
            const QString suffix = recognitionFeaturesEnabled() ? "full" : "lite";
            QVERIFY(window.grab().save(QDir(resourceRoot()).filePath(".cache/theme-settings-"+theme.second+"-"+suffix+".png")));
            window.navigate(2);
            window.findChild<QPushButton *>("playbackPlaylistsTab")->click();
            QVERIFY(window.grab().save(QDir(resourceRoot()).filePath(".cache/theme-playback-"+theme.second+"-"+suffix+".png")));
            window.findChild<QPushButton *>("playbackSongsTab")->click();
            auto tracks = window.findChild<TrackView *>("playbackTracks");
            const auto originalItem = tracks->topLevelItem(0);
            window.player()->currentChanged(id);
            QCOMPARE(tracks->topLevelItem(0), originalItem); // Highlighting must not rebuild the whole view.
        }
        QCOMPARE(UiTheme::normalized("unknown-theme"), QString("dark"));
        MainWindow restored(&library, &settings);
        QCOMPARE(restored.property("uiTheme").toString(), QString("pink"));
    }
    void searchKeyboardCycleAndTransport() {
        QTemporaryDir dir(QDir::current().filePath("keyboard-test-XXXXXX"));
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        settings.setValue("lyrics/autoFetch", false);
        settings.setValue("windows/showFloat", false);
        settings.setValue("windows/showLyrics", false);
        MainWindow window(&library, &settings);
        window.show(); window.activateWindow(); window.navigate(0);
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto toggle = window.findChild<QPushButton *>("transportToggle");
        auto input = window.findChild<QLineEdit *>("onlineSearchInput");
        auto search = window.findChild<QPushButton *>("onlineSearchButton");
        auto results = window.findChild<QTreeWidget *>("searchResults");
        auto jobs = window.findChild<QListWidget *>("downloadJobs");
        auto pages = window.findChild<QStackedWidget *>("modulePages");
        QTRY_COMPARE(QApplication::focusWidget(), toggle);
        const QList<QVariantMap> firstPage{
            {{"title", "第一首"}, {"url", "https://www.bilibili.com/video/BV13x41117TL?p=1"}},
            {{"title", "第二首"}, {"url", "https://www.bilibili.com/video/BV13x41117TL?p=2"}}};
        const QList<QVariantMap> secondPage{
            {{"title", "第三首"}, {"url", "https://www.bilibili.com/video/BV13x41117TL?p=3"}}};
        // Supply deterministic search responses without contacting any provider.
        disconnect(search, nullptr, &window, nullptr);
        connect(search, &QPushButton::clicked, &window, [&] {
            QTimer::singleShot(0, &window, [&] { window.showSearchResults(firstPage, 1, true, false); });
        });
        auto next = window.findChild<QPushButton *>("nextSearchPage");
        auto previous = window.findChild<QPushButton *>("previousSearchPage");
        disconnect(next, nullptr, &window, nullptr);
        disconnect(previous, nullptr, &window, nullptr);
        connect(next, &QPushButton::clicked, &window, [&] { window.showSearchResults(secondPage, 2, false, false); });
        connect(previous, &QPushButton::clicked, &window, [&] { window.showSearchResults(firstPage, 1, true, false); });
        QTest::keyClick(toggle, Qt::Key_F, Qt::ControlModifier);
        QCOMPARE(QApplication::focusWidget(), input);
        input->setText("测试");
        QTest::keyClick(input, Qt::Key_Return);
        QTRY_COMPARE(QApplication::focusWidget(), results);
        QCOMPARE(results->currentItem()->text(1), QString("第一首"));
        QTest::keyClick(results, Qt::Key_Down);
        QCOMPARE(results->currentItem()->text(1), QString("第二首"));
        QTest::keyClick(results, Qt::Key_Up);
        QCOMPARE(results->currentItem()->text(1), QString("第一首"));
        QTest::keyClick(results, Qt::Key_Right);
        QCOMPARE(results->currentItem()->text(1), QString("第三首"));
        QCOMPARE(QApplication::focusWidget(), results);
        QTest::keyClick(results, Qt::Key_Left);
        QCOMPARE(results->currentItem()->text(1), QString("第一首"));
        auto respond = [&](bool cancel) {
            QTimer::singleShot(1500, &window, [&] {
                if (auto choice = window.findChild<QMessageBox *>("searchResultAction")) choice->reject();
            });
            QTimer::singleShot(30, &window, [&, cancel] {
                auto choice = window.findChild<QMessageBox *>("searchResultAction");
                QVERIFY(choice);
                auto focus = qobject_cast<QPushButton *>(QApplication::focusWidget());
                QVERIFY(focus); QCOMPARE(focus->text(), QString("URL 收录"));
                QVERIFY(choice->defaultButton() == focus);
                if (cancel) {
                    QTest::keyClick(focus, Qt::Key_Tab);
                    focus = qobject_cast<QPushButton *>(QApplication::focusWidget());
                    QVERIFY(focus); QCOMPARE(focus->text(), QString("下载"));
                    QTest::keyClick(focus, Qt::Key_Tab);
                    focus = qobject_cast<QPushButton *>(QApplication::focusWidget());
                    QVERIFY(focus); QCOMPARE(focus->text(), QString("取消"));
                }
                QTest::keyClick(focus, Qt::Key_Return);
            });
            QTest::keyClick(results, Qt::Key_Return);
            QTRY_COMPARE(QApplication::focusWidget(), results);
        };
        respond(true);
        QCOMPARE(jobs->count(), 0);
        respond(false);
        QCOMPARE(jobs->count(), 1);
        QTest::keyClick(results, Qt::Key_Down);
        respond(false);
        QCOMPARE(jobs->count(), 2);
        QTest::keyClick(results, Qt::Key_Tab);
        QCOMPARE(QApplication::focusWidget()->objectName(), QString("discardDownloadedTrack"));
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Right);
        QCOMPARE(QApplication::focusWidget()->objectName(), QString("collectDownloadedTrack"));
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Down);
        QCOMPARE(jobs->currentRow(), 1);
        QCOMPARE(QApplication::focusWidget()->objectName(), QString("collectDownloadedTrack"));
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Up);
        QCOMPARE(jobs->currentRow(), 0);
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Right);
        QCOMPARE(QApplication::focusWidget()->objectName(), QString("openDownloadedTrack"));
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Return);
        QCOMPARE(pages->currentIndex(), 3);
        window.navigate(0); results->setFocus();
        QTest::keyClick(results, Qt::Key_Tab);
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Return); // Delete current staged URL.
        QCOMPARE(jobs->count(), 1);
        QCOMPARE(QApplication::focusWidget()->objectName(), QString("discardDownloadedTrack"));
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Right);
        QTest::keyClick(QApplication::focusWidget(), Qt::Key_Return); // Collect remaining URL.
        QCOMPARE(jobs->count(), 0);
        QCOMPARE(library.tracks().size(), 1);
        QCOMPARE(QApplication::focusWidget(), jobs);
        QTest::keyClick(jobs, Qt::Key_Tab);
        QCOMPARE(QApplication::focusWidget(), toggle);
        auto volume = window.findChild<QSlider *>("playerVolume");
        const QList<QPair<int,int>> upward{{75,85},{95,100},{20,30},{19,20},{5,10},{4,5},{0,1}};
        for (const auto &pair : upward) {
            volume->setValue(pair.first); QTest::keyClick(toggle, Qt::Key_Up);
            QCOMPARE(volume->value(), pair.second);
        }
        const QList<QPair<int,int>> downward{{75,65},{25,20},{20,15},{6,5},{5,4},{1,0},{0,0}};
        for (const auto &pair : downward) {
            volume->setValue(pair.first); QTest::keyClick(toggle, Qt::Key_Down);
            QCOMPARE(volume->value(), pair.second);
        }
        QTest::keyClick(toggle, Qt::Key_Tab);
        QCOMPARE(QApplication::focusWidget(), input);
        for (int page : {1,2,3}) {
            window.navigate(page);
            auto target = page == 1 ? static_cast<QWidget *>(window.findChild<TrackView *>("availableTracks"))
                                   : page == 2 ? static_cast<QWidget *>(window.findChild<TrackView *>("playbackTracks"))
                                               : static_cast<QWidget *>(window.findChild<QLineEdit *>("lyricSongTitle"));
            target->setFocus();
            QTest::keyClick(target, Qt::Key_F, Qt::ControlModifier);
            QCOMPARE(pages->currentIndex(), page);
            QVERIFY(QApplication::focusWidget() != input);
        }
    }
    void droppedAudioAndFoldersImportWithoutLyrics() {
        QTemporaryDir dir(QDir::current().filePath("drop-import-test-XXXXXX"));
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        settings.setValue("lyrics/autoFetch", false);
        settings.setValue("windows/showFloat", false);
        settings.setValue("windows/showLyrics", false);
        const auto audio = dir.filePath("单独 歌曲.wav");
        QProcess fixture;
        fixture.start(toolPath(&settings, "ffmpeg"), {"-v", "error", "-y", "-f", "lavfi", "-i",
                      "anullsrc=r=44100:cl=mono", "-t", "4", "-c:a", "pcm_s16le", audio});
        QVERIFY(fixture.waitForFinished(10000)); QCOMPARE(fixture.exitCode(), 0);
        QFile sidecar(dir.filePath("单独 歌曲.lrc"));
        QVERIFY(sidecar.open(QIODevice::WriteOnly)); sidecar.write("[00:01.000]Do not import this lyric"); sidecar.close();
        MainWindow window(&library, &settings);
        window.show(); window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QSignalSpy imported(window.imports(), &ImportService::imported);
        QMimeData mime; mime.setUrls({QUrl::fromLocalFile(audio)});
        QDragEnterEvent enter(QPoint(50,50), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &enter); QVERIFY(enter.isAccepted());
        QDropEvent drop(QPointF(50,50), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(&window, &drop); QVERIFY(drop.isAccepted());
        QTRY_COMPARE_WITH_TIMEOUT(imported.size(), 1, 15000);
        QCOMPARE(library.playlists().size(), 1);
        QCOMPARE(library.playlists().first().value("name").toString(), QString("导入"));
        const auto id = imported.first().first().toLongLong();
        QCOMPARE(library.tracks(library.playlists().first().value("id").toLongLong()).size(), 1);
        QVERIFY(library.track(id).value("lrc").toString().isEmpty());
        QVERIFY(library.stagedTracks().isEmpty());
        QVERIFY(library.setLyrics(id, "[00:01.000]Existing saved lyric", "Existing"));
        window.imports()->enqueueLocalFiles({audio, audio});
        QTRY_COMPARE_WITH_TIMEOUT(imported.size(), 2, 15000);
        QCOMPARE(library.tracks().size(), 1);
        QCOMPARE(library.playlists().size(), 1);
        QCOMPARE(library.track(id).value("lrc").toString(), QString("[00:01.000]Existing saved lyric"));
        // Exercise the transport on real decoded audio, without online sources.
        auto toggle = window.findChild<QPushButton *>("transportToggle");
        window.player()->setQueue({id}, id, false);
        QTRY_VERIFY_WITH_TIMEOUT(window.player()->media()->isSeekable(), 10000);
        toggle->setFocus(); QTest::keyClick(toggle, Qt::Key_Return);
        QTRY_COMPARE_WITH_TIMEOUT(window.player()->media()->playbackState(), QMediaPlayer::PlayingState, 5000);
        QTest::keyClick(toggle, Qt::Key_Enter, Qt::KeypadModifier);
        QTRY_COMPARE(window.player()->media()->playbackState(), QMediaPlayer::PausedState);
        const auto folder = dir.filePath("文件夹 歌单");
        QVERIFY(QDir().mkpath(folder + "/子文件夹"));
        QVERIFY(QFile::copy(audio, folder + "/第一首.WAV"));
        QVERIFY(QFile::copy(audio, folder + "/第二首.wav"));
        QVERIFY(QFile::copy(audio, folder + "/子文件夹/不能递归.wav"));
        ImportService service(&library, &settings);
        QSignalSpy done(&service, &ImportService::imported);
        QSignalSpy notices(&service, &ImportService::localImportNotice);
        service.enqueueLocalFiles({folder});
        QCOMPARE(notices.size(), 1);
        QVERIFY(notices.first().first().toString().contains("子文件夹"));
        QTRY_COMPARE_WITH_TIMEOUT(done.size(), 2, 15000);
        QCOMPARE(library.tracks().size(), 3);
        QCOMPARE(library.playlists().size(), 2);
        for (const auto &playlist : library.playlists())
            if (playlist.value("name").toString() == "文件夹 歌单")
                QCOMPARE(library.tracks(playlist.value("id").toLongLong()).size(), 2);
        const auto second = done.first().first().toLongLong();
        window.player()->setQueue({id, second}, id, false);
        toggle->setFocus(); QTest::keyClick(toggle, Qt::Key_Right);
        QCOMPARE(window.player()->currentId(), second);
        window.player()->media()->pause();
        window.player()->media()->setPosition(1000);
        QTest::keyClick(toggle, Qt::Key_Left);
        QCOMPARE(window.player()->media()->position(), 0);
    }
    void timelineTrimmingUsesPreviewPositionAndOnlyChangesDraft() {
        QTemporaryDir dir(QDir::current().filePath("timeline-trim-test-XXXXXX"));
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        settings.setValue("lyrics/autoFetch", false);
        settings.setValue("windows/showFloat", false);
        settings.setValue("windows/showLyrics", false);
        const auto audio = dir.filePath("tone.wav");
        QProcess fixture;
        fixture.start(toolPath(&settings, "ffmpeg"), {"-v", "error", "-y", "-f", "lavfi", "-i",
                      "anullsrc=r=44100:cl=mono", "-t", "12", "-c:a", "pcm_s16le", audio});
        QVERIFY(fixture.waitForFinished(10000));
        QCOMPARE(fixture.exitCode(), 0);
        const auto id = library.addTrack({{"title", "裁剪测试"}, {"path", audio}, {"duration", 12000}});
        const auto other = library.addTrack({{"title", "另一首"}, {"path", dir.filePath("other.wav")}});
        const QString original = "[00:01.000]門後\n[00:02.000]Foreign\n[00:02.000]譯文\n"
                                 "[00:03.000]第三句\n[00:04.000]第四句\n[00:05.000]第五句\n[00:06.000]第六句";
        QVERIFY(library.setLyrics(id, original, "已保存版本"));
        MainWindow window(&library, &settings);
        window.resize(1500, 950);
        window.show();
        window.loadLibraryTrack(id);
        auto seconds = window.findChild<QDoubleSpinBox *>("lyricTrimSeconds");
        QVERIFY(seconds);
        QCOMPARE(seconds->value(), 1.);
        auto after = window.findChild<QPushButton *>("lyricTrimAfter");
        QVERIFY(!after->isEnabled());
        window.findChild<QPushButton *>("trialLyrics")->click();
        auto media = window.player()->media();
        QTRY_VERIFY_WITH_TIMEOUT(media->isSeekable(), 10000);
        QTRY_COMPARE_WITH_TIMEOUT(media->playbackState(), QMediaPlayer::PlayingState, 10000);
        media->pause();
        QTRY_COMPARE(media->playbackState(), QMediaPlayer::PausedState);
        const auto source = media->source();
        const QStringList buttons{"lyricTrimAfter", "lyricTrimBefore", "lyricTrimToStart", "lyricTrimToEnd"};
        const QList<QList<qint64>> expected{{1000, 2000, 4000, 5000, 6000},
                                          {1000, 3000, 4000, 5000, 6000},
                                          {3000, 4000, 5000, 6000}, {1000, 2000}};
        for (int i = 0; i < buttons.size(); ++i) {
            QVERIFY(library.setWorkbenchLyrics(id, original, "草稿来源", 1000));
            media->setPosition(2000);
            QCOMPARE(media->position(), 2000);
            auto button = window.findChild<QPushButton *>(buttons[i]);
            QVERIFY(button->isEnabled());
            QSignalSpy changed(&library, &Library::workbenchChanged);
            button->click();
            QCOMPARE(changed.size(), 1); // Lyrics and offset change atomically.
            const auto draft = library.workbenchTrack(id);
            QCOMPARE(draft.value("lyric_offset").toInt(), 1000);
            QCOMPARE(draft.value("lyric_source").toString(), QString("草稿来源"));
            QList<qint64> stamps;
            for (const auto &line : Lrc::parse(draft.value("raw_lrc").toString())) stamps << line.ms;
            QCOMPARE(stamps, expected[i]);
            QVERIFY(draft.value("raw_lrc").toString().contains(i == 2 ? "第三句" : "門後"));
            if (i != 1 && i != 2) {
                QVERIFY(draft.value("raw_lrc").toString().contains("Foreign"));
                QVERIFY(draft.value("raw_lrc").toString().contains("譯文"));
            }
            QCOMPARE(library.track(id).value("raw_lrc").toString(), original);
            QCOMPARE(media->position(), 2000);
            QCOMPARE(media->source(), source);
            QCOMPARE(media->playbackState(), QMediaPlayer::PausedState);
        }
        window.loadLibraryTrack(other);
        QVERIFY(!after->isEnabled());
        after->click();
        QVERIFY(library.workbenchTrack(other).value("lrc").toString().isEmpty());
        window.loadLibraryTrack(id);
        seconds->setValue(2.5);
        QCOMPARE(settings.value("lyrics/trimSeconds").toDouble(), 2.5);
        QVERIFY(library.setWorkbenchLyrics(id, original, "草稿来源", -500));
        media->setPosition(2000); // Cursor on the lyric clock is 1.5 seconds.
        after->click();
        QList<qint64> stamps;
        for (const auto &line : Lrc::parse(library.workbenchTrack(id).value("raw_lrc").toString())) stamps << line.ms;
        QCOMPARE(stamps, QList<qint64>({1000, 4000, 5000, 6000}));
        QCOMPARE(library.workbenchTrack(id).value("lyric_offset").toInt(), -500);
        media->setPosition(11000);
        const auto unchanged = library.workbenchTrack(id).value("raw_lrc");
        after->click();
        QCOMPARE(library.workbenchTrack(id).value("raw_lrc"), unchanged);
        media->setPosition(0);
        window.findChild<QPushButton *>("lyricTrimToEnd")->click();
        QVERIFY(library.workbenchTrack(id).value("raw_lrc").toString().isEmpty());
        QVERIFY(!after->isEnabled());
        QVERIFY(library.setWorkbenchLyrics(id, original, "草稿来源", 1000));
        media->setPosition(2000);
        window.findChild<QPushButton *>("lyricTrimToEnd")->click();
        QVERIFY(window.grab().save(QDir(resourceRoot()).filePath(recognitionFeaturesEnabled()
            ? ".cache/timeline-trim-full.png" : ".cache/timeline-trim-lite.png")));
        window.resize(1120, 780);
        QTest::qWait(50);
        auto actionBar = window.findChild<QWidget *>("workbenchActions");
        for (const auto &name : buttons) {
            auto button = window.findChild<QPushButton *>(name);
            QVERIFY(actionBar->rect().contains(QRect(button->mapTo(actionBar, QPoint()), button->size())));
        }
        QVERIFY(window.grab().save(QDir(resourceRoot()).filePath(recognitionFeaturesEnabled()
            ? ".cache/timeline-trim-narrow-full.png" : ".cache/timeline-trim-narrow-lite.png")));
        window.findChild<QPushButton *>("saveWorkbenchTrack")->click();
        QCOMPARE(library.track(id).value("lyric_offset").toInt(), 1000);
        QCOMPARE(Lrc::parse(library.track(id).value("raw_lrc").toString()).size(), 2);
        QCOMPARE(library.track(id).value("path").toString(), audio);
        QVERIFY(QFileInfo(audio).size() > 100000);
    }
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
        QVERIFY(!window.findChild<QPushButton *>("libraryPlaylistSequential"));
        QVERIFY(!window.findChild<QPushButton *>("libraryPlaylistShuffle"));
        QVERIFY(window.grab().save(QDir(resourceRoot()).filePath(recognitionFeaturesEnabled()
            ? ".cache/library-playlist-detail-full.png" : ".cache/library-playlist-detail-lite.png")));
        window.findChild<QPushButton *>("playlistBack")->click();
        QCOMPARE(pages->currentIndex(), 0);
        QVERIFY(window.grab().save(QDir(resourceRoot()).filePath(recognitionFeaturesEnabled()
            ? ".cache/library-actions-full.png" : ".cache/library-actions-lite.png")));
        window.player()->setQueue({ids[3]}, ids[3], false);
        window.navigate(2);
        window.findChild<QPushButton *>("playbackPlaylistsTab")->click();
        auto playbackCards = window.findChild<PlaylistView *>("playbackPlaylistCards");
        auto playbackPages = window.findChild<QStackedWidget *>("playbackBrowserPages");
        QTest::qWait(30);
        QCOMPARE(playbackPages->currentIndex(), 1);
        QTest::mouseClick(playbackCards->viewport(), Qt::LeftButton, Qt::NoModifier,
                          playbackCards->visualItemRect(playbackCards->item(0)).center());
        QCOMPARE(playbackPages->currentIndex(), 0);
        auto sequential = window.findChild<QPushButton *>("playbackPlaylistSequential");
        auto random = window.findChild<QPushButton *>("playbackPlaylistShuffle");
        QVERIFY(sequential->isVisible());
        QVERIFY(random->isVisible());
        sequential->click();
        QCOMPARE(window.player()->queue(), QList<qint64>({ids[3], ids[0], ids[1]}));
        QCOMPARE(window.player()->currentId(), ids[3]);
        random->click();
        QCOMPARE(window.player()->queue().size(), 5);
        const auto shuffled = window.player()->queue().mid(3);
        QCOMPARE(QSet<qint64>(shuffled.begin(), shuffled.end()), QSet<qint64>({ids[0], ids[1]}));
        QCOMPARE(window.player()->currentId(), ids[3]);
        QVERIFY(window.grab().save(QDir(resourceRoot()).filePath(recognitionFeaturesEnabled()
            ? ".cache/playback-playlist-detail-full.png" : ".cache/playback-playlist-detail-lite.png")));
        window.findChild<QPushButton *>("playbackPlaylistBack")->click();
        QCOMPARE(playbackPages->currentIndex(), 1);
        QVERIFY(!sequential->isVisible());
        QVERIFY(!random->isVisible());
        QTest::qWait(30);
        auto rect = playbackCards->visualItemRect(playbackCards->item(0));
        QTest::mouseClick(playbackCards->viewport(), Qt::LeftButton, Qt::NoModifier, QPoint(rect.right() - 50, rect.bottom() - 30));
        QCOMPARE(playbackPages->currentIndex(), 0); // The former action area opens the playlist normally.
        QCOMPARE(window.player()->queue().size(), 5);
        window.findChild<QPushButton *>("playbackPlaylistBack")->click();
        QCOMPARE(playbackPages->currentIndex(), 1);
        QVERIFY(window.grab().save(QDir(resourceRoot()).filePath(recognitionFeaturesEnabled()
            ? ".cache/playback-playlist-actions-full.png" : ".cache/playback-playlist-actions-lite.png")));
        const auto empty = library.createPlaylist("空歌单");
        for (int i = 0; i < playbackCards->count(); ++i)
            if (playbackCards->item(i)->data(Qt::UserRole).toLongLong() == empty)
                playbackCards->itemClicked(playbackCards->item(i));
        QVERIFY(sequential->isVisible());
        QVERIFY(!sequential->isEnabled());
        QVERIFY(!random->isEnabled());
        sequential->click();
        random->click();
        QCOMPARE(window.player()->queue().size(), 5);
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
