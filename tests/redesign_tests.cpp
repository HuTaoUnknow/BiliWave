#include "desktopintegration.h"
#include "lyricworkflow.h"
#include "theme.h"
#include "window.h"
#include <QButtonGroup>
#include <QClipboard>
#include <QCloseEvent>
#include <QCursor>
#include <QDesktopServices>
#include <QDir>
#include <QDropEvent>
#include <QFile>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QProcess>
#include <QScopeGuard>
#include <QScreen>
#include <QScrollArea>
#include <QScrollBar>
#include <QSplitter>
#include <QTemporaryDir>
#include <QThread>
#include <QUuid>
#include <QWheelEvent>
#include <QtMath>
#include <QtTest>
#include <cmath>

class AiLinkCapture : public QObject {
    Q_OBJECT
  public:
    QUrl opened;
  public slots:
    void open(const QUrl &url) {
        opened = url;
    }
};
class DragTrackView : public TrackView {
  public:
    using TrackView::startDrag;
};

class RedesignTests : public QObject {
    Q_OBJECT
    static QAbstractButton *settingChoice(MainWindow &window, const QString &name, const QString &value) {
        auto group = window.findChild<QButtonGroup *>(name);
        if (group)
            for (auto button : group->buttons())
                if (button->property("value").toString() == value)
                    return button;
        return nullptr;
    }
    static void admitTrack(MainWindow &window, qint64 id) {
        window.imports()->completed(QString("test-%1").arg(id), id);
        auto jobs = window.findChild<QListWidget *>("downloadJobs");
        for (int i = 0; i < jobs->count(); ++i) {
            auto item = jobs->item(i);
            if (item->data(Qt::UserRole).toLongLong() == id) {
                jobs->itemWidget(item)->findChild<QPushButton *>("openDownloadedTrack")->click();
                return;
            }
        }
    }
  private slots:
    void onlineResultsAndSimplificationSetting() {
        QTemporaryDir dir(QDir::current().filePath("online-results-ui-XXXXXX"));
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        settings.setValue("lyrics/autoFetch", false);
        settings.setValue("windows/showFloat", false);
        settings.setValue("windows/showLyrics", false);
        MainWindow window(&library, &settings);
        window.show();
        const auto id = library.addTrack(
            {{"title", "歌词结果验证"}, {"artist", "测试歌手"}, {"path", "test.flac"}, {"staged", 1}});
        admitTrack(window, id);
        window.lyrics()->cancel();
        window.navigate(3);
        auto queue = window.findChild<LyricQueue *>();
        QVERIFY(queue->isVisible());
        const QVariantMap candidate{{"id", 12345},
                                    {"provider", "LRCLIB"},
                                    {"trackName", "歌词结果验证"},
                                    {"artistName", "测试歌手"},
                                    {"syncedLyrics", "[00:01.000]這是愛與夢想\n[00:03.000]讓音樂繼續"},
                                    {"sourceUrl", "https://lrclib.net/api/get/12345"}};
        window.lyrics()->candidates(
            id, {candidate, {{"id", 67890}, {"trackName", "纯文字版本"}, {"plainLyrics", "這是純文字版本"}}});
        QCOMPARE(queue->count(), 2);
        window.lyrics()->useCandidate(id, candidate, true);
        QCOMPARE(queue->count(), 2);
        QCOMPARE(queue->taskState(0), QString("已应用"));
        auto timeline = window.findChild<QListWidget *>("timedLyricPreview");
        auto displayed = [timeline] {
            return timeline->itemWidget(timeline->item(0))->findChild<QLabel *>()->text();
        };
        QVERIFY(displayed().contains("这是爱与梦想"));
        window.navigate(4);
        auto simplify = window.findChild<QCheckBox *>("settingsAutoSimplifyChinese");
        QVERIFY(simplify && simplify->isVisible() && simplify->isChecked());
        simplify->click();
        QVERIFY(!settings.value("lyrics/autoSimplifyChinese", true).toBool());
        QVERIFY(displayed().contains("這是愛與夢想"));
        simplify->click();
        QVERIFY(displayed().contains("这是爱与梦想"));
        QSettings saved(settings.fileName(), QSettings::IniFormat);
        QVERIFY(saved.value("lyrics/autoSimplifyChinese", false).toBool());
        QDir screenshots(resourceRoot() + "/docs/screenshots");
        screenshots.mkpath(".");
        QCoreApplication::processEvents();
        QVERIFY(window.grab().save(screenshots.filePath("automatic-simplified-chinese.png")));
        window.navigate(3);
        QCoreApplication::processEvents();
        QVERIFY(window.grab().save(screenshots.filePath("lyric-results-queue.png")));
    }
    void duckingSettingsPreserveBaseVolumeAndPersist() {
        QTemporaryDir dir(QDir::current().filePath("ducking-ui-XXXXXX"));
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        settings.setValue("lyrics/autoFetch", false);
        settings.setValue("windows/showFloat", false);
        settings.setValue("windows/showLyrics", false);
        settings.setValue("application/closeToTray", false);
        settings.setValue("player/volume", 80);
        {
            MainWindow window(&library, &settings);
            window.show();
            window.navigate(4);
            auto enabled = window.findChild<QCheckBox *>("settingsDuckingEnabled");
            auto speech = window.findChild<QCheckBox *>("settingsDuckingOnSpeech");
            auto mic = window.findChild<QCheckBox *>("settingsDuckingOnMicrophone");
            auto other = window.findChild<QCheckBox *>("settingsDuckingOnOtherAudio");
            auto reduction = window.findChild<QSpinBox *>("settingsDuckingReduction");
            auto volume = window.findChild<QSlider *>("playerVolume");
            QVERIFY(enabled && speech && mic && other && reduction && volume);
            QVERIFY(!enabled->isChecked());
            QVERIFY(speech->isChecked() && mic->isChecked());
            QVERIFY(other->isChecked());
            QVERIFY(!reduction->isEnabled());
            QCOMPARE(reduction->value(), 70);
            // Avoid opening the physical microphone in a settings persistence test.
            speech->setChecked(false);
            mic->setChecked(false);
            other->setChecked(false);
            enabled->setChecked(true);
            QVERIFY(reduction->isEnabled());
            reduction->setValue(85);
            window.player()->setDuckingGain(.15f);
            QCOMPARE(volume->value(), 80);
            QCOMPARE(settings.value("player/volume").toInt(), 80);
            volume->setValue(60);
            QVERIFY(qAbs(window.player()->audio()->volume() - .09f) < .001f);
            window.player()->setDuckingGain(1);
            enabled->setChecked(false);
            speech->setChecked(true);
            mic->setChecked(true);
            QCoreApplication::processEvents();
            QDir shots(resourceRoot() + "/docs/screenshots");
            shots.mkpath(".");
            QVERIFY(window.grab().save(shots.filePath("auto-ducking-settings.png")));
            window.close();
            QCOMPARE(settings.value("player/volume").toInt(), 60);
        }
        MainWindow restored(&library, &settings);
        QCOMPARE(restored.findChild<QSpinBox *>("settingsDuckingReduction")->value(), 85);
        QCOMPARE(restored.findChild<QSlider *>("playerVolume")->value(), 60);
        QVERIFY(!restored.findChild<QCheckBox *>("settingsDuckingEnabled")->isChecked());
        QVERIFY(!restored.findChild<QCheckBox *>("settingsDuckingOnOtherAudio")->isChecked());
    }
    void startupRegistrationQuotesPathsAndRemovesOnlyItsOwnEntry() {
        QTemporaryDir dir(QDir::current().filePath("startup-registration-XXXXXX"));
        QSettings store(dir.filePath("startup.ini"), QSettings::IniFormat);
        store.setValue("UnrelatedApplication", "keep this entry");
        const auto exe = dir.filePath("Program Files/BiliWave.exe");
        const auto data = dir.filePath("Music Library/data");
        QString error;
        QVERIFY(DesktopIntegration::writeStartupEntry(store, true, exe, data, &error));
        const auto command = store.value(DesktopIntegration::startupEntryName()).toString();
        QVERIFY(command.startsWith('"' + QDir::toNativeSeparators(exe) + '"'));
        QVERIFY(command.contains(" --startup --data "));
        QVERIFY(command.endsWith('"' + QDir::toNativeSeparators(data) + '"'));
        QVERIFY(DesktopIntegration::writeStartupEntry(store, false, exe, data, &error));
        QVERIFY(!store.contains(DesktopIntegration::startupEntryName()));
        QCOMPARE(store.value("UnrelatedApplication").toString(), QString("keep this entry"));
#ifdef Q_OS_WIN
        QVERIFY(DesktopIntegration::startupCommand(exe, "D:/").endsWith("\"D:\\\\\""));
#endif
    }
    void existingInstanceReceivesCommandsWithoutStartingAnotherServer() {
        QTemporaryDir dir(QDir::current().filePath("instance-test-XXXXXX"));
        DesktopIntegration::ApplicationInstance first, second;
        QThread serverThread;
        auto mainThread = QThread::currentThread();
        first.moveToThread(&serverThread);
        serverThread.start();
        auto cleanup = qScopeGuard([&] {
            QMetaObject::invokeMethod(
                &first, [&] { first.moveToThread(mainThread); }, Qt::BlockingQueuedConnection);
            serverThread.quit();
            serverThread.wait();
        });
        QString error;
        DesktopIntegration::ApplicationInstance::Claim claimed;
        QMetaObject::invokeMethod(
            &first, [&] { claimed = first.claim(dir.path(), {}, &error); }, Qt::BlockingQueuedConnection);
        QVERIFY2(claimed == DesktopIntegration::ApplicationInstance::Primary, qPrintable(error));
        QList<QJsonObject> messages;
        QObject receiver;
        connect(&first, &DesktopIntegration::ApplicationInstance::commandRequested, &receiver,
                [&](const QJsonObject &value) { messages << value; });
        const QJsonObject request{{"startup", false}, {"search", "当前歌曲"}};
        QVERIFY2(second.claim(dir.path(), request, &error) ==
                     DesktopIntegration::ApplicationInstance::Notified,
                 qPrintable(error));
        QTRY_COMPARE_WITH_TIMEOUT(messages.size(), 1, 3000);
        QCOMPARE(messages.first(), request);
        QCOMPARE(second.claim(dir.path(), {{"startup", true}}, &error),
                 DesktopIntegration::ApplicationInstance::Notified);
        QTRY_COMPARE_WITH_TIMEOUT(messages.size(), 2, 3000);
        QVERIFY(messages.last()["startup"].toBool());
        DesktopIntegration::ApplicationInstance otherLibrary;
        QCOMPARE(otherLibrary.claim(dir.filePath("another-data"), {}, &error),
                 DesktopIntegration::ApplicationInstance::Primary);
    }
    void trayCloseKeepsPlaybackAndQueuedWorkAlive() {
        if (!QSystemTrayIcon::isSystemTrayAvailable())
            QSKIP("Windows system tray unavailable on this desktop");
        QTemporaryDir dir(QDir::current().filePath("tray-lifecycle-XXXXXX"));
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        settings.setValue("lyrics/autoFetch", false);
        settings.setValue("windows/showFloat", true);
        settings.setValue("windows/showLyrics", true);
        const auto audio = dir.filePath("test.flac");
        QCOMPARE(QProcess::execute(toolPath(&settings, "ffmpeg"),
                                   {"-v", "error", "-f", "lavfi", "-i", "sine=duration=20", "-y", audio}),
                 0);
        const auto id = library.addTrack({{"title", "托盘后台播放"}, {"path", audio}});
        MainWindow window(&library, &settings);
        window.show();
        window.navigate(4);
        window.findChild<QPushButton *>("settingsCategory0")->click();
        auto closeToTray = window.findChild<QCheckBox *>("settingsCloseToTray");
        auto autoStart = window.findChild<QCheckBox *>("settingsAutoStart");
        QVERIFY(closeToTray && closeToTray->isChecked());
        QVERIFY(autoStart);
        QCOMPARE(autoStart->isChecked(), DesktopIntegration::startupEnabled());
        closeToTray->setChecked(false);
        QVERIFY(!settings.value("application/closeToTray").toBool());
        closeToTray->setChecked(true);
        auto tray = window.findChild<QSystemTrayIcon *>("biliwaveTray");
        QVERIFY(tray && tray->isVisible());
        QVERIFY(!tray->icon().isNull());
        const QImage iconImage(":/biliwave/icon.png");
        QVERIFY(iconImage.hasAlphaChannel());
        QCOMPARE(iconImage.pixelColor(0, 0).alpha(), 0);
        QCOMPARE(iconImage.pixelColor(iconImage.width() - 1, iconImage.height() - 1).alpha(), 0);
        QCOMPARE(tray->icon().cacheKey(), window.windowIcon().cacheKey());
        window.player()->setQueue({id}, id, true);
        QTRY_VERIFY_WITH_TIMEOUT(window.player()->media()->position() > 100, 10000);
        auto queue = window.findChild<LyricQueue *>();
        queue->enqueue(id, {{"operation", "lrc"}, {"lrc", "[00:01.00]后台结果"}});
        const auto position = window.player()->media()->position();
        QCloseEvent close;
        QApplication::sendEvent(&window, &close);
        QVERIFY(!close.isAccepted());
        QVERIFY(!window.isVisible());
        QVERIFY(tray->isVisible());
        QVERIFY(window.floatBar()->isVisible());
        QVERIFY(window.overlay()->isVisible());
        QCOMPARE(window.player()->media()->playbackState(), QMediaPlayer::PlayingState);
        QTRY_VERIFY_WITH_TIMEOUT(window.player()->media()->position() > position + 100, 5000);
        QTRY_COMPARE_WITH_TIMEOUT(queue->taskState(0), QString("待应用"), 10000);
        QVERIFY(library.track(id).value("lrc").toString().isEmpty());
        auto menu = tray->contextMenu();
        menu->findChild<QAction *>("trayTogglePlayback")->trigger();
        QCOMPARE(window.player()->media()->playbackState(), QMediaPlayer::PausedState);
        menu->findChild<QAction *>("trayTogglePlayback")->trigger();
        QCOMPARE(window.player()->media()->playbackState(), QMediaPlayer::PlayingState);
        window.showMaximized();
        window.hideToTray();
        menu->findChild<QAction *>("trayShowWindow")->trigger();
        QVERIFY(window.isVisible());
        QVERIFY(window.isMaximized());
        window.hideToTray();
        menu->findChild<QAction *>("trayOpenSettings")->trigger();
        QVERIFY(window.isVisible());
        QVERIFY(window.findChild<QPushButton *>("navSettings")->isChecked());
        window.showNormal();
        window.resize(1240, 850);
        QDir shots(resourceRoot() + "/docs/screenshots");
        shots.mkpath(".");
        QVERIFY(window.grab().save(shots.filePath("tray-startup-settings.png")));
        QVERIFY(menu->grab().save(shots.filePath("tray-menu.png")));
        menu->findChild<QAction *>("trayExit")->trigger();
        QVERIFY(!window.isVisible());
        QVERIFY(!tray->isVisible());
        QVERIFY(!window.floatBar()->isVisible());
        QVERIFY(!window.overlay()->isVisible());
        QCOMPARE(settings.value("player/current").toLongLong(), id);
        MainWindow restored(&library, &settings);
        QVERIFY(restored.findChild<QCheckBox *>("settingsCloseToTray")->isChecked());
    }
    void libraryDeletesRelatedFilesAfterConfirmation() {
        QTemporaryDir dir(QDir::current().filePath("delete-related-XXXXXX"));
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        settings.setValue("lyrics/autoFetch", false);
        settings.setValue("windows/showFloat", false);
        settings.setValue("windows/showLyrics", false);
        const auto token = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto audio = dir.filePath(token + ".m4a");
        const auto raw = dir.filePath(token + ".source.m4s");
        QProcess ffmpeg;
        ffmpeg.start(QDir(resourceRoot()).filePath("tools/ffmpeg.exe"),
                     {"-v", "error", "-n", "-f", "lavfi", "-i", "sine=frequency=440:duration=6", audio});
        QVERIFY(ffmpeg.waitForFinished(15000) && ffmpeg.exitCode() == 0);
        QVERIFY(QFile::copy(audio, raw));
        const QStringList related{audio, raw, dir.filePath(token + ".lrc"),
                                  dir.filePath(token + ".cover.jpg"),
                                  dir.filePath(token + ".source.m4s.part")};
        for (const auto &path : related.mid(2)) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("test");
        }
        const auto keep = dir.filePath("keep.flac");
        {
            QFile file(keep);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("keep");
        }
        const auto id = library.addTrack({{"title", "删除测试歌曲"}, {"path", audio}, {"original", raw}});
        const auto playlist = library.createPlaylist("保留歌单");
        library.addToPlaylist(playlist, {id});
        library.setLyrics(id, "[00:01.00]原歌词", "测试");
        MainWindow window(&library, &settings);
        window.show();
        window.navigate(1);
        window.player()->setQueue({id, id}, id, false);
        QTRY_VERIFY_WITH_TIMEOUT(window.player()->media()->duration() > 5000, 15000);
        auto queue = window.findChild<LyricQueue *>();
        QSignalSpy finished(queue, &LyricQueue::taskFinished);
        queue->enqueue(id, {{"operation", "lrc"}, {"lrc", "[00:01.00]结果"}});
        QVERIFY(finished.wait(10000));
        auto tracks = window.findChild<TrackView *>("availableTracks");
        auto perform = [&](qint64 target, bool accept, QString *details) {
            QTreeWidgetItem *item = nullptr;
            tracks->clearSelection();
            for (int i = 0; i < tracks->topLevelItemCount(); ++i)
                if (tracks->topLevelItem(i)->data(0, Qt::UserRole).toLongLong() == target)
                    item = tracks->topLevelItem(i);
            if (!item)
                return false;
            item->setSelected(true);
            bool offered = false, confirmed = false;
            QTimer::singleShot(0, &window, [&] {
                auto menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
                if (!menu)
                    return;
                for (auto action : menu->actions())
                    if (action->objectName() == "deleteSongFiles") {
                        offered = true;
                        QTimer::singleShot(0, &window, [&] {
                            auto box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                            if (box) {
                                confirmed = box->objectName() == "deleteSongFilesConfirmation";
                                if (details)
                                    *details = box->detailedText();
                                box->done(accept ? QMessageBox::Yes : QMessageBox::No);
                            }
                        });
                        action->trigger();
                        break;
                    }
                menu->close();
            });
            tracks->customContextMenuRequested(tracks->visualItemRect(item).center());
            return offered && confirmed;
        };
        QString details;
        QVERIFY(perform(id, false, &details));
        for (const auto &path : related) {
            QVERIFY(QFile::exists(path));
            QVERIFY(details.contains(path));
        }
        QVERIFY(!details.contains(keep));
        QVERIFY(!library.track(id).isEmpty());
        QCOMPARE(queue->count(), 1);
        QVERIFY(perform(id, true, nullptr));
        QTRY_VERIFY(library.track(id).isEmpty());
        for (const auto &path : related)
            QVERIFY(!QFile::exists(path));
        QVERIFY(QFile::exists(keep));
        QCOMPARE(queue->count(), 0);
        QVERIFY(library.tracks(playlist).isEmpty());
        QVERIFY(!window.player()->queue().contains(id));
        // An unselected song still using an original file must keep that resource.
        const auto shared = dir.filePath("shared.m4s"), rendered = dir.filePath("selected.m4a");
        for (const auto &path : {shared, rendered}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("test");
        }
        const auto other = library.addTrack({{"title", "保留歌曲"}, {"path", keep}, {"original", shared}});
        const auto selected =
            library.addTrack({{"title", "共享原音轨"}, {"path", rendered}, {"original", shared}});
        QVERIFY(perform(selected, true, &details));
        QTRY_VERIFY(library.track(selected).isEmpty());
        QVERIFY(QFile::exists(shared) && QFile::exists(keep));
        QVERIFY(!QFile::exists(rendered));
        QVERIFY(!library.track(other).isEmpty());
        QVERIFY(details.contains("仍被其他歌曲使用"));
    }
    void twoThemesPreservePlaybackAndBackgroundWork() {
        QTemporaryDir dir(QDir::current().filePath("theme-workflow-XXXXXX"));
        const auto audio = dir.filePath("test.flac");
        QProcess ffmpeg;
        ffmpeg.start(QDir(resourceRoot()).filePath("tools/ffmpeg.exe"),
                     {"-v", "error", "-n", "-f", "lavfi", "-i", "sine=frequency=440:duration=12", audio});
        QVERIFY(ffmpeg.waitForFinished(15000));
        QCOMPARE(ffmpeg.exitCode(), 0);
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        settings.setValue("lyrics/autoFetch", false);
        settings.setValue("windows/showFloat", false);
        settings.setValue("windows/showLyrics", false);
        const QString lrc =
            "[00:01.00]听见微光，落在窗前\n[00:05.00]让下一句，轻轻唱起\n[00:09.00]沿着旋律慢慢走";
        QList<qint64> tracks;
        for (const auto &title : {"听见微光", "晚风来信", "雨后漫步", "远方灯塔"}) {
            const auto id = library.addTrack({{"title", title},
                                              {"artist", "BiliWave 界面测试"},
                                              {"path", audio + QString::number(tracks.size())},
                                              {"duration", 12000},
                                              {"codec", "flac"}});
            tracks << id;
            library.setLyrics(id, lrc, "界面测试");
        }
        const auto local = library.addTrack({{"title", "歌词配置示例"},
                                             {"artist", "BiliWave 界面测试"},
                                             {"path", audio},
                                             {"duration", 12000},
                                             {"staged", 1}});
        library.setLyrics(local, lrc, "界面测试");
        for (const auto &name : {"晨间漫步", "夜航电台", "安静工作"}) {
            const auto playlist = library.createPlaylist(name);
            library.addToPlaylist(playlist, {tracks[0], tracks[1]});
        }
        MainWindow window(&library, &settings);
        window.resize(1240, 850);
        window.show();
        auto downloads = window.findChild<QListWidget *>("downloadJobs");
        downloads->itemWidget(downloads->item(0))->findChild<QPushButton *>("openDownloadedTrack")->click();
        auto player = window.player();
        player->setQueue({local, tracks[1], local}, local, false);
        QTRY_VERIFY_WITH_TIMEOUT(player->media()->duration() > 11000, 15000);
        const auto playbackState = player->media()->playbackState();
        const auto entries = player->entries();
        auto queue = window.findChild<LyricQueue *>();
        QSignalSpy finished(queue, &LyricQueue::taskFinished);
        queue->enqueue(local, {{"operation", "lrc"}, {"lrc", lrc}});
        QVERIFY(finished.wait(10000));
        window.showSearchResults(
            {{{"title", "听见微光 · 界面测试"}, {"duration", 12000}, {"uploader", "BiliWave"}},
             {{"title", "晚风来信 · 界面测试"}, {"duration", 12000}, {"uploader", "BiliWave"}}},
            1, true, false);
        auto contrast = [](const QColor &a, const QColor &b) {
            auto luminance = [](const QColor &c) {
                auto linear = [](double v) {
                    return v <= .04045 ? v / 12.92 : std::pow((v + .055) / 1.055, 2.4);
                };
                return .2126 * linear(c.redF()) + .7152 * linear(c.greenF()) + .0722 * linear(c.blueF());
            };
            const auto x = luminance(a), y = luminance(b);
            return (qMax(x, y) + .05) / (qMin(x, y) + .05);
        };
        for (const auto &theme : {QString("dark"), QString("pink")}) {
            QDir shots(resourceRoot() + "/docs/screenshots/themes/" + theme);
            QVERIFY(shots.mkpath("."));
            window.navigate(4);
            window.findChild<QPushButton *>("settingsCategory0")->click();
            QTest::mouseClick(settingChoice(window, "settingsTheme", theme), Qt::LeftButton);
            QCOMPARE(settings.value("ui/theme").toString(), theme);
            QCOMPARE(window.palette().color(QPalette::Window),
                     UiTheme::palette(theme).color(QPalette::Window));
            QVERIFY(contrast(window.palette().color(QPalette::Text),
                             window.palette().color(QPalette::Base)) >= 4.5);
            QVERIFY(contrast(window.palette().color(QPalette::HighlightedText),
                             window.palette().color(QPalette::Highlight)) >= 3.0);
            auto capture = [&](QWidget *widget, const QString &name) {
                QCoreApplication::processEvents();
                return widget->grab().save(shots.filePath(name + ".png"));
            };
            for (int page = 0; page < 4; ++page) {
                window.navigate(page);
                QVERIFY(capture(&window, QString("page-%1").arg(page)));
                if (page == 2) {
                    auto note =
                        window.findChild<QWidget *>(QString("playingNote_%1").arg(player->currentEntry()));
                    QVERIFY(note);
                    const auto pixels = note->grab().toImage();
                    const auto accent = UiTheme::palette(theme).color(QPalette::Accent);
                    int accentInk = 0;
                    for (int y = 0; y < pixels.height(); ++y)
                        for (int x = 0; x < pixels.width(); ++x)
                            accentInk += pixels.pixelColor(x, y) == accent;
                    QVERIFY(accentInk > 10);
                }
            }
            auto cards = window.findChild<PlaylistView *>("playlistList");
            window.navigate(1);
            QTest::mouseClick(cards->viewport(), Qt::LeftButton, Qt::NoModifier,
                              cards->visualItemRect(cards->item(0)).center());
            QVERIFY(capture(&window, "playlist-detail"));
            window.findChild<QPushButton *>("playlistBack")->click();
            window.navigate(2);
            window.findChild<QPushButton *>("playbackPlaylistsTab")->click();
            QVERIFY(capture(&window, "playback-playlists"));
            window.findChild<QPushButton *>("playbackSongsTab")->click();
            window.navigate(4);
            for (int section = 0; section < 4; ++section) {
                window.findChild<QPushButton *>(QString("settingsCategory%1").arg(section))->click();
                QVERIFY(capture(&window, QString("settings-%1").arg(section)));
            }
            window.navigate(3);
            {
                LyricWizard wizard(&library, &settings, local, lrc, &window);
                wizard.show();
                QCOMPARE(wizard.palette().color(QPalette::Window), window.palette().color(QPalette::Window));
                QVERIFY(capture(&wizard, "lyrics-import"));
                wizard.close();
            }
            {
                LyricWizard wizard(&library, &settings, local, {}, &window);
                wizard.findChild<QLineEdit *>("lyricVideoInput")->clear();
                wizard.findChild<QComboBox *>("lyricMethod")->setCurrentIndex(1);
                wizard.show();
                auto preview = wizard.findChild<LyricCropPreview *>();
                QImage frame(640, 360, QImage::Format_RGB32);
                frame.fill(QColor("#1d3046"));
                QPainter p(&frame);
                p.setPen(Qt::white);
                p.drawText(frame.rect().adjusted(0, 250, 0, 0), Qt::AlignCenter,
                           "字幕范围测试 / Caption preview");
                p.end();
                preview->setImage(frame);
                QVERIFY(capture(&wizard, "subtitle-crop"));
                QCOMPARE(wizard.findChild<QScrollArea *>()->horizontalScrollBar()->maximum(), 0);
                wizard.close();
            }
            bool reviewed = false;
            QTimer::singleShot(0, &window, [&] {
                auto dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
                if (dialog) {
                    reviewed = capture(dialog, "lyric-review");
                    auto lines = dialog->findChild<QTreeWidget *>("queuedLyricLines");
                    QCOMPARE(lines->topLevelItemCount(), 3);
                    auto arrows = lines->itemWidget(lines->topLevelItem(0), 2)->findChildren<QPushButton *>();
                    arrows[0]->click();
                    QCOMPARE(
                        Lrc::parse(dialog->findChild<QPlainTextEdit *>("queuedLyricResult")->toPlainText())[0]
                            .ms,
                        900);
                    lines->topLevelItem(0)->setText(0, "00:02.345");
                    QCOMPARE(
                        Lrc::parse(dialog->findChild<QPlainTextEdit *>("queuedLyricResult")->toPlainText())[0]
                            .ms,
                        2345);
                    QCOMPARE(library.track(local).value("lrc").toString(), lrc);
                    dialog->reject();
                }
            });
            window.findChild<QPushButton *>("reviewQueuedLyrics")->click();
            QVERIFY(reviewed);
            QCOMPARE(queue->taskState(0), QString("待应用"));
            QCOMPARE(queue->count(), 1);
            const auto after = player->entries();
            QCOMPARE(after.size(), entries.size());
            for (int i = 0; i < after.size(); ++i)
                QCOMPARE(after[i].key, entries[i].key);
            QCOMPARE(player->media()->playbackState(), playbackState);
            window.resize(1040, 650);
            for (int page : {0, 3, 4}) {
                window.navigate(page);
                QVERIFY(capture(&window, QString("compact-page-%1").arg(page)));
                QVERIFY(window.width() <= 1040);
                if (page == 3) {
                    auto button = window.findChild<QPushButton *>("matchLyrics");
                    auto module = window.findChild<QWidget *>("workbenchPage");
                    QVERIFY(module->rect().contains(button->mapTo(module, button->rect().center())));
                }
            }
            window.resize(1240, 850);
        }
        MainWindow restored(&library, &settings);
        QCOMPARE(restored.property("uiTheme").toString(), QString("pink"));
        QVERIFY(settingChoice(restored, "settingsTheme", "pink")->isChecked());
        player->media()->stop();
    }
    void settingsCategoriesSaveChoicesWithoutWheelChanges() {
        QTemporaryDir dir(QDir::current().filePath("settings-categories-XXXXXX"));
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        settings.setValue("lyrics/autoFetch", false);
        settings.setValue("windows/showFloat", false);
        settings.setValue("windows/showLyrics", false);
        settings.setValue("ui/navigationVersion", 2);
        settings.setValue("ui/page", 3);
        {
            MainWindow window(&library, &settings);
            window.resize(1450, 900);
            window.show();
            auto pages = window.findChild<QStackedWidget *>("modulePages");
            QCOMPARE(pages->currentIndex(), 4);
            auto cpu = settingChoice(window, "settingsOcrDevice", "cpu");
            QVERIFY(cpu);
            QTest::mouseClick(cpu, Qt::LeftButton);
            QVERIFY(cpu->isChecked());
            QVERIFY(!settingChoice(window, "settingsOcrDevice", "auto")->isChecked());
            const auto center = cpu->rect().center();
            QWheelEvent wheel(center, cpu->mapToGlobal(center), QPoint(), QPoint(0, 120), Qt::NoButton,
                              Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(cpu, &wheel);
            QVERIFY(cpu->isChecked());
            settingChoice(window, "settingsOcrLanguage", "ru_zh")->click();
            settingChoice(window, "settingsAlignLanguage", "ru")->click();
            settingChoice(window, "settingsLyricSource", "offline")->click();
            auto sections = window.findChild<QStackedWidget *>("settingsSections");
            QDir shots(resourceRoot() + "/docs/screenshots");
            shots.mkpath(".");
            for (int i = 0; i < 4; ++i) {
                auto button = window.findChild<QPushButton *>(QString("settingsCategory%1").arg(i));
                button->click();
                QTest::qWait(40);
                QCOMPARE(sections->currentIndex(), i);
                QVERIFY(window.grab().save(shots.filePath(QString("settings-category-%1.png").arg(i))));
            }
            auto directory = window.findChild<QLineEdit *>("settingsPath_download_directory");
            directory->setText(dir.filePath("music"));
            window.findChild<QPushButton *>("settingsCategory2")->click();
            window.findChild<QSlider *>("settingsLyricOpacity")->setValue(75);
            auto category = window.findChild<QPushButton *>("settingsCategory2");
            const auto top = category->mapTo(&window, QPoint()).y();
            qobject_cast<QScrollArea *>(sections->currentWidget())->verticalScrollBar()->setValue(10000);
            QCOMPARE(category->mapTo(&window, QPoint()).y(), top);
            QVERIFY(category->isVisible());
            window.findChild<QPushButton *>("saveSettings")->click();
            QCOMPARE(settings.value("lyrics/ocrDevice").toString(), QString("cpu"));
            QCOMPARE(settings.value("lyrics/ocrLanguage").toString(), QString("ru_zh"));
            QCOMPARE(settings.value("lyrics/language").toString(), QString("ru"));
            QCOMPARE(settings.value("lyrics/source").toString(), QString("offline"));
            QCOMPARE(settings.value("lyrics/opacity").toInt(), 75);
            QCOMPARE(settings.value("download/directory").toString(), directory->text());
        }
        MainWindow restored(&library, &settings);
        QVERIFY(settingChoice(restored, "settingsOcrDevice", "cpu")->isChecked());
        QVERIFY(settingChoice(restored, "settingsOcrLanguage", "ru_zh")->isChecked());
        QCOMPARE(restored.findChild<QSlider *>("settingsLyricOpacity")->value(), 75);
    }
    void workbenchIsSeparateAndDownloadArrowNavigatesToIt() {
        QTemporaryDir dir(QDir::current().filePath("workbench-module-XXXXXX"));
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        settings.setValue("lyrics/autoFetch", false);
        settings.setValue("windows/showFloat", false);
        settings.setValue("windows/showLyrics", false);
        MainWindow window(&library, &settings);
        window.show();
        window.navigate(0);
        auto pages = window.findChild<QStackedWidget *>("modulePages");
        QVERIFY(!pages->widget(0)->findChild<QListWidget *>("timedLyricPreview"));
        QVERIFY(!pages->widget(0)->findChild<LyricQueue *>());
        auto queue = pages->widget(3)->findChild<LyricQueue *>();
        QVERIFY(queue);
        QVERIFY(pages->widget(3)->findChild<QListWidget *>("timedLyricPreview"));
        auto nav = window.findChild<QPushButton *>("navWorkbench");
        QVERIFY(nav->mapTo(&window, QPoint()).y() >
                window.findChild<QPushButton *>("navPlayback")->mapTo(&window, QPoint()).y());
        const auto id =
            library.addTrack({{"title", "已下载的歌曲"}, {"path", dir.filePath("test.m4a")}, {"staged", 1}});
        window.imports()->completed("module-test", id);
        QCOMPARE(pages->currentIndex(), 0);
        auto jobs = window.findChild<QListWidget *>("downloadJobs");
        jobs->itemWidget(jobs->item(0))->findChild<QPushButton *>("openDownloadedTrack")->click();
        QCOMPARE(pages->currentIndex(), 3);
        QCOMPARE(window.findChild<QComboBox *>("lyricTrackSelector")->currentData().toLongLong(), id);
        QSignalSpy finished(queue, &LyricQueue::taskFinished);
        queue->enqueue(id, {{"operation", "lrc"}, {"lrc", "[00:01.00]后台结果"}});
        window.navigate(0);
        QVERIFY(finished.wait(10000));
        QCOMPARE(pages->currentIndex(), 0);
        QCOMPARE(queue->taskState(0), QString("待应用"));
        QVERIFY(library.track(id).value("lrc").toString().isEmpty());
        QDir shots(resourceRoot() + "/docs/screenshots");
        shots.mkpath(".");
        QVERIFY(window.grab().save(shots.filePath("separate-search.png")));
        nav->click();
        QCOMPARE(pages->currentIndex(), 3);
        QVERIFY(window.grab().save(shots.filePath("separate-workbench.png")));
    }
    void musicOnlyDragKeepsPlaylistAndQueueDrops() {
#ifdef Q_OS_WIN
        QTemporaryDir dir(QDir::current().filePath("music-drag-XXXXXX"));
        Library library(dir.filePath("data"));
        const auto id = library.addTrack({{"title", "拖动歌曲"}, {"path", dir.filePath("song.flac")}});
        QWidget window;
        auto layout = new QHBoxLayout(&window);
        auto source = new DragTrackView;
        auto member = new DragTrackView;
        member->playlist = 7;
        member->acceptsTrackCopy = true;
        auto cards = new PlaylistView;
        cards->setAcceptDrops(true);
        cards->setDragDropMode(QAbstractItemView::DropOnly);
        auto queue = new PlaybackQueueView(&library);
        for (auto widget : QList<QWidget *>{source, cards, member, queue})
            layout->addWidget(widget);
        auto song = new QTreeWidgetItem(source, {"拖动歌曲"});
        song->setData(0, Qt::UserRole, id);
        song->setSelected(true);
        auto card = new QListWidgetItem("目标歌单", cards);
        card->setData(Qt::UserRole, qint64(7));
        auto first = new QTreeWidgetItem(member, {"第一首"});
        first->setData(0, Qt::UserRole, qint64(101));
        auto second = new QTreeWidgetItem(member, {"第二首"});
        second->setData(0, Qt::UserRole, qint64(102));
        QSignalSpy cardDrops(cards, &PlaylistView::tracksDropped);
        QSignalSpy memberDrops(member, &TrackView::tracksAdded);
        QSignalSpy reordered(member, &TrackView::reordered);
        QSignalSpy queueDrops(queue, &PlaybackQueueView::tracksDropped);
        window.resize(1350, 550);
        window.show();
        window.raise();
        window.activateWindow();
        QTest::qWait(100);
        bool sawNote = false, sawBlankCursor = false;
        auto drop = [&](DragTrackView *from, QWidget *viewport, QPoint position) {
            const auto global = viewport->mapToGlobal(position);
            QTimer::singleShot(30, &window, [&, from, global] {
                for (auto widget : QApplication::topLevelWidgets())
                    if (widget->objectName() == "musicDragIndicator" && widget->isVisible())
                        sawNote = true;
                sawBlankCursor = QApplication::overrideCursor() &&
                                 QApplication::overrideCursor()->shape() == Qt::BlankCursor;
                const auto local = from->viewport()->mapFromGlobal(global);
                QMouseEvent release(QEvent::MouseButtonRelease, QPointF(local), QPointF(global),
                                    Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
                QApplication::sendEvent(from->viewport(), &release);
            });
            from->startDrag(Qt::CopyAction);
        };
        drop(source, cards->viewport(), cards->visualItemRect(card).center());
        QVERIFY(sawNote && sawBlankCursor);
        QCOMPARE(cardDrops.size(), 1);
        QCOMPARE(cardDrops.first()[0].toLongLong(), 7);
        QCOMPARE(qvariant_cast<QList<qint64>>(cardDrops.first()[1]), QList<qint64>{id});
        drop(source, member->viewport(), member->viewport()->rect().center());
        QCOMPARE(memberDrops.size(), 1);
        drop(source, queue->viewport(), queue->viewport()->rect().center());
        QCOMPARE(queueDrops.size(), 1);
        first->setSelected(true);
        drop(member, member->viewport(), QPoint(20, member->viewport()->height() - 20));
        QCOMPARE(reordered.size(), 1);
        QCOMPARE(qvariant_cast<QList<qint64>>(reordered.first()[0]), QList<qint64>({102, 101}));
        QTimer::singleShot(30, &window, [source] { QTest::keyClick(source, Qt::Key_Escape); });
        source->startDrag(Qt::CopyAction);
        QVERIFY(!QApplication::overrideCursor());
        QCOMPARE(cardDrops.size(), 1);
        QCOMPARE(source->topLevelItemCount(), 1);
#endif
    }
    void discardDownloadedTaskAndUnifiedNavigation() {
        QTemporaryDir dir(QDir::current().filePath("discard-download-XXXXXX"));
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        settings.setValue("lyrics/autoFetch", false);
        settings.setValue("windows/showFloat", false);
        settings.setValue("windows/showLyrics", false);
        MainWindow window(&library, &settings);
        window.show();
        window.navigate(0);
        const auto token = QUuid::createUuid().toString(QUuid::WithoutBraces);
        const auto raw = dir.filePath(token + ".source.m4s"), audio = dir.filePath(token + ".m4a");
        for (const auto &path : {raw, audio, raw + ".part", dir.filePath("keep.flac")}) {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("test");
        }
        const auto id = library.addTrack({{"title", "待删除下载"},
                                          {"path", audio},
                                          {"original", raw},
                                          {"source", "https://www.bilibili.com/video/BV13x41117TL"},
                                          {"staged", 1}});
        window.imports()->completed(token, id);
        auto jobs = window.findChild<QListWidget *>("downloadJobs");
        QCOMPARE(jobs->count(), 1);
        auto cell = jobs->itemWidget(jobs->item(0));
        auto remove = cell->findChild<QPushButton *>("discardDownloadedTrack");
        auto collect = cell->findChild<QPushButton *>("collectDownloadedTrack");
        auto open = cell->findChild<QPushButton *>("openDownloadedTrack");
        QCoreApplication::processEvents();
        QVERIFY(remove->geometry().right() < collect->geometry().left());
        QVERIFY(collect->geometry().right() < open->geometry().left());
        QVERIFY(remove->geometry().top() >
                cell->findChild<QLabel *>("downloadTaskTitle")->geometry().bottom());
        QVERIFY(!remove->icon().isNull() && !collect->icon().isNull() && !open->icon().isNull());
        QDir screenshots(resourceRoot() + "/docs/screenshots");
        screenshots.mkpath(".");
        QVERIFY(window.grab().save(screenshots.filePath("task-delete-and-navigation.png")));
        open->click();
        auto tasks = window.findChild<LyricQueue *>("lyricTaskQueue");
        tasks->enqueue(id, {{"operation", "lrc"}, {"lrc", "[00:01.00]歌词"}});
        remove->click();
        QCOMPARE(jobs->count(), 0);
        QCOMPARE(tasks->count(), 0);
        QVERIFY(library.track(id).isEmpty());
        QVERIFY(!QFile::exists(raw) && !QFile::exists(audio) && !QFile::exists(raw + ".part"));
        QVERIFY(QFile::exists(dir.filePath("keep.flac")));
        const auto local = library.addTrack({{"title", "本地原文件"},
                                             {"path", dir.filePath("keep.flac")},
                                             {"original", dir.filePath("keep.flac")},
                                             {"staged", 1}});
        window.imports()->completed("local", local);
        jobs->itemWidget(jobs->item(0))->findChild<QPushButton *>("discardDownloadedTrack")->click();
        QVERIFY(QFile::exists(dir.filePath("keep.flac")));
        QCOMPARE(jobs->count(), 0);
        const auto remux = dir.filePath(QUuid::createUuid().toString(QUuid::WithoutBraces) + ".m4a");
        {
            QFile file(remux);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("remux");
        }
        const auto remuxTrack = library.addTrack({{"title", "本地封装输出"},
                                                  {"path", remux},
                                                  {"original", dir.filePath("keep.flac")},
                                                  {"staged", 1}});
        window.imports()->completed("remux", remuxTrack);
        jobs->itemWidget(jobs->item(0))->findChild<QPushButton *>("discardDownloadedTrack")->click();
        QVERIFY(!QFile::exists(remux));
        QVERIFY(QFile::exists(dir.filePath("keep.flac")));
        window.navigate(0);
        const auto direct =
            library.addTrack({{"title", "直接收录"}, {"path", dir.filePath("keep.flac")}, {"staged", 1}});
        window.imports()->completed("direct", direct);
        jobs->itemWidget(jobs->item(0))->findChild<QPushButton *>("collectDownloadedTrack")->click();
        QCOMPARE(jobs->count(), 0);
        QVERIFY(!library.track(direct).value("staged").toBool());
        QCOMPARE(window.findChild<QStackedWidget *>("modulePages")->currentIndex(), 0);
        QVERIFY(QFile::exists(dir.filePath("keep.flac")));
        window.imports()->status("failed", "失败任务", "失败：测试错误");
        cell = jobs->itemWidget(jobs->item(0));
        QVERIFY(!cell->findChild<QPushButton *>("collectDownloadedTrack")->isEnabled());
        cell->findChild<QPushButton *>("discardDownloadedTrack")->click();
        QCOMPARE(jobs->count(), 0);
    }
    void duplicateQueueControlsAndPlayingAnimation() {
        QTemporaryDir dir(QDir::current().filePath("queue-controls-XXXXXX"));
        QProcess ffmpeg;
        const auto path = dir.filePath("tone.flac");
        ffmpeg.start(QDir(resourceRoot()).filePath("tools/ffmpeg.exe"),
                     {"-v", "error", "-y", "-f", "lavfi", "-i", "sine=frequency=440:duration=12", path});
        QVERIFY(ffmpeg.waitForFinished(15000) && ffmpeg.exitCode() == 0);
        Library library(dir.filePath("data"));
        const auto id = library.addTrack({{"title", "同一首歌曲"}, {"path", path}, {"duration", 12000}});
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        settings.setValue("lyrics/autoFetch", false);
        settings.setValue("windows/showFloat", false);
        settings.setValue("windows/showLyrics", false);
        MainWindow window(&library, &settings);
        window.resize(1500, 950);
        window.show();
        window.navigate(2);
        auto player = window.player();
        player->audio()->setVolume(0);
        player->setMode(Player::Sequential);
        player->setQueue({id, id, id}, id, false);
        auto entries = player->entries();
        auto queue = window.findChild<PlaybackQueueView *>("playbackQueue");
        QCOMPARE(queue->topLevelItemCount(), 3);
        QCOMPARE(queue->columnCount(), 1);
        QVERIFY(!window.findChild<QPushButton *>("enqueuePlaybackSelection"));
        QCOMPARE(window.findChild<QWidget *>("playbackQueuePane")->minimumWidth(), 480);
        QCOMPARE(queue->topLevelItem(2)->data(0, Qt::UserRole + 1).toULongLong(), entries[0].key);
        auto control = [&](const QString &name, quint64 key) {
            return window.findChild<QPushButton *>(name + "_" + QString::number(key));
        };
        QVERIFY(!control("queueTop", entries[0].key)->isEnabled());
        QVERIFY(!control("queueTop", entries[1].key)->icon().isNull());
        QVERIFY(control("queueTop", entries[1].key)->text().isEmpty());
        control("queueBottom", entries[2].key)->click();
        QCOMPARE(player->pendingEntries().first().key, entries[2].key);
        control("queueUp", entries[2].key)->click();
        QCOMPARE(player->pendingEntries().last().key, entries[2].key);
        control("queueDown", entries[2].key)->click();
        QCOMPARE(player->pendingEntries().first().key, entries[2].key);
        control("queueTop", entries[2].key)->click();
        QCOMPARE(player->pendingEntries().last().key, entries[2].key);
        control("dequeueEntry", entries[1].key)->click();
        QCOMPARE(player->queue(), QList<qint64>({id, id}));
        QCOMPARE(player->currentEntry(), entries[0].key);
        QCOMPARE(library.tracks().size(), 1);
        window.findChild<QPushButton *>(QString("enqueueTrack_%1").arg(id))->click();
        window.findChild<QPushButton *>(QString("enqueueTrack_%1").arg(id))->click();
        QCOMPARE(player->queue().size(), 4);
        QTRY_VERIFY_WITH_TIMEOUT(player->media()->duration() > 11000, 15000);
        player->media()->setPosition(4000);
        player->jumpToEntry(entries[2].key);
        QTRY_VERIFY(player->media()->position() < 2000);
        const auto noteName = QString("playingNote_%1").arg(entries[2].key);
        QPointer<QWidget> note = queue->itemWidget(queue->topLevelItem(queue->topLevelItemCount() - 1), 0)
                                     ->findChild<QWidget *>(noteName);
        QVERIFY(note);
        QTRY_VERIFY(note && note->property("rotating").toBool());
        const int angle = note->property("angle").toInt();
        QTRY_VERIFY(note->property("angle").toInt() != angle);
        player->media()->pause();
        QTRY_VERIFY(!note->property("rotating").toBool());
        const int pausedAngle = note->property("angle").toInt();
        QTest::qWait(100);
        QCOMPARE(note->property("angle").toInt(), pausedAngle);
        QCOMPARE(queue->topLevelItem(queue->topLevelItemCount() - 1)->data(0, Qt::UserRole + 1).toULongLong(),
                 player->currentEntry());
        const auto drag = musicDragPixmap(1, 2.);
        QCOMPARE(drag.devicePixelRatio(), 2.);
        QCOMPARE(drag.size(), QSize(88, 88));
        const auto image = drag.toImage().convertToFormat(QImage::Format_ARGB32);
        QVERIFY(image.pixelColor(0, 0).alpha() == 0);
        int ink = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x)
                ink += image.pixelColor(x, y).alpha() > 20;
        QVERIFY(ink > 100);
        QDir screenshots(resourceRoot() + "/docs/screenshots");
        screenshots.mkpath(".");
        QVERIFY(window.grab().save(screenshots.filePath("duplicate-queue-controls.png")));
        QVERIFY(drag.save(screenshots.filePath("music-drag-icon.png")));
        player->setMode(Player::ListLoop);
        player->setQueue({id, id}, id, false);
        const auto endEntries = player->entries();
        QTRY_VERIFY_WITH_TIMEOUT(player->media()->duration() > 11000, 15000);
        player->media()->setPosition(player->media()->duration() - 100);
        player->media()->play();
        QTRY_COMPARE_WITH_TIMEOUT(player->currentEntry(), endEntries[1].key, 10000);
        QCOMPARE(player->queue().size(), 1);
        QTRY_VERIFY_WITH_TIMEOUT(player->media()->duration() > 11000, 15000);
        player->media()->setPosition(player->media()->duration() - 100);
        player->media()->play();
        QTRY_VERIFY_WITH_TIMEOUT(player->queue().isEmpty(), 10000);
        QCOMPARE(queue->topLevelItemCount(), 0);
        QCOMPARE(library.tracks().size(), 1);
        player->setQueue({});
    }
    void explicitDownloadWorkbenchEntryAndBilingualDisplay() {
        QTemporaryDir dir(QDir::current().filePath("download-gateway-XXXXXX"));
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        settings.setValue("lyrics/autoFetch", false);
        settings.setValue("windows/showFloat", false);
        settings.setValue("windows/showLyrics", false);
        settings.setValue("lyrics/color", "#ff0000");
        settings.setValue("lyrics/secondaryColor", "#0000ff");
        MainWindow window(&library, &settings);
        window.resize(1800, 1100);
        window.show();
        window.navigate(0);
        auto selector = window.findChild<QComboBox *>("lyricTrackSelector");
        auto jobs = window.findChild<QListWidget *>("downloadJobs");
        QProcess ffmpeg;
        ffmpeg.start(QDir(resourceRoot()).filePath("tools/ffmpeg.exe"),
                     {"-v", "error", "-y", "-f", "lavfi", "-i", "sine=frequency=440:duration=6",
                      dir.filePath("one.flac")});
        QVERIFY(ffmpeg.waitForFinished(15000) && ffmpeg.exitCode() == 0);
        QVERIFY(QFile::copy(dir.filePath("one.flac"), dir.filePath("two.flac")));
        const auto first =
            library.addTrack({{"title", "第一首"}, {"path", dir.filePath("one.flac")}, {"staged", 1}});
        window.imports()->status("one", "第一首", "完成");
        window.imports()->completed("one", first);
        QCOMPARE(selector->currentData().toLongLong(), 0);
        QCOMPARE(jobs->count(), 1);
        auto arrow = jobs->itemWidget(jobs->item(0))->findChild<QPushButton *>("openDownloadedTrack");
        QVERIFY(arrow);
        arrow->click();
        QCOMPARE(selector->currentData().toLongLong(), first);
        const QVariantMap url{{"title", "URL 歌曲"}, {"url", "https://www.bilibili.com/video/BV13x41117TL"}};
        window.showSearchResults({url}, 1, false, false);
        auto results = window.findChild<QTreeWidget *>("searchResults");
        QTest::mouseClick(results->viewport(), Qt::LeftButton, Qt::NoModifier,
                          results->visualItemRect(results->topLevelItem(0)).center());
        window.findChild<QPushButton *>("collectSearchUrl")->click();
        QCOMPARE(selector->currentData().toLongLong(), first);
        QCOMPARE(selector->count(), 2);
        const auto second =
            library.addTrack({{"title", "第二首"}, {"path", dir.filePath("two.flac")}, {"staged", 1}});
        window.imports()->completed("two", second);
        QCOMPARE(selector->currentData().toLongLong(), first);
        QCoreApplication::processEvents();
        QTest::mouseClick(jobs->viewport(), Qt::LeftButton, Qt::NoModifier,
                          jobs->visualItemRect(jobs->item(0)).center());
        QTest::mouseDClick(jobs->viewport(), Qt::LeftButton, Qt::NoModifier,
                           jobs->visualItemRect(jobs->item(0)).center());
        QCOMPARE(selector->currentData().toLongLong(), second);
        library.setWorkbenchLyrics(second, "[00:00.00]Привет, мир!\n[00:00.00]你好，世界！\n[00:04.00]", "测试");
        QCOMPARE(window.findChild<QListWidget *>("timedLyricPreview")->count(), 2);
        window.findChild<QPushButton *>("commitWithoutLyrics")->click();
        QCOMPARE(window.findChild<QLabel *>("libraryImportStatus")->text(), QString("已导入乐库"));
        for (auto button : window.findChildren<QPushButton *>())
            QVERIFY(button->text() != "取消匹配");
        QVERIFY(!window.findChild<QPushButton *>("recognizeSubtitles"));
        auto match = window.findChild<QPushButton *>("matchLyrics");
        auto trial = window.findChild<QPushButton *>("trialLyrics");
        QCOMPARE(match->mapTo(&window, QPoint()).y(), trial->mapTo(&window, QPoint()).y());
        QCOMPARE(jobs->count(), 1);
        window.navigate(4);
        window.findChild<QSlider *>("settingsLyricOpacity")->setValue(80);
        window.findChild<QSlider *>("settingsLyricSecondaryOpacity")->setValue(30);
        window.findChild<QSpinBox *>("settingsLyricPrimarySize")->setValue(28);
        window.findChild<QSpinBox *>("settingsLyricSecondarySize")->setValue(18);
        window.findChild<QSpinBox *>("settingsLyricHorizontalOffset")->setValue(140);
        window.findChild<QSpinBox *>("settingsLyricVerticalOffset")->setValue(85);
        QVERIFY(settingChoice(window, "settingsOcrLanguage", "en_zh"));
        QVERIFY(settingChoice(window, "settingsOcrLanguage", "ru_zh"));
        QVERIFY(settingChoice(window, "settingsOcrLanguage", "it_zh"));
        window.findChild<QPushButton *>("saveSettings")->click();
        QCOMPARE(settings.value("lyrics/secondaryOpacity").toInt(), 30);
        QCOMPARE(settings.value("lyrics/horizontalOffset").toInt(), 140);
        window.player()->setQueue({second}, second, false);
        auto image = window.overlay()->grab().toImage().convertToFormat(QImage::Format_ARGB32);
        int topAlpha = 0, lowerAlpha = 0, lowerLeft = image.width();
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x) {
                const auto c = image.pixelColor(x, y);
                if (c.alpha() > 10 && c.red() > 200)
                    topAlpha = qMax(topAlpha, c.alpha());
                if (c.alpha() > 10 && c.blue() > 200) {
                    lowerAlpha = qMax(lowerAlpha, c.alpha());
                    lowerLeft = qMin(lowerLeft, x);
                }
            }
        QVERIFY(qAbs(topAlpha - 204) <= 2);
        QVERIFY(qAbs(lowerAlpha - 77) <= 2);
        QVERIFY(lowerLeft >= 160 * image.devicePixelRatio());
        QDir screenshots(resourceRoot() + "/docs/screenshots");
        screenshots.mkpath(".");
        QVERIFY(window.overlay()->grab().save(screenshots.filePath("bilingual-lyrics.png")));
        window.findChild<QPushButton *>("settingsCategory2")->click();
        QCoreApplication::processEvents();
        QVERIFY(window.grab().save(screenshots.filePath("bilingual-settings.png")));
        window.navigate(0);
        QVERIFY(window.grab().save(screenshots.filePath("download-workbench-entry.png")));
        window.player()->setQueue({});
    }
    void searchActionsTimelineAndCandidateProgression() {
        QTemporaryDir dir(QDir::current().filePath("workbench-actions-XXXXXX"));
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        settings.setValue("lyrics/autoFetch", false);
        settings.setValue("windows/showFloat", false);
        settings.setValue("windows/showLyrics", false);
        MainWindow window(&library, &settings);
        window.resize(2100, 1100);
        window.navigate(0);
        window.show();
        QTest::qWait(80);
        const QVariantMap result{{"title", "测试歌曲"},
                                 {"uploader", "测试UP"},
                                 {"part", 2},
                                 {"url", "https://www.bilibili.com/video/BV13x41117TL?p=2"}};
        window.showSearchResults({result}, 1, false, true);
        auto results = window.findChild<QTreeWidget *>("searchResults");
        QCOMPARE(results->headerItem()->text(3), QString("UP主"));
        QCOMPARE(results->topLevelItem(0)->text(3), QString("测试UP"));
        QTest::mouseClick(results->viewport(), Qt::LeftButton, Qt::NoModifier,
                          results->visualItemRect(results->topLevelItem(0)).center());
        bool prompted = false;
        QTimer::singleShot(40, &window, [&] {
            auto dialog = window.findChild<QMessageBox *>("searchResultAction");
            if (!dialog)
                return;
            prompted = true;
            for (auto button : dialog->buttons())
                if (button->text() == "URL 收录") {
                    button->click();
                    return;
                }
        });
        QTest::mouseDClick(results->viewport(), Qt::LeftButton, Qt::NoModifier,
                           results->visualItemRect(results->topLevelItem(0)).center());
        QVERIFY(prompted);
        QCOMPARE(results->topLevelItemCount(), 1);
        QCOMPARE(library.tracks().size(), 1);
        auto jobs = window.findChild<QListWidget *>("downloadJobs");
        window.imports()->status("done", "已完成", "下载中");
        QCOMPARE(jobs->count(), 1);
        window.imports()->status("done", "已完成", "完成 · FLAC · 原轨已保留");
        QCOMPARE(jobs->count(), 1);
        window.imports()->status("failed", "失败歌曲", "失败：测试");
        QCOMPARE(jobs->count(), 2);
        window.imports()->status("cancelled", "取消歌曲", "已取消");
        QCOMPARE(jobs->count(), 2);
        QVERIFY(!window.findChild<QPushButton *>("openLyricWorkflow"));
        QVERIFY(!window.findChild<QPushButton *>("enqueueWorkbenchLyrics"));
        QCOMPARE(window.findChild<QComboBox *>("localLyricMethod")->count(), alignmentFeaturesEnabled() ? 2 : 1);
        const auto id =
            library.addTrack({{"title", "测试歌曲"}, {"path", dir.filePath("download.flac")}, {"staged", 1}});
        auto selector = window.findChild<QComboBox *>("lyricTrackSelector");
        QCOMPARE(selector->currentData().toLongLong(), 0);
        admitTrack(window, id);
        QCOMPARE(selector->currentData().toLongLong(), id);
        library.setWorkbenchLyrics(id, "[00:01.000]甲\n[00:02.000]乙\n[00:03.000]丙", "测试");
        window.findChild<QPushButton *>("lyricLine1_0")->click();
        QCOMPARE(Lrc::parse(library.workbenchTrack(id).value("lrc").toString())[1].ms, 1900);
        QVERIFY(
            window.findChild<QListWidget *>("timedLyricPreview")->item(1)->toolTip().startsWith("00:01.900"));
        window.findChild<QPushButton *>("lyricLine1_1")->click();
        QCOMPARE(Lrc::parse(library.workbenchTrack(id).value("lrc").toString())[1].ms, 2000);
        window.findChild<QPushButton *>("lyricLine0_2")->click();
        auto lines = Lrc::parse(library.workbenchTrack(id).value("lrc").toString());
        QCOMPARE(lines.size(), 2);
        QCOMPARE(lines[0].text, QString("乙"));
        QCOMPARE(lines[0].ms, 2000);
        QCOMPARE(lines[1].text, QString("丙"));
        QCOMPARE(lines[1].ms, 3000);
        auto timeline = window.findChild<QListWidget *>("timedLyricPreview");
        QTimer::singleShot(40, &window, [&] {
            auto menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
            if (!menu)
                return;
            QTimer::singleShot(40, &window, [&] {
                auto dialog = window.findChild<QInputDialog *>();
                if (dialog) {
                    dialog->setTextValue("新句");
                    dialog->accept();
                }
            });
            for (auto action : menu->actions())
                if (action->text() == "在下方添加歌词") {
                    action->trigger();
                    break;
                }
            menu->close();
        });
        timeline->customContextMenuRequested(timeline->visualItemRect(timeline->item(0)).center());
        lines = Lrc::parse(library.workbenchTrack(id).value("lrc").toString());
        QCOMPARE(lines.size(), 3);
        QCOMPARE(lines[1].text, QString("新句"));
        QCOMPARE(lines[1].ms, 2500);
        window.findChild<QComboBox *>("workbenchLyricSource")->setCurrentIndex(0);
        QList<QVariantMap> candidates;
        for (int i = 0; i < 3; ++i)
            candidates << QVariantMap{{"trackName", QString::number(i)},
                                      {"syncedLyrics", QString("[00:01.00]版本%1").arg(i)}};
        window.lyrics()->candidates(id, candidates);
        window.findChild<QPushButton *>("useLyricCandidate")->click();
        window.findChild<QPushButton *>("matchLyrics")->click();
        QVERIFY(library.workbenchTrack(id).value("lrc").toString().contains("版本1"));
        window.findChild<QPushButton *>("matchLyrics")->click();
        QVERIFY(library.workbenchTrack(id).value("lrc").toString().contains("版本2"));
        window.findChild<QPushButton *>("matchLyrics")->click();
        QVERIFY(library.workbenchTrack(id).value("lrc").toString().contains("版本2"));
        QDir screenshots(resourceRoot() + "/docs/screenshots");
        screenshots.mkpath(".");
        QCoreApplication::processEvents();
        QVERIFY(window.grab().save(screenshots.filePath("workbench-simplified.png")));
        window.findChild<QTreeWidget *>("lyricCandidates")->hide();
        window.findChild<QPushButton *>("useLyricCandidate")->hide();
        window.findChild<QPushButton *>("realignLyricCandidate")->hide();
        for (auto edit : window.findChildren<QPlainTextEdit *>())
            edit->hide();
        library.setWorkbenchLyrics(id, "[00:01.000]第一句歌词\n[00:04.000]下一句歌词\n[00:08.000]最后一句", "测试");
        QTest::qWait(50);
        const auto cell = timeline->itemWidget(timeline->item(0));
        QVERIFY(cell->height() >= 30);
        const auto lineLabel = cell->findChild<QLabel *>();
        QVERIFY(lineLabel->height() >= lineLabel->fontMetrics().height());
        QVERIFY(window.grab().save(screenshots.filePath("workbench-line-editor.png")));
        LyricWizard wizard(&library, &settings, id);
        auto method = wizard.findChild<QComboBox *>("lyricMethod");
        QCOMPARE(method->count(), 3);
        method->setCurrentIndex(0);
        QVERIFY(!wizard.findChild<QPushButton *>("enqueueLyricTask"));
        AiLinkCapture capture;
        QDesktopServices::setUrlHandler("https", &capture, "open");
        wizard.findChild<QPushButton *>("askLyricAi")->click();
        QDesktopServices::unsetUrlHandler("https");
        QCOMPARE(capture.opened.host(), QString("chat.deepseek.com"));
        const auto prompt = wizard.findChild<QPlainTextEdit *>("lyricAiPrompt")->toPlainText();
        QVERIFY(prompt.contains("歌名：测试歌曲"));
        QVERIFY(prompt.contains("不要编造"));
        wizard.show();
        QTest::qWait(50);
        QVERIFY(wizard.grab().save(screenshots.filePath("lyric-deepseek-import.png")));
        wizard.findChild<QPushButton *>("inputLyricText")->click();
        QVERIFY(wizard.findChild<QPlainTextEdit *>("lyricTextEditor"));
        window.navigate(4);
        auto compute = window.findChild<QButtonGroup *>("settingsOcrDevice");
        QVERIFY(compute);
        QCOMPARE(compute->checkedButton()->property("value").toString(), QString("auto"));
        settingChoice(window, "settingsOcrDevice", "cpu")->click();
        window.findChild<QPushButton *>("saveSettings")->click();
        QCOMPARE(settings.value("lyrics/ocrDevice").toString(), QString("cpu"));
        QCOMPARE(LyricJob::prepareConfig(&settings, {})["ocr_device"].toString(), QString("cpu"));
        QVERIFY(settingChoice(window, "settingsOcrLanguage", "ru"));
        QVERIFY(settingChoice(window, "settingsAlignLanguage", "ru"));
        settingChoice(window, "settingsOcrLanguage", "it")->click();
        settingChoice(window, "settingsAlignLanguage", "it")->click();
        window.findChild<QPushButton *>("saveSettings")->click();
        QCOMPARE(settings.value("lyrics/ocrLanguage").toString(), QString("it"));
        QCOMPARE(settings.value("lyrics/language").toString(), QString("it"));
        QCOMPARE(LyricJob::prepareConfig(&settings, {})["ocr_language"].toString(), QString("it"));
    }
    void libraryTrackWorkbenchEditingAndSave() {
        QTemporaryDir dir(QDir::current().filePath("library-workbench-XXXXXX"));
        QVERIFY(dir.isValid());
        const auto audio = dir.filePath("song.flac");
        QFile file(audio);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("test");
        file.close();
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        settings.setValue("lyrics/autoFetch", false);
        settings.setValue("windows/showFloat", false);
        settings.setValue("windows/showLyrics", false);
        const auto id = library.addTrack({{"title", "乐库歌曲"}, {"artist", "歌手"}, {"path", audio}});
        QVERIFY(library.setLyrics(id, "[00:01.000]甲\n[00:02.000]乙\n[00:03.000]丙", "测试"));
        MainWindow window(&library, &settings);
        window.resize(1700, 1000);
        window.show();
        window.navigate(1);
        auto tracks = window.findChild<TrackView *>("availableTracks");
        QVERIFY(tracks && tracks->topLevelItemCount() == 1);
        window.loadLibraryTrack(id);
        auto selector = window.findChild<QComboBox *>("lyricTrackSelector");
        auto commit = window.findChild<QPushButton *>("commitWithLyrics");
        auto skip = window.findChild<QPushButton *>("commitWithoutLyrics");
        auto save = window.findChild<QPushButton *>("saveWorkbenchTrack");
        auto timeline = window.findChild<QListWidget *>("timedLyricPreview");
        QCOMPARE(selector->currentData().toLongLong(), id);
        QVERIFY(!commit->isEnabled() && !skip->isEnabled());
        QVERIFY(save->isVisible() && save->isEnabled());
        QVERIFY(timeline && timeline->count() == 3);

        window.findChild<QLineEdit *>("lyricSongTitle")->setText("已保存歌曲");
        save->click();
        QCOMPARE(library.track(id).value("title").toString(), QString("已保存歌曲"));
        QCOMPARE(selector->currentData().toLongLong(), qint64(0));
        QCOMPARE(selector->count(), 1);
        QCOMPARE(timeline->count(), 0);
        QVERIFY(!save->isVisible());
    }
    void workbenchPanelProportionsAreSaved() {
        QTemporaryDir dir(QDir::current().filePath("panel-ratios-test-XXXXXX"));
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        settings.setValue("lyrics/autoFetch", false);
        settings.setValue("windows/showFloat", false);
        settings.setValue("windows/showLyrics", false);
        QList<int> expected;
        {
            MainWindow window(&library, &settings);
            window.resize(2000, 1000);
            window.navigate(0);
            window.show();
            QTest::qWait(80);
            window.navigate(3);
            QTest::qWait(50);
            auto split = window.findChild<QSplitter *>("workbenchPanels");
            QVERIFY(split);
            auto sizes = split->sizes();
            const double total = sizes[0] + sizes[1];
            QVERIFY(qAbs(sizes[0] / total - .75) < .01);
            QVERIFY(qAbs(sizes[1] / total - .25) < .01);
            auto handle = split->handle(1);
            const auto point = handle->rect().center();
            QTest::mousePress(handle, Qt::LeftButton, Qt::NoModifier, point);
            QTest::mouseMove(handle, point + QPoint(80, 0));
            QTest::mouseRelease(handle, Qt::LeftButton, Qt::NoModifier, point + QPoint(80, 0));
            QCoreApplication::processEvents();
            expected = split->sizes();
            QVERIFY(expected != sizes);
            QVERIFY(settings.value("ui/workbenchPanelRatios").toList()[0].toDouble() > .75);
            window.close();
        }
        {
            MainWindow window(&library, &settings);
            window.resize(2000, 1000);
            window.navigate(0);
            window.show();
            QTest::qWait(80);
            window.navigate(3);
            QTest::qWait(50);
            const auto restored = window.findChild<QSplitter *>("workbenchPanels")->sizes();
            for (int i = 0; i < 2; ++i)
                QVERIFY(qAbs(expected[i] - restored[i]) <= 2);
        }
    }
    void topProgressStrip() {
        QTemporaryDir dir(QDir::current().filePath("progress-test-XXXXXX"));
        const auto audio = dir.filePath("tone.flac");
        QProcess ffmpeg;
        ffmpeg.start(QDir(resourceRoot()).filePath("tools/ffmpeg.exe"),
                     {"-v", "error", "-n", "-f", "lavfi", "-i", "sine=frequency=440:duration=10", audio});
        QVERIFY(ffmpeg.waitForFinished(15000));
        QCOMPARE(ffmpeg.exitCode(), 0);
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        settings.setValue("lyrics/autoFetch", false);
        settings.setValue("windows/showFloat", false);
        settings.setValue("windows/showLyrics", false);
        settings.setValue("windows/float", QPoint(450, 320));
        const auto id = library.addTrack({{"title", "纯进度条"}, {"path", audio}, {"duration", 10000}});
        MainWindow window(&library, &settings);
        window.show();
        window.navigate(4);
        auto strip = window.floatBar();
        window.findChild<QCheckBox *>("settingsShowFloat")->setChecked(true);
        QTRY_VERIFY(strip->isVisible());
        const auto screen = QGuiApplication::primaryScreen();
        QCOMPARE(strip->pos(), screen->geometry().topLeft());
        QCOMPARE(strip->width(), screen->geometry().width());
        QCOMPARE(strip->height(), qCeil(24 / screen->devicePixelRatio()));
        QVERIFY(strip->windowFlags().testFlag(Qt::WindowTransparentForInput));
        QVERIFY(strip->windowFlags().testFlag(Qt::WindowDoesNotAcceptFocus));
        QVERIFY(strip->windowFlags().testFlag(Qt::WindowStaysOnTopHint));
        QVERIFY(strip->testAttribute(Qt::WA_TransparentForMouseEvents));
        QCOMPARE(strip->focusPolicy(), Qt::NoFocus);
        QVERIFY(!strip->acceptDrops());
        QVERIFY(strip->findChildren<QWidget *>().isEmpty());
        window.player()->setQueue({id}, id, false);
        QTRY_VERIFY_WITH_TIMEOUT(window.player()->media()->duration() >= 9900, 15000);
        window.player()->media()->setPosition(window.player()->media()->duration() / 2);
        QTest::qWait(80);
        auto image = strip->grab().toImage().convertToFormat(QImage::Format_ARGB32);
        const int played = qRound(image.width() * double(window.player()->media()->position()) /
                                  window.player()->media()->duration());
        QVERIFY(played > image.width() / 3 && played < image.width() * 2 / 3);
        const auto left = image.pixelColor(2, 0), right = image.pixelColor(played + 50, 0);
        QVERIFY(left.red() > left.blue() + 80);
        QVERIFY(right.blue() > right.red() + 80);
        QVERIFY(left.alpha() > 230);
        QVERIFY(image.pixelColor(2, 12).alpha() < left.alpha());
        QCOMPARE(image.pixelColor(2, image.height() - 1).alpha(), 0);
        QVERIFY(image.pixelColor(image.width() - 1, 0).alpha() > 230);
        QVERIFY(image.pixelColor(played - 50, 0).red() > image.pixelColor(played - 50, 0).blue() + 80);
        QVERIFY(qAbs(image.pixelColor(played, 0).red() - image.pixelColor(played, 0).blue()) < 40);
        auto gradientWidth = window.findChild<QSpinBox *>("progressGradientWidth");
        QVERIFY(gradientWidth);
        gradientWidth->setValue(160);
        QCOMPARE(settings.value("progress/gradientWidth").toInt(), 160);
        auto wide = strip->grab().toImage().convertToFormat(QImage::Format_ARGB32);
        QVERIFY(wide.pixelColor(played - 60, 0) != image.pixelColor(played - 60, 0));
        QVERIFY(wide.pixelColor(played + 60, 0) != image.pixelColor(played + 60, 0));
        QDir screenshots(QDir(resourceRoot()).filePath("docs/screenshots"));
        screenshots.mkpath(".");
        QVERIFY(image.save(screenshots.filePath("top-progress-red-blue.png")));
        gradientWidth->setValue(0);
        const auto oldPosition = window.player()->media()->position();
        QTest::mouseClick(strip, Qt::LeftButton, Qt::NoModifier, QPoint(strip->width() - 2, 1));
        QCOMPARE(window.player()->media()->position(), oldPosition);
        QCOMPARE(strip->pos(), screen->geometry().topLeft());
        auto leftInput = window.findChild<QLineEdit *>("progressLeftColor");
        auto rightInput = window.findChild<QLineEdit *>("progressRightColor");
        leftInput->setText("#00ff00");
        QVERIFY(QMetaObject::invokeMethod(leftInput, "editingFinished"));
        rightInput->setText("#ffff00");
        QVERIFY(QMetaObject::invokeMethod(rightInput, "editingFinished"));
        window.findChild<QSpinBox *>("progressFadeHeight")->setValue(32);
        QCOMPARE(settings.value("progress/leftColor").toString(), QString("#00ff00"));
        QCOMPARE(settings.value("progress/rightColor").toString(), QString("#ffff00"));
        QCOMPARE(settings.value("progress/fadeHeight").toInt(), 32);
        QCOMPARE(strip->height(), qCeil(32 / screen->devicePixelRatio()));
        auto custom = strip->grab().toImage().convertToFormat(QImage::Format_ARGB32);
        QVERIFY(custom.pixelColor(2, 0).green() > 230);
        QVERIFY(custom.pixelColor(played + 2, 0).red() > 230);
        QVERIFY(custom.pixelColor(played - 2, 0).red() < 20);
        QCOMPARE(custom.pixelColor(2, custom.height() - 1).alpha(), 0);
        QVERIFY(custom.save(screenshots.filePath("top-progress-custom.png")));
        window.player()->media()->setPosition(0);
        QTest::qWait(50);
        auto empty = strip->grab().toImage().convertToFormat(QImage::Format_ARGB32);
        QVERIFY(empty.pixelColor(2, 0).red() > 230);
        QVERIFY(empty.pixelColor(empty.width() - 1, 0).green() > 230);
        QCOMPARE(empty.pixelColor(2, empty.height() - 1).alpha(), 0);
        leftInput->setText("not a color");
        QVERIFY(QMetaObject::invokeMethod(leftInput, "editingFinished"));
        QCOMPARE(leftInput->text(), QString("#00ff00"));
        window.findChild<QPushButton *>("settingsCategory1")->click();
        QCoreApplication::processEvents();
        QVERIFY(window.grab().save(screenshots.filePath("top-progress-settings.png")));
        QList<qint64> repeats;
        for (int i = 0; i < 12; ++i)
            repeats << id;
        window.player()->enqueue(repeats);
        auto markerLength = window.findChild<QSpinBox *>("progressQueueMarkerLength");
        auto markerLimit = window.findChild<QSpinBox *>("progressQueueMarkerLimit");
        QCOMPARE(markerLimit->maximum(), 10);
        QCOMPARE(markerLimit->value(), 10);
        markerLength->setValue(14);
        window.player()->media()->setPosition(window.player()->media()->duration() / 2);
        auto markers = strip->grab().toImage().convertToFormat(QImage::Format_ARGB32);
        int groups = 0, whitePixels = 0;
        bool inside = false;
        for (int x = 0; x < markers.width(); ++x) {
            const auto c = markers.pixelColor(x, 0);
            const bool white = c.red() > 240 && c.green() > 240 && c.blue() > 240 && c.alpha() > 240;
            if (white && !inside)
                ++groups;
            whitePixels += white;
            inside = white;
        }
        QCOMPARE(groups, 10);
        QCOMPARE(whitePixels, 140);
        const int musicWidth = markers.width() - 10 * (14 + 4);
        QVERIFY(markers.pixelColor(musicWidth / 2 - 2, 0).green() > 240);
        QVERIFY(markers.pixelColor(musicWidth / 2 + 2, 0).red() > 240);
        auto markerColor = window.findChild<QLineEdit *>("progressQueueMarkerColor");
        markerColor->setText("#ff00ff");
        QVERIFY(QMetaObject::invokeMethod(markerColor, "editingFinished"));
        markerLength->setValue(20);
        markerLimit->setValue(4);
        markers = strip->grab().toImage().convertToFormat(QImage::Format_ARGB32);
        int colored = 0;
        for (int x = 0; x < markers.width(); ++x) {
            const auto c = markers.pixelColor(x, 0);
            colored += c.red() > 240 && c.blue() > 240 && c.green() < 10 && c.alpha() > 240;
        }
        QCOMPARE(colored, 80);
        QVERIFY(markers.save(screenshots.filePath("top-progress-queue-markers.png")));
        window.player()->setQueue({});
        strip->hide();
    }
    void compactControlsAndUrlCollection() {
        QTemporaryDir dir(QDir::current().filePath("compact-ui-XXXXXX"));
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        settings.setValue("lyrics/autoFetch", false);
        settings.setValue("lyrics/source", "local");
        settings.setValue("windows/showFloat", false);
        settings.setValue("windows/showLyrics", false);
        QList<qint64> ids;
        const QStringList names{"Alpha", "Bravo", "Zulu"};
        for (int i = 0; i < 3; ++i) {
            QFile file(dir.filePath(QString::number(i) + ".flac"));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(QByteArray(10 + 10 * i, 'a'));
            file.close();
            ids << library.addTrack(
                {{"title", names[i]}, {"path", file.fileName()}, {"duration", (3 - i) * 10000}});
        }
        const auto a = library.createPlaylist("晨间漫步"), b = library.createPlaylist("夜航电台");
        MainWindow window(&library, &settings);
        window.show();
        window.navigate(1);
        auto tracks = window.findChild<TrackView *>("availableTracks");
        QCOMPARE(tracks->topLevelItem(0)->text(0), QString("Alpha"));
        auto sort = window.findChild<QPushButton *>("librarySort");
        sort->click();
        QCOMPARE(sort->text(), QString("time"));
        QCOMPARE(tracks->topLevelItem(0)->text(0), QString("Zulu"));
        window.findChild<QPushButton *>("librarySortDirection")->click();
        QCOMPARE(tracks->topLevelItem(0)->text(0), QString("Alpha"));
        sort->click();
        QCOMPARE(sort->text(), QString("size"));
        QCOMPARE(tracks->topLevelItem(0)->text(0), QString("Zulu"));
        auto filter = window.findChild<QLineEdit *>("libraryFilter");
        filter->setText("Bravo");
        window.findChild<QPushButton *>("confirmLibraryFilter")->click();
        QCOMPARE(tracks->topLevelItemCount(), 1);
        window.findChild<QPushButton *>("cancelLibraryFilter")->click();
        QCOMPARE(tracks->topLevelItemCount(), 3);
        auto playlists = window.findChild<PlaylistView *>("playlistList");
        QCOMPARE(playlists->viewMode(), QListView::IconMode);
        QTest::mouseClick(playlists->viewport(), Qt::LeftButton, Qt::NoModifier,
                          playlists->visualItemRect(playlists->item(0)).center());
        QVERIFY(window.findChild<QPushButton *>("playlistBack")->isVisible());
        tracks->topLevelItem(0)->setSelected(true);
        tracks->setFocus();
        QTest::keyClick(tracks, Qt::Key_A, Qt::ControlModifier);
        QCOMPARE(library.tracks(a).size(), 1);
        auto members = window.findChild<TrackView *>("playlistTracks");
        members->topLevelItem(0)->setSelected(true);
        members->setFocus();
        QTest::keyClick(members, Qt::Key_D, Qt::ControlModifier);
        QCOMPARE(library.tracks(a).size(), 0);
        QTest::keyClick(members, Qt::Key_Escape);
        QVERIFY(playlists->isVisible());
        window.navigate(3);
        auto lyricSource = window.findChild<QComboBox *>("workbenchLyricSource");
        QCOMPARE(lyricSource->count(), 3);
        QCOMPARE(lyricSource->itemText(0), QString("线上匹配"));
        QCOMPARE(lyricSource->itemText(1), QString("本机识别"));
        QCOMPARE(lyricSource->itemText(2), QString("导入 LRC"));
        auto localMethod = window.findChild<QComboBox *>("localLyricMethod");
        QVERIFY(window.findChild<LyricQueue *>("lyricTaskQueue"));
        QVERIFY(window.findChild<QFrame *>("lyricSectionDivider"));
        auto options = window.findChild<QWidget *>("lyricMatchingOptions");
        auto timeline = window.findChild<QListWidget *>("timedLyricPreview");
        QCoreApplication::processEvents();
        const auto timelineTop = timeline->mapTo(window.centralWidget(), QPoint()).y();
        const auto optionsSize = options->size();
        QVERIFY(timeline->height() >= 220);
        lyricSource->setCurrentIndex(1);
        QCoreApplication::processEvents();
        QCOMPARE(options->size(), optionsSize);
        QCOMPARE(timeline->mapTo(window.centralWidget(), QPoint()).y(), timelineTop);
        QVERIFY(window.findChild<QPushButton *>("localLyricOption0")->isVisible());
        QCOMPARE(localMethod->currentData().toInt(), 1);
        QCOMPARE(window.findChild<QPushButton *>("matchLyrics")->text(), QString("识别字幕"));
        if (alignmentFeaturesEnabled()) {
            localMethod->setCurrentIndex(1);
            QCOMPARE(window.findChild<QPushButton *>("matchLyrics")->text(), QString("导入歌词"));
        }
        lyricSource->setCurrentIndex(2);
        QCoreApplication::processEvents();
        QCOMPARE(options->size(), optionsSize);
        QCOMPARE(timeline->mapTo(window.centralWidget(), QPoint()).y(), timelineTop);
        QVERIFY(!window.findChild<QPushButton *>("localLyricOption0")->isVisible());
        QCOMPARE(window.findChild<QPushButton *>("matchLyrics")->text(), QString("导入 LRC"));
        window.findChild<QPushButton *>("lyricCategory1")->click();
        QVERIFY(window.findChild<QPushButton *>("lyricCategory1")->isChecked());
        QVERIFY(!window.findChild<QPushButton *>("lyricCategory2")->isChecked());
        if (alignmentFeaturesEnabled()) {
        window.findChild<QPushButton *>("localLyricOption1")->click();
        QCOMPARE(localMethod->currentData().toInt(), 0);
        QVERIFY(window.findChild<QPushButton *>("localLyricOption1")->isChecked());
        QVERIFY(!window.findChild<QPushButton *>("localLyricOption0")->isChecked());
        }
        window.findChild<QPushButton *>("lyricCategory2")->click();
        const QVariantMap result{{"bvid", "BV1bK411W797"},
                                 {"title", "待收录歌曲"},
                                 {"duration", 98000},
                                 {"part", 2},
                                 {"url", "https://www.bilibili.com/video/BV1bK411W797?p=2"}};
        window.showSearchResults({result}, 1, false, true);
        auto results = window.findChild<QTreeWidget *>("searchResults");
        QTest::mouseClick(results->viewport(), Qt::LeftButton, Qt::NoModifier,
                          results->visualItemRect(results->topLevelItem(0)).center());
        QCOMPARE(library.tracks().size(), 3);
        QCOMPARE(library.stagedTracks().size(), 0);
        auto selector = window.findChild<QComboBox *>("lyricTrackSelector");
        QCOMPARE(selector->currentData().toLongLong(), 0);
        QVERIFY(QFile::copy(dir.filePath("0.flac"), dir.filePath("download.flac")));
        const auto draft =
            library.addTrack({{"title", "下载歌曲"}, {"path", dir.filePath("download.flac")}, {"staged", 1}});
        admitTrack(window, draft);
        QCOMPARE(selector->currentData().toLongLong(), draft);
        window.findChild<QPushButton *>("trialLyrics")->click();
        QCOMPARE(window.player()->currentId(), draft);
        window.findChild<QPushButton *>("commitWithoutLyrics")->click();
        QCOMPARE(library.tracks().size(), 4);
        QVERIFY(library.stagedTracks().isEmpty());
        window.findChild<QPushButton *>("collectSearchUrl")->click();
        QCOMPARE(library.tracks().size(), 5);
        QCOMPARE(selector->currentData().toLongLong(), draft);
        QVERIFY(!window.findChild<QPushButton *>("cancelOnlineSearch"));
        window.lyrics()->activityChanged(true);
        QVERIFY(window.findChild<BusySpinner *>("lyricMatchSpinner")->active());
        window.lyrics()->activityChanged(false);
        QVERIFY(!window.findChild<BusySpinner *>("lyricMatchSpinner")->active());
        QDir screenshots(QDir(resourceRoot()).filePath("docs/screenshots"));
        screenshots.mkpath(".");
        QTest::qWait(100);
        QVERIFY(window.grab().save(screenshots.filePath("compact-search-url.png")));
        window.navigate(1);
        bool marked = false;
        for (int i = 0; i < tracks->topLevelItemCount(); ++i)
            marked |= tracks->topLevelItem(i)->text(0).contains("[URL]");
        QVERIFY(marked);
        QVERIFY(window.grab().save(screenshots.filePath("compact-library-cards.png")));
        window.navigate(2);
        window.player()->setMode(Player::Sequential);
        window.player()->setQueue(ids.mid(0, 2), ids[0], false);
        auto enqueue = window.findChild<QPushButton *>(QString("enqueueTrack_%1").arg(ids[2]));
        QVERIFY(enqueue);
        enqueue->click();
        QVERIFY(window.player()->queue().contains(ids[2]));
        const auto entry = window.player()->pendingEntries().last().key;
        auto remove = window.findChild<QPushButton *>(QString("dequeueEntry_%1").arg(entry));
        QVERIFY(remove);
        remove->click();
        QVERIFY(!window.player()->queue().contains(ids[2]));
        QCOMPARE(library.tracks().size(), 5);
        window.navigate(4);
        auto settingsFloat = window.findChild<QCheckBox *>("settingsShowFloat");
        settingsFloat->setChecked(true);
        QVERIFY(window.findChild<QPushButton *>("sidebarFloatBar")->isChecked());
        settingsFloat->setChecked(false);
        QCOMPARE(library.tracks(b).size(), 0);
        const auto pending = library.addTrack(
            {{"title", "等待歌词入库"}, {"path", dir.filePath("pending.flac")}, {"staged", 1}});
        admitTrack(window, pending);
        window.findChild<QPushButton *>("commitWithLyrics")->click();
        QCOMPARE(library.tracks().size(), 5);
        QCOMPARE(library.stagedTracks().size(), 1);
        library.setWorkbenchLyrics(pending, "[00:00.00]已配置歌词", "测试");
        QCOMPARE(library.tracks().size(), 6);
        QVERIFY(library.stagedTracks().isEmpty());
        window.player()->setQueue({});
    }
    void playbackBrowsingAndQueue() {
        QTemporaryDir dir(QDir::current().filePath("playback-test-XXXXXX"));
        QVERIFY(dir.isValid());
        const auto audio = dir.filePath("tone.flac");
        QProcess ffmpeg;
        ffmpeg.start(QDir(resourceRoot()).filePath("tools/ffmpeg.exe"),
                     {"-v", "error", "-n", "-f", "lavfi", "-i", "sine=frequency=440:duration=30", audio});
        QVERIFY(ffmpeg.waitForFinished(15000));
        QCOMPARE(ffmpeg.exitCode(), 0);
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        settings.setValue("lyrics/autoFetch", false);
        settings.setValue("windows/showFloat", false);
        settings.setValue("windows/showLyrics", false);
        settings.setValue("ui/page", 2); // Former settings index must migrate to 4.
        QList<qint64> ids;
        const QStringList names{"听见微光", "晚风来信", "雨后漫步", "橘色海岸", "月下归途", "旧时光电台"};
        for (int i = 0; i < names.size(); ++i) {
            const auto path = dir.filePath(QString("%1.flac").arg(i));
            QVERIFY(QFile::copy(audio, path));
            ids << library.addTrack({{"title", names[i]}, {"path", path}, {"duration", 30000}});
        }
        const auto a = library.createPlaylist("晨间漫步"), b = library.createPlaylist("夜航电台");
        library.addToPlaylist(a, {ids[0], ids[1], ids[2]});
        library.addToPlaylist(b, {ids[3], ids[4], ids[5]});
        MainWindow window(&library, &settings);
        window.show();
        auto pages = window.findChild<QStackedWidget *>("modulePages");
        QCOMPARE(pages->count(), 5);
        QCOMPARE(pages->currentIndex(), 4);
        window.findChild<QPushButton *>("navLibrary")->click();
        QCOMPARE(pages->currentIndex(), 1);
        auto managed = window.findChild<TrackView *>("availableTracks");
        QTest::mouseDClick(managed->viewport(), Qt::LeftButton, Qt::NoModifier,
                           managed->visualItemRect(managed->topLevelItem(0)).center());
        QCOMPARE(window.player()->currentId(), 0);
        window.findChild<QPushButton *>("navPlayback")->click();
        QCOMPARE(pages->currentIndex(), 2);
        auto songs = window.findChild<QPushButton *>("playbackSongsTab");
        auto playlists = window.findChild<QPushButton *>("playbackPlaylistsTab");
        auto tracks = window.findChild<TrackView *>("playbackTracks");
        auto cards = window.findChild<PlaylistView *>("playbackPlaylistCards");
        auto queue = window.findChild<PlaybackQueueView *>("playbackQueue");
        QVERIFY(songs->isChecked() && !playlists->isChecked());
        QCOMPARE(tracks->topLevelItemCount(), 6);
        QVERIFY(tracks->isColumnHidden(1) && tracks->isColumnHidden(2));
        QCOMPARE(tracks->headerItem()->text(0), QString("歌曲名称"));
        QCOMPARE(tracks->headerItem()->text(3), QString("时长"));
        window.player()->setMode(Player::Sequential);
        window.player()->setQueue(ids.mid(0, 4), ids[0], false);
        QCOMPARE(queue->topLevelItemCount(), 4);
        QCOMPARE(queue->topLevelItem(0)->data(0, Qt::UserRole).toLongLong(), ids[3]);
        QCOMPARE(queue->topLevelItem(2)->data(0, Qt::UserRole).toLongLong(), ids[1]);
        QCOMPARE(queue->topLevelItem(3)->data(0, Qt::UserRole).toLongLong(), ids[0]);
        QCOMPARE(queue->columnCount(), 1);
        QDir screenshots(QDir(resourceRoot()).filePath("docs/screenshots"));
        screenshots.mkpath(".");
        QTest::qWait(150);
        QVERIFY(window.grab().save(screenshots.filePath("player-songs-queue.png")));
        playlists->click();
        QVERIFY(!songs->isChecked() && playlists->isChecked());
        QCOMPARE(cards->count(), 2);
        QVERIFY(cards->isVisible());
        QTest::qWait(100);
        QVERIFY(window.grab().save(screenshots.filePath("player-playlist-cards.png")));
        QTest::mouseClick(cards->viewport(), Qt::LeftButton, Qt::NoModifier,
                          cards->visualItemRect(cards->item(0)).center());
        QVERIFY(tracks->isVisible());
        QCOMPARE(tracks->topLevelItemCount(), 3);
        QCOMPARE(tracks->topLevelItem(0)->text(0), names[0]);
        QVERIFY(window.grab().save(screenshots.filePath("player-playlist-tracks.png")));
        window.findChild<QPushButton *>("playbackPlaylistBack")->click();
        QVERIFY(cards->isVisible());
        songs->click();
        QCOMPARE(tracks->topLevelItemCount(), 6);
        QVERIFY(!playlists->isChecked());
        window.player()->setMode(Player::Shuffle);
        const auto announced = window.player()->upcoming().first();
        QCOMPARE(queue->topLevelItem(queue->topLevelItemCount() - 2)->data(0, Qt::UserRole).toLongLong(),
                 announced);
        window.player()->next();
        QCOMPARE(window.player()->currentId(), announced);
        window.player()->media()->pause();
        const auto membership = library.tracks(a).size();
        window.player()->removeQueued(window.player()->upcoming().first());
        QCOMPARE(library.tracks().size(), 6);
        QCOMPARE(library.tracks(a).size(), membership);
        window.findChild<QPushButton *>("clearPlaybackQueue")->click();
        QCOMPARE(window.player()->currentId(), 0);
        QCOMPARE(queue->topLevelItemCount(), 0);
        QMimeData playlistMime;
        playlistMime.setData(PlaylistMime, QByteArray::number(b));
        QDragEnterEvent enter(QPoint(30, 30), Qt::CopyAction, &playlistMime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(queue->viewport(), &enter);
        QDropEvent drop(QPointF(30, 30), Qt::CopyAction, &playlistMime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(queue->viewport(), &drop);
        QCOMPARE(window.player()->queue().size(), 3);
        QCOMPARE(library.tracks(b).size(), 3);
        window.player()->media()->pause();
        window.navigate(1);
        QCOMPARE(managed->topLevelItemCount(), 6);
        window.navigate(4);
        QVERIFY(window.findChild<QPushButton *>("saveSettings")->isVisibleTo(pages->currentWidget()));
    }
    void searchData() {
        QJsonObject search{
            {"numPages", 3},
            {"pagesize", 20},
            {"result", QJsonArray{QJsonObject{{"bvid", "BV13x41117TL"},
                                              {"title", "<em class=\"keyword\">歌曲</em> &amp; 标题"},
                                              {"duration", "1:02:03"},
                                              {"pic", "//cdn.example/cover.jpg"},
                                              {"author", "演唱者"}}}}};
        auto page = SearchService::parseSearch(search, 2);
        QCOMPARE(page.page, 2);
        QVERIFY(page.hasNext);
        QCOMPARE(page.items.size(), 1);
        QCOMPARE(page.items[0].value("title").toString(), QString("歌曲 & 标题"));
        QCOMPARE(page.items[0].value("duration").toLongLong(), 3723000);
        QVERIFY(page.items[0].value("thumbnail").toString().startsWith("https://"));
        QVERIFY(!SearchService::parseSearch(search, 3).hasNext);
        QJsonArray parts;
        for (int p = 1; p <= 3; ++p)
            parts << QJsonObject{{"page", p}, {"part", QString("歌曲 %1").arg(p)}, {"duration", p * 60}};
        QJsonObject video{{"bvid", "BV13x41117TL"},
                          {"title", "分 P 专辑"},
                          {"pic", "https://cdn.example/cover.jpg"},
                          {"pages", parts}};
        auto parsed = SearchService::parseVideo(video);
        QVERIFY(parsed.video);
        QCOMPARE(parsed.items.size(), 3);
        QCOMPARE(parsed.items[1].value("url").toString(),
                 QString("https://www.bilibili.com/video/BV13x41117TL?p=2"));
        QCOMPARE(parsed.items[2].value("duration").toLongLong(), 180000);
    }
    void modulesMembershipAndLyrics() {
        QTemporaryDir dir(QDir::current().filePath("ui-test-XXXXXX"));
        QVERIFY(dir.isValid());
        const auto audio = dir.filePath("tone.flac");
        QProcess ffmpeg;
        ffmpeg.start(QDir(resourceRoot()).filePath("tools/ffmpeg.exe"),
                     {"-v", "error", "-n", "-f", "lavfi", "-i",
                      "sine=frequency=440:duration=12:sample_rate=48000", "-c:a", "flac", audio});
        QVERIFY(ffmpeg.waitForFinished(15000));
        QCOMPARE(ffmpeg.exitCode(), 0);
        QVERIFY(QFile::copy(audio, dir.filePath("second.flac")));
        QVERIFY(QFile::copy(audio, dir.filePath("third.flac")));
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        settings.setValue("lyrics/autoFetch", false);
        settings.setValue("lyrics/source", "local");
        settings.setValue("windows/showFloat", false);
        settings.setValue("windows/showLyrics", false);
        auto add = [&](const QString &title, const QString &path) {
            return library.addTrack({{"title", title},
                                     {"artist", "BiliWave"},
                                     {"path", path},
                                     {"duration", 12000},
                                     {"codec", "flac"},
                                     {"sample_rate", 48000}});
        };
        auto one = add("听见微光", audio), two = add("晚风来信", dir.filePath("second.flac")),
             three = add("雨后漫步", dir.filePath("third.flac"));
        auto a = library.createPlaylist("晨间漫步"), b = library.createPlaylist("夜航电台");
        library.addToPlaylist(a, {one, two});
        library.setLyrics(one, "[00:00.00]听见微光，落在窗前\n[00:04.00]让下一句，轻轻唱起\n[00:10.00]",
                          "测试歌词");
        MainWindow window(&library, &settings);
        window.show();
        window.navigate(1);
        QTest::qWait(150);
        auto pages = window.findChild<QStackedWidget *>("modulePages");
        QVERIFY(pages);
        QCOMPARE(pages->currentIndex(), 1);
        auto left = window.findChild<TrackView *>("availableTracks"),
             members = window.findChild<TrackView *>("playlistTracks");
        auto playlists = window.findChild<PlaylistView *>("playlistList");
        QVERIFY(left && members && playlists);
        QCOMPARE(left->topLevelItemCount(), 3);
        QCOMPARE(playlists->count(), 2);
        QMimeData trackMime;
        trackMime.setData(TrackMime, QJsonDocument(QJsonArray{QString::number(one)}).toJson());
        auto target = playlists->item(1);
        auto point = playlists->visualItemRect(target).center();
        QDragEnterEvent enter(point, Qt::CopyAction, &trackMime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(playlists->viewport(), &enter);
        QDropEvent drop(point, Qt::CopyAction, &trackMime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(playlists->viewport(), &drop);
        QCOMPARE(library.tracks(b).size(), 1);
        QCOMPARE(library.tracks(a).size(), 2);
        QCOMPARE(library.tracks().size(), 3);
        QTest::mouseClick(playlists->viewport(), Qt::LeftButton, Qt::NoModifier,
                          playlists->visualItemRect(playlists->item(0)).center());
        QCOMPARE(left->topLevelItemCount(), 1);
        QCOMPARE(left->topLevelItem(0)->data(0, Qt::UserRole).toLongLong(), three);
        QCOMPARE(members->topLevelItemCount(), 2);
        QMimeData thirdMime;
        thirdMime.setData(TrackMime, QJsonDocument(QJsonArray{QString::number(three)}).toJson());
        QDragEnterEvent memberEnter(QPoint(30, 30), Qt::CopyAction, &thirdMime, Qt::LeftButton,
                                    Qt::NoModifier);
        QApplication::sendEvent(members->viewport(), &memberEnter);
        QDropEvent memberDrop(QPointF(30, 30), Qt::CopyAction, &thirdMime, Qt::LeftButton, Qt::NoModifier);
        QApplication::sendEvent(members->viewport(), &memberDrop);
        QCOMPARE(left->topLevelItemCount(), 0);
        QCOMPARE(members->topLevelItemCount(), 3);
        members->topLevelItem(2)->setSelected(true);
        window.findChild<QPushButton *>("removeMembership")->click();
        QCOMPARE(left->topLevelItemCount(), 1);
        QCOMPARE(members->topLevelItemCount(), 2);
        QCOMPARE(library.tracks().size(), 3);
        QDir screenshots(QDir(resourceRoot()).filePath("docs/screenshots"));
        QDir().mkpath(screenshots.absolutePath());
        QVERIFY(window.grab().save(screenshots.filePath("playlist-detail.png")));
        window.findChild<QPushButton *>("playlistBack")->click();
        QCOMPARE(left->topLevelItemCount(), 3);
        QVERIFY(playlists->isVisible());
        QVERIFY(window.grab().save(screenshots.filePath("playback.png")));
        QMimeData playlistMime;
        playlistMime.setData(PlaylistMime, QByteArray::number(a));
        window.player()->playPlaylist(a);
        QCOMPARE(window.player()->queue().size(), 2);
        QCOMPARE(window.player()->currentId(), one);
        QTRY_VERIFY_WITH_TIMEOUT(window.player()->media()->duration() > 11000, 15000);
        window.player()->media()->pause();
        window.player()->media()->setPosition(1000);
        window.navigate(0);
        auto selector = window.findChild<QComboBox *>("lyricTrackSelector");
        auto stagedTrack = library.track(one);
        const auto workbenchPath = dir.filePath("workbench.flac");
        QVERIFY(QFile::copy(stagedTrack.value("path").toString(), workbenchPath));
        stagedTrack["path"] = workbenchPath;
        stagedTrack["staged"] = 1;
        const auto workbenchSong = library.addTrack(stagedTrack);
        library.setLyrics(workbenchSong, library.track(one).value("lrc").toString(), "测试");
        admitTrack(window, workbenchSong);
        QCOMPARE(selector->currentData().toLongLong(), workbenchSong);
        library.commitTrack(workbenchSong);
        auto preview = window.findChild<QListWidget *>("timedLyricPreview");
        QCOMPARE(preview->count(), 3);
        auto offset = window.findChild<QSpinBox *>("lyricTimeOffset");
        offset->setValue(500);
        QCOMPARE(library.workbenchTrack(workbenchSong).value("lyric_offset").toInt(), 500);
        QList<QVariantMap> candidates{
            {{"trackName", "听见微光"},
             {"artistName", "BiliWave"},
             {"difference", 0.2},
             {"syncedLyrics", "[00:00.00]听见微光，落在窗前\n[00:04.00]让下一句，轻轻唱起\n[00:10.00]"}}};
        window.lyrics()->candidates(workbenchSong, candidates);
        auto candidateList = window.findChild<QTreeWidget *>("lyricCandidates");
        QCOMPARE(candidateList->topLevelItemCount(), 1);
        window.findChild<QPushButton *>("useLyricCandidate")->click();
        QCOMPARE(library.workbenchTrack(workbenchSong).value("lyric_source").toString(), QString("LRCLIB"));
        QCOMPARE(window.player()->currentId(), workbenchSong);
        QTRY_VERIFY_WITH_TIMEOUT(window.player()->media()->duration() > 11000, 15000);
        window.player()->media()->pause();
        window.player()->media()->setPosition(4500);
        QTRY_COMPARE(preview->currentRow(), 1);
        QList<QVariantMap> results;
        for (int p = 1; p <= 3; ++p)
            results << QVariantMap{
                {"bvid", "BV13x41117TL"},
                {"title", QString("测试专辑 / 分 P %1").arg(p)},
                {"url", QString("https://www.bilibili.com/video/BV13x41117TL?p=%1").arg(p)},
                {"duration", p * 60000},
                {"part", p}};
        window.showSearchResults(results, 1, false, true);
        QCOMPARE(window.findChild<QTreeWidget *>("searchResults")->topLevelItemCount(), 3);
        QVERIFY(!window.findChild<QPushButton *>("nextSearchPage")->isEnabled());
        window.showSearchResults(results, 2, true, false);
        QVERIFY(window.findChild<QPushButton *>("nextSearchPage")->isEnabled());
        QVERIFY(window.findChild<QPushButton *>("previousSearchPage")->isEnabled());
        QTest::qWait(150);
        QVERIFY(window.grab().save(screenshots.filePath("search.png")));
        window.navigate(4);
        auto settingOpacity = window.findChild<QSlider *>("settingsLyricOpacity");
        settingOpacity->setValue(60);
        window.findChild<QSlider *>("settingsLyricSecondaryOpacity")->setValue(30);
        window.findChild<QPushButton *>("saveSettings")->click();
        QCOMPARE(settings.value("lyrics/opacity").toInt(), 60);
        QVERIFY(window.grab().save(screenshots.filePath("settings.png")));
        window.player()->media()->setPosition(1000);
        QTRY_COMPARE(preview->currentRow(), 0);
        auto image = window.overlay()->grab().toImage().convertToFormat(QImage::Format_ARGB32);
        int top = 0, bottom = 0;
        for (int y = 0; y < image.height(); ++y)
            for (int x = 0; x < image.width(); ++x) {
                auto alpha = qAlpha(image.pixel(x, y));
                if (alpha > 50) {
                    const auto pixel = QColor::fromRgba(image.pixel(x, y));
                    QVERIFY(pixel.green() > pixel.red());
                    QVERIFY(pixel.green() > 170);
                }
                if (y < 90 * image.devicePixelRatio())
                    top = qMax(top, alpha);
                else
                    bottom = qMax(bottom, alpha);
            }
        QVERIFY2(qAbs(top - 2 * bottom) < 5, qPrintable(QString("Alpha: %1 / %2").arg(top).arg(bottom)));
        QVERIFY(top > 100 && bottom > 40);
        window.player()->media()->setPosition(1000);
        QTest::qWait(100);
        QVERIFY(window.floatBar()->grab().save(screenshots.filePath("float-bar.png")));
        QVERIFY(window.overlay()->grab().save(screenshots.filePath("lyrics.png")));
        window.navigate(1);
        QVERIFY(window.grab().save(screenshots.filePath("library.png")));
        window.player()->media()->stop();
    }
};
QTEST_MAIN(RedesignTests)
#include "redesign_tests.moc"
