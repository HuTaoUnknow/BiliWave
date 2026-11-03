#include "lyricworkflow.h"
#include "theme.h"
#include "window.h"
#include <QBoxLayout>
#include <QScreen>
#include <QApplication>
#include <QButtonGroup>
#include <QMenu>
#include <QMessageBox>
#include <QShortcut>
#include <QSignalBlocker>
#include <algorithm>
#include <memory>

#include "uihelpers.h"
#include <QAction>
using namespace Ui;

MainWindow::MainWindow(Library *l, QSettings *s, QWidget *p)
    : QMainWindow(p), m_library(l), m_settings(s), m_player(l), m_ducking(&m_player, s, this), m_import(l, s),
      m_lyrics(l, s), m_exports(l, s), m_lookup(s), m_stream(s) {
    m_player.setOutputDeviceId(QByteArray::fromBase64(s->value("audio/outputDevice").toByteArray()));
    if (!lyricFeaturesEnabled()) {
        s->setValue("lyrics/autoFetch", false);
        s->setValue("lyrics/matchAfterDownload", false);
    }
    if (!recognitionFeaturesEnabled()) {
        s->setValue("lyrics/source", "online");
        s->setValue("lyrics/autoFetch", false);
        s->setValue("lyrics/matchAfterDownload", false);
    }
    if (s->value("lyrics/source", "auto").toString() == "auto")
        s->setValue("lyrics/source", "online");
    setWindowTitle(recognitionFeaturesEnabled() ? "BiliWave 加装版 · 音乐工作台" : "BiliWave 简装版 · 音乐工作台");
    setWindowIcon(QIcon(":/biliwave/icon.png"));
    setMinimumSize(1040, 650);
    const auto available = QGuiApplication::primaryScreen()->availableGeometry();
    resize(qMin(1240, available.width() - 30), qMin(850, available.height() - 45));
    UiTheme::apply(this, s->value("ui/theme", "dark").toString());
    auto central = new QWidget;
    setCentralWidget(central);
    auto shell = new QHBoxLayout(central);
    shell->setContentsMargins(0, 0, 0, 0);
    shell->setSpacing(0);
    auto navigation = new QWidget;
    navigation->setObjectName("navigation");
    navigation->setFixedWidth(170);
    auto rail = new QVBoxLayout(navigation);
    rail->setContentsMargins(12, 26, 12, 18);
    rail->setSpacing(8);
    rail->addWidget(label("BiliWave", "brand"));
    rail->addWidget(label("音乐工作台", "muted"));
    rail->addSpacing(25);
    m_navSearch = action("搜索", "navSearch");
    m_navLibrary = action("乐库", "navLibrary");
    m_navPlayback = action("播放", "navPlayback");
    m_navWorkbench = action("工作台", "navWorkbench");
    m_navSettings = action("设置", "navSettings");
    const QList<QPushButton *> navigationButtons{m_navSearch, m_navLibrary, m_navPlayback, m_navWorkbench,
                                                 m_navSettings};
    for (int i = 0; i < navigationButtons.size(); ++i) {
        navigationButtons[i]->setProperty("nav", true);
        navigationButtons[i]->setIcon(navigationIcon(i, UiTheme::forWidget(this)));
        navigationButtons[i]->setIconSize(QSize(20, 20));
    }
    auto group = new QButtonGroup(this);
    for (auto b : navigationButtons) {
        b->setCheckable(true);
        group->addButton(b);
    }
    rail->addWidget(m_navSearch);
    rail->addWidget(m_navLibrary);
    rail->addWidget(m_navPlayback);
    if (lyricFeaturesEnabled())
        rail->addWidget(m_navWorkbench);
    else {
        m_navWorkbench->setParent(navigation);
        m_navWorkbench->hide();
    }
    rail->addStretch();
    m_importStatus = label("", "libraryImportStatus");
    m_importStatus->setWordWrap(true);
    rail->addWidget(m_importStatus);
    m_floatButton = action("顶部进度条", "sidebarFloatBar");
    m_floatButton->setCheckable(true);
    m_lyricButton = action("桌面歌词", "sidebarLyrics");
    m_lyricButton->setCheckable(true);
    rail->addWidget(m_floatButton);
    if (lyricFeaturesEnabled())
        rail->addWidget(m_lyricButton);
    else {
        m_lyricButton->setParent(navigation);
        m_lyricButton->hide();
    }
    rail->addWidget(m_navSettings);
    shell->addWidget(navigation);
    auto main = new QWidget;
    auto layout = new QVBoxLayout(main);
    layout->setContentsMargins(22, 20, 22, 12);
    layout->setSpacing(14);
    auto header = new QHBoxLayout;
    auto titles = new QVBoxLayout;
    m_pageTitle = label("搜索", "pageTitle");
    m_pageSubtitle = label("搜索音乐视频，下载原始音轨，校准时间轴歌词。", "muted");
    titles->addWidget(m_pageTitle);
    titles->addWidget(m_pageSubtitle);
    header->addLayout(titles);
    m_headerTools = new QStackedWidget;
    m_headerTools->setObjectName("headerTools");
    header->addWidget(m_headerTools, 1);
    layout->addLayout(header);
    m_pages = new QStackedWidget;
    m_pages->setObjectName("modulePages");
    m_pages->addWidget(createSearchPage());
    m_pages->addWidget(createLibraryPage());
    m_pages->addWidget(createPlaybackPage());
    m_pages->addWidget(createWorkbenchPage());
    m_pages->addWidget(createSettingsPage());
    layout->addWidget(m_pages, 1);
    auto footer = new QWidget;
    footer->setObjectName("footer");
    auto bottom = new QVBoxLayout(footer);
    bottom->setContentsMargins(14, 12, 14, 10);
    bottom->setSpacing(8);
    auto now = new QHBoxLayout;
    m_current = label("选择一首歌曲，开始聆听", "songTitle");
    m_currentLyric = label("", "muted");
    m_nextLyric = label("", "muted");
    m_nextLyric->hide();
    now->addWidget(m_current, 1);
    now->addWidget(m_currentLyric, 1);
    if (!lyricFeaturesEnabled())
        m_currentLyric->hide();
    layout->addWidget(footer);
    bottom->addLayout(now);
    auto controls = new QHBoxLayout;
    auto previous = action({}, "previousTrack"), next = action({}, "nextTrack");
    previous->setToolTip("上一首");
    previous->setAccessibleName("上一首");
    next->setToolTip("下一首");
    next->setAccessibleName("下一首");
    for (auto button : {previous, next}) {
        button->setProperty("transport", true);
        button->setFixedSize(32, 36);
        button->setIconSize(QSize(22, 22));
    }
    m_toggle = action({}, "transportToggle");
    m_toggle->setProperty("primary", true);
    m_toggle->setToolTip("播放");
    m_toggle->setAccessibleName("播放");
    m_toggle->setFixedSize(44, 44);
    m_toggle->setIconSize(QSize(24, 24));
    m_mode = new QComboBox;
    m_mode->setObjectName("playbackMode");
    m_mode->addItem("顺序播放", Player::Sequential);
    m_mode->addItem("随机播放", Player::Shuffle);
    m_mode->addItem("单曲循环", Player::SingleLoop);
    m_mode->setCurrentIndex(m_mode->findData(Player::modeFromStored(s->value("player/mode", 0).toInt())));
    m_player.setMode(Player::Mode(m_mode->currentData().toInt()));
    controls->addWidget(previous);
    controls->addWidget(m_toggle);
    controls->addWidget(next);
    controls->addWidget(m_mode);
    m_progress = new QSlider(Qt::Horizontal);
    m_progress->setRange(0, 10000);
    controls->addWidget(m_progress, 1);
    m_time = label("00:00 / 00:00", "muted");
    controls->addWidget(m_time);
    controls->addWidget(label("音量", "muted"));
    auto volume = new QSlider(Qt::Horizontal);
    volume->setObjectName("playerVolume");
    volume->setRange(0, 100);
    volume->setMaximumWidth(75);
    volume->setValue(s->value("player/volume", 75).toInt());
    m_player.setVolume(volume->value() / 100.);
    controls->addWidget(volume);
    bottom->addLayout(controls);
    m_status = label("就绪 · 原轨保留，直接封装", "muted");
    bottom->addWidget(m_status);
    shell->addWidget(main, 1);
    m_float = new FloatBar(&m_player, s);
    m_overlay = new LyricOverlay(s);
    connect(&m_exports, &AudioExportService::message, this, &MainWindow::notify);
    connect(&m_exports, &AudioExportService::finished, this,
            [this](bool ok, const QString &audio, const QString &, const QString &error) {
                notify(ok ? "已导出歌曲及歌词：" + audio : error);
            });
    connect(m_navSearch, &QPushButton::clicked, this, [this] { navigate(0); });
    connect(m_navLibrary, &QPushButton::clicked, this, [this] { navigate(1); });
    connect(m_navPlayback, &QPushButton::clicked, this, [this] { navigate(2); });
    connect(m_navWorkbench, &QPushButton::clicked, this, [this] { navigate(3); });
    connect(m_navSettings, &QPushButton::clicked, this, [this] { navigate(4); });
    connect(&m_lookup, &SearchService::results, this, &MainWindow::showSearchResults);
    connect(&m_lookup, &SearchService::message, this, [this](const QString &text) {
        m_resultStatus->setText(text);
        notify(text);
    });
    connect(&m_lookup, &SearchService::busyChanged, this, [this](bool busy) {
        m_searchButton->setText(busy ? "搜索中…" : "搜索");
        m_previousPage->setEnabled(!busy && !m_resultIsVideo && m_resultPage > 1);
        m_nextPage->setEnabled(!busy && !m_resultIsVideo && m_resultHasNext);
    });
    connect(&m_import, &ImportService::status, this,
            [this](const QString &id, const QString &title, const QString &state) {
                if ((!lyricFeaturesEnabled() && state.startsWith("完成")) || state.startsWith("取消") ||
                    state == "已取消") {
                    delete m_jobItems.take(id);
                    return;
                }
                updateDownloadTask(id, title, state);
            });
    connect(&m_import, &ImportService::completed, this, [this](const QString &taskId, qint64 trackId) {
        const auto track = m_library->track(trackId);
        if (lyricFeaturesEnabled() && !Lrc::parse(track.value("raw_lrc").toString()).isEmpty())
            m_lyricQueue->markApplied(trackId, track.value("raw_lrc").toString(),
                                      track.value("lyric_source").toString());
        completeDownload(taskId, trackId);
    });
    connect(&m_import, &ImportService::localImportNotice, this, [this](const QString &message) {
        notify(message);
        auto notice = new QMessageBox(QMessageBox::Information, "导入音频", message, QMessageBox::Ok, this);
        notice->setObjectName("localImportNotice");
        notice->setAttribute(Qt::WA_DeleteOnClose);
        notice->open();
    });
    if (lyricFeaturesEnabled()) {
        connect(&m_lyrics, &LyricService::message, this, [this](const QString &text) {
            m_lyricStatus->setText(text);
            notify(text);
        });
        connect(&m_lyrics, &LyricService::candidates, this,
                [this](qint64 track, const QList<QVariantMap> &items) {
                    m_lyricQueue->addOnlineResults(track, items);
                    chooseLyrics(track, items);
                });
        connect(&m_lyrics, &LyricService::resultApplied, m_lyricQueue, &LyricQueue::markApplied);
        connect(&m_lyrics, &LyricService::activityChanged, this, [this](bool busy) {
            m_lyricSpinner->setActive(busy || m_lyricQueue->activeFor(m_lyricTrack));
        });
    }
    connect(&m_player, &Player::streamRequested, &m_stream, &StreamService::resolve);
    connect(&m_stream, &StreamService::ready, &m_player, &Player::acceptStream);
    connect(&m_stream, &StreamService::message, this, &MainWindow::notify);
    connect(&m_player, &Player::failure, this, &MainWindow::notify);
    connect(m_toggle, &QPushButton::clicked, &m_player, &Player::toggle);
    connect(previous, &QPushButton::clicked, &m_player, &Player::previous);
    connect(next, &QPushButton::clicked, this, [this] { m_player.next(); });
    connect(m_mode, &QComboBox::currentIndexChanged, this, [this] {
        const auto mode = Player::modeFromStored(m_mode->currentData().toInt());
        m_player.setMode(mode);
        m_settings->setValue("player/mode", int(mode));
    });
    connect(&m_player, &Player::queueChanged, m_mode, [this] {
        QSignalBlocker guard(m_mode);
        m_mode->setCurrentIndex(m_mode->findData(m_player.mode()));
        refreshPlaybackQueue();
    });
    connect(volume, &QSlider::valueChanged, this, [this](int value) {
        m_player.setVolume(value / 100.);
        m_settings->setValue("player/volume", value);
    });
    connect(&m_player, &Player::volumeChanged, volume, [volume](float value) {
        const QSignalBlocker guard(volume);
        volume->setValue(qRound(value * 100));
    });
    connect(m_progress, &QSlider::sliderReleased, this, [this] {
        m_player.media()->setPosition(m_player.media()->duration() * m_progress->value() / 10000);
    });
    connect(m_player.media(), &QMediaPlayer::positionChanged, this, [this] {
        auto media = m_player.media();
        if (!m_progress->isSliderDown())
            m_progress->setValue(media->duration() ? int(media->position() * 10000 / media->duration()) : 0);
        m_time->setText(displayTime(media->position()) + " / " + displayTime(media->duration()));
        updateLyrics();
        syncLyricPreview();
    });
    connect(m_player.media(), &QMediaPlayer::playbackStateChanged, this, [this](auto state) {
        const bool playing = state == QMediaPlayer::PlayingState;
        m_toggle->setToolTip(playing ? "暂停" : "播放");
        m_toggle->setAccessibleName(m_toggle->toolTip());
        m_toggle->setIcon(transportIcon(playing ? 3 : 1, UiTheme::forWidget(this).color(QPalette::HighlightedText), devicePixelRatioF()));
        updateLyrics();
    });
    connect(m_player.media(), &QMediaPlayer::durationChanged, this, [this](qint64 duration) {
        m_time->setText(displayTime(m_player.media()->position()) + " / " + displayTime(duration));
    });
    connect(&m_player, &Player::currentChanged, this, [this] {
        m_lyricFingerprint.clear();
        updateCurrent();
        if (lyricFeaturesEnabled() && m_player.currentId() &&
            m_previewTrack != m_player.currentId() &&
            !m_library->track(m_player.currentId()).value("staged").toBool() &&
            m_settings->value("lyrics/autoFetch", true).toBool())
            m_lyrics.fetch(m_player.currentId());
    });
    connect(l, &Library::changed, this, [this] {
        for (auto it = m_jobItems.begin(); it != m_jobItems.end();) {
            const auto id = it.value()->data(Qt::UserRole).toLongLong();
            const auto track = m_library->track(id);
            if (id && (track.isEmpty() || !track.value("staged").toBool())) {
                delete it.value();
                it = m_jobItems.erase(it);
            } else
                ++it;
        }
        refresh();
        refreshLyricLibrary();
        refreshLyricWorkbench();
        updateCurrent();
    });
    connect(l, &Library::workbenchChanged, this, [this](qint64 id) {
        if (m_pendingCommit == id &&
            !Lrc::parse(m_library->workbenchTrack(id).value("lrc").toString()).isEmpty()) {
            m_pendingCommit = 0;
            if (m_library->saveWorkbench(id))
                finishWorkbenchImport(id, "已导入乐库");
        }
        if (id == m_lyricTrack)
            refreshLyricWorkbench();
        if (id == m_previewTrack) {
            m_lyricFingerprint.clear();
            updateCurrent();
        }
    });
    connect(m_floatButton, &QPushButton::toggled, this, [this](bool on) {
        m_float->setVisible(on);
        m_settings->setValue("windows/showFloat", on);
    });
    connect(m_lyricButton, &QPushButton::toggled, this, [this](bool on) {
        m_overlay->setVisible(on);
        m_settings->setValue("windows/showLyrics", on);
    });
    m_tray = new QSystemTrayIcon(windowIcon(), this);
    m_tray->setObjectName("biliwaveTray");
    auto trayMenu = new QMenu(this);
    trayMenu->setObjectName("trayMenu");
    trayMenu->addAction("显示主窗口", this, &MainWindow::showMainWindow)->setObjectName("trayShowWindow");
    trayMenu->addAction("播放 / 暂停", &m_player, &Player::toggle)->setObjectName("trayTogglePlayback");
    trayMenu
        ->addAction("设置", this,
                    [this] {
                        showMainWindow();
                        settingsDialog();
                    })
        ->setObjectName("trayOpenSettings");
    trayMenu->addSeparator();
    trayMenu->addAction("退出", this, &MainWindow::exitApplication)->setObjectName("trayExit");
    m_tray->setContextMenu(trayMenu);
    m_tray->setToolTip("BiliWave");
    connect(m_tray, &QSystemTrayIcon::activated, this, [this](auto reason) {
        if (reason == QSystemTrayIcon::Trigger || reason == QSystemTrayIcon::DoubleClick)
            showMainWindow();
    });
    m_tray->show();
    auto space = new QShortcut(QKeySequence(Qt::Key_Space), this);
    connect(space, &QShortcut::activated, this, [this] {
        auto focus = QApplication::focusWidget();
        if (!qobject_cast<QLineEdit *>(focus) && !qobject_cast<QPlainTextEdit *>(focus))
            m_player.toggle();
    });
    refresh();
    refreshLyricLibrary();
    int savedPage = s->value("ui/page", 0).toInt();
    const auto navigationVersion = s->value("ui/navigationVersion", 1).toInt();
    if ((navigationVersion < 2 && savedPage == 2) || (navigationVersion < 3 && savedPage == 3))
        savedPage = 4;
    s->setValue("ui/navigationVersion", 3);
    navigate(qBound(0, savedPage, 4));
    applyTheme(s->value("ui/theme", "dark").toString());
    restoreGeometry(s->value("windows/main").toByteArray());
    m_floatButton->setChecked(s->value("windows/showFloat", true).toBool());
    m_lyricButton->setChecked(lyricFeaturesEnabled() && s->value("windows/showLyrics", true).toBool());
    QList<qint64> queue;
    for (auto v : s->value("player/queue").toList())
        queue << v.toLongLong();
    const auto position = s->value("player/position", 0).toLongLong();
    auto restored = std::make_shared<bool>(false);
    connect(m_player.media(), &QMediaPlayer::mediaStatusChanged, this,
            [this, position, restored](auto status) {
                if (!*restored &&
                    (status == QMediaPlayer::LoadedMedia || status == QMediaPlayer::BufferedMedia)) {
                    *restored = true;
                    m_player.media()->setPosition(position);
                }
            });
    if (s->contains("player/currentQueueIndex"))
        m_player.restoreQueue(queue, s->value("player/currentQueueIndex").toInt());
    else
        m_player.setQueue(queue, s->value("player/current").toLongLong(), false);
    setAcceptDrops(true);
    qApp->installEventFilter(this);
}

void MainWindow::navigate(int page) {
    page = qBound(0, page, 4);
    if (page == 3 && !lyricFeaturesEnabled())
        page = 4;
    m_pages->setCurrentIndex(page);
    m_navSearch->setChecked(page == 0);
    m_navLibrary->setChecked(page == 1);
    m_navPlayback->setChecked(page == 2);
    m_navWorkbench->setChecked(page == 3);
    m_navSettings->setChecked(page == 4);
    m_settings->setValue("ui/page", page);
    const QStringList titles = {"搜索", "乐库", "播放", "工作台", "设置"},
                      subtitles = {"搜索音乐视频，下载原始音轨，校准时间轴歌词。",
                                   "左侧选择歌曲，右侧整理歌单。同一首歌可以收录到多个歌单。",
                                   "选择歌曲或歌单开始播放，当前歌曲固定在队列最下方。",
                                   "",
                                   "管理歌词显示、默认获取策略和本机资源。"};
    m_pageTitle->setText(titles[page]);
    m_pageSubtitle->setText(!lyricFeaturesEnabled() && page == 4 ? "管理下载目录、工具和顶部进度条。"
                                                                 : subtitles[page]);
    m_pageSubtitle->setVisible(page >= 2 && page != 3);
    m_headerTools->setVisible(page < 2);
    if (page < 2)
        m_headerTools->setCurrentIndex(page);
    if (page == 4)
        for (const auto &entry : LyricsSourcesSearch::sources()) {
            if (auto box = findChild<QCheckBox *>("onlineSource_" + entry.first)) {
                const QSignalBlocker guard(box);
                box->setChecked(LyricsSourcesSearch::selectedSources(m_settings).contains(entry.first));
            }
            if (auto group = findChild<QButtonGroup *>("settingsPrimaryOnlineSource"))
                for (auto button : group->buttons())
                    if (button->property("value") == m_settings->value("lyrics/primaryOnlineSource", "msr")) {
                        const QSignalBlocker guard(button);
                        button->setChecked(true);
                    }
        }
    if (page == 4)
        if (auto source = m_pages->widget(4)->findChild<QButtonGroup *>("settingsLyricSource")) {
            for (auto button : source->buttons())
                if (button->property("value") == m_settings->value("lyrics/source", "online"))
                    button->setChecked(true);
        }
}

void MainWindow::applyTheme(const QString &name) {
    const auto theme = UiTheme::normalized(name);
    const auto themePalette = UiTheme::palette(theme);
    UiTheme::apply(this, theme);
    m_settings->setValue("ui/theme", theme);
    m_settings->sync();
    const QList<QPushButton *> buttons{m_navSearch, m_navLibrary, m_navPlayback, m_navWorkbench,
                                       m_navSettings};
    for (int i = 0; i < buttons.size(); ++i)
        buttons[i]->setIcon(navigationIcon(i, themePalette));
    findChild<QPushButton *>("previousTrack")
        ->setIcon(transportIcon(0, themePalette.color(QPalette::ButtonText), devicePixelRatioF()));
    findChild<QPushButton *>("nextTrack")
        ->setIcon(transportIcon(2, themePalette.color(QPalette::ButtonText), devicePixelRatioF()));
    m_toggle->setIcon(transportIcon(m_player.media()->playbackState() == QMediaPlayer::PlayingState ? 3 : 1,
                                    themePalette.color(QPalette::HighlightedText), devicePixelRatioF()));
    highlightCurrentTrack();
    // Queue icons are rasterized vectors; regenerate them for the new foreground.
    const auto selected = m_playbackQueue->currentItem();
    const auto selectedKey = selected ? selected->data(0, Qt::UserRole + 1).toULongLong() : 0;
    refreshPlaybackQueue();
    for (int i = 0; i < m_playbackQueue->topLevelItemCount(); ++i) {
        auto item = m_playbackQueue->topLevelItem(i);
        if (item->data(0, Qt::UserRole + 1).toULongLong() == selectedKey)
            m_playbackQueue->setCurrentItem(item);
    }
    for (auto button : findChildren<QPushButton *>())
        if (button->property("downloadAction").isValid())
            button->setIcon(downloadActionIcon(button->property("downloadAction").toInt(),
                                               UiTheme::palette(theme), devicePixelRatioF()));
    for (auto widget : findChildren<QWidget *>())
        widget->update();
    for (int i = 0; i < m_results->topLevelItemCount(); ++i) {
        auto item = m_results->topLevelItem(i);
        if (!item->data(0, Qt::UserRole + 1).toBool())
            item->setIcon(0, emptyCover(UiTheme::palette(theme)));
    }
}

void MainWindow::highlightCurrentTrack() {
    const auto color = UiTheme::forWidget(this).color(QPalette::Accent);
    for (auto tree : {m_tracks, m_playlistTracks, m_browseTracks})
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            auto item = tree->topLevelItem(i);
            item->setForeground(0, item->data(0, Qt::UserRole).toLongLong() == m_player.currentId()
                                       ? QBrush(color) : QBrush());
        }
}
