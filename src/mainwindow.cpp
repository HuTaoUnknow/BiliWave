#include "desktopintegration.h"
#include "lyricworkflow.h"
#include "theme.h"
#include "window.h"
#include <QApplication>
#include <QActionGroup>
#include <QBoxLayout>
#include <QButtonGroup>
#include <QCollator>
#include <QColorDialog>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QNetworkReply>
#include <QPainter>
#include <QRegularExpression>
#include <QRandomGenerator>
#include <QScreen>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSplitter>
#include <QStyle>
#include <algorithm>
#include <memory>

namespace {
class WorkbenchPanelSplitter : public QSplitter {
  public:
    explicit WorkbenchPanelSplitter(QSettings *settings) : m_settings(settings) {
        setObjectName("workbenchPanels");
        const auto saved = settings->value("ui/workbenchPanelRatios").toList();
        if (saved.size() == 2 && saved[0].toDouble() > 0 && saved[1].toDouble() > 0) {
            const double sum = saved[0].toDouble() + saved[1].toDouble();
            for (int i = 0; i < 2; ++i)
                m_ratios[i] = saved[i].toDouble() / sum;
        } else
            settings->setValue("ui/workbenchPanelRatios", QVariantList{.75, .25});
        connect(this, &QSplitter::splitterMoved, this, [this] {
            if (m_applying)
                return;
            const auto values = sizes();
            const int sum = values.value(0) + values.value(1);
            if (sum <= 0 || values.contains(0))
                return;
            QVariantList saved;
            for (int i = 0; i < 2; ++i) {
                m_ratios[i] = double(values[i]) / sum;
                saved << m_ratios[i];
            }
            m_settings->setValue("ui/workbenchPanelRatios", saved);
            m_settings->sync();
        });
        setChildrenCollapsible(false);
        QTimer::singleShot(0, this, [this] { applyRatios(); });
    }

  protected:
    void resizeEvent(QResizeEvent *event) override {
        QSplitter::resizeEvent(event);
        applyRatios();
    }

  private:
    void applyRatios() {
        if (count() != 2)
            return;
        const int available = qMax(1, width() - handleWidth());
        m_applying = true;
        setSizes({qRound(available * m_ratios[0]), qRound(available * m_ratios[1])});
        m_applying = false;
    }
    QSettings *m_settings;
    double m_ratios[2]{.75, .25};
    bool m_applying = false;
};

QPushButton *action(const QString &text, const QString &name = {}) {
    auto b = new QPushButton(text);
    b->setObjectName(name);
    if (name == "primary")
        b->setProperty("primary", true);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}
QLabel *label(const QString &text, const QString &name = {}) {
    auto l = new QLabel(text);
    l->setObjectName(name);
    l->setTextFormat(Qt::PlainText);
    return l;
}
void compactTracks(TrackView *view) {
    view->header()->setStretchLastSection(false);
    view->hideColumn(1);
    view->hideColumn(2);
    view->setHeaderLabels({"歌曲名称", "歌手", "音轨", "时长"});
    view->header()->setSectionResizeMode(3, QHeaderView::Fixed);
    view->header()->resizeSection(3, 75);
}
void lyricSources(QComboBox *combo) {
    combo->addItem("线上匹配", "online");
    combo->addItem("本机识别", "offline");
    combo->addItem("导入 LRC", "local");
}
QIcon navigationIcon(int kind, const QPalette &palette) {
    QIcon icon;
    for (bool active : {false, true}) {
        QPixmap pixmap(QSize(20, 20) * qApp->devicePixelRatio());
        pixmap.setDevicePixelRatio(qApp->devicePixelRatio());
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);
        const QColor color(active ? palette.color(QPalette::HighlightedText)
                                  : palette.color(QPalette::WindowText));
        p.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (kind == 0) {
            p.drawEllipse(QRectF(3, 3, 10, 10));
            p.drawLine(QPointF(12, 12), QPointF(17, 17));
        } else if (kind == 1) {
            p.drawLine(QPointF(7, 14), QPointF(7, 5));
            p.drawLine(QPointF(7, 5), QPointF(15, 3));
            p.drawLine(QPointF(15, 3), QPointF(15, 12));
            p.setBrush(color);
            p.drawEllipse(QRectF(3, 13, 4, 3));
            p.drawEllipse(QRectF(11, 11, 4, 3));
        } else if (kind == 2) {
            p.drawPolygon(QPolygonF{QPointF(5, 3), QPointF(16, 10), QPointF(5, 17)});
        } else if (kind == 3) {
            p.drawRoundedRect(QRectF(2, 3, 16, 14), 1, 1);
            p.drawLine(QPointF(5, 7), QPointF(15, 7));
            p.drawLine(QPointF(5, 10), QPointF(12, 10));
            p.drawLine(QPointF(5, 13), QPointF(14, 13));
        } else {
            p.drawEllipse(QRectF(5, 5, 10, 10));
            p.drawEllipse(QRectF(8, 8, 4, 4));
            p.translate(10, 10);
            for (int i = 0; i < 8; ++i) {
                p.drawLine(QPointF(0, 6), QPointF(0, 8));
                p.rotate(45);
            }
        }
        p.end();
        icon.addPixmap(pixmap, QIcon::Normal, active ? QIcon::On : QIcon::Off);
    }
    return icon;
}
QIcon downloadActionIcon(int type, const QPalette &palette, qreal ratio) {
    if (type == 2)
        return navigationIcon(3, palette);
    QPixmap pixmap(QSize(20, 20) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(palette.color(QPalette::ButtonText), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    if (type == 0) {
        p.drawLine(QPointF(5, 5), QPointF(15, 15));
        p.drawLine(QPointF(5, 15), QPointF(15, 5));
    } else {
        p.drawRoundedRect(QRectF(2, 2, 16, 16), 1.5, 1.5);
        p.drawEllipse(QRectF(4, 7, 10, 10));
        p.drawEllipse(QRectF(8, 11, 2, 2));
        p.drawPolyline(QPolygonF{QPointF(16, 5), QPointF(16, 10), QPointF(13, 14)});
    }
    return QIcon(pixmap);
}
QIcon transportIcon(int kind, const QColor &color, qreal ratio) {
    QPixmap pixmap(QSize(24, 24) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    if (kind == 3) {
        p.drawRoundedRect(QRectF(6, 5, 4, 14), 1, 1);
        p.drawRoundedRect(QRectF(14, 5, 4, 14), 1, 1);
    } else if (kind == 1) {
        p.drawPolygon(QPolygonF{QPointF(8, 4), QPointF(20, 12), QPointF(8, 20)});
    } else {
        if (kind == 0) {
            p.translate(24, 0);
            p.scale(-1, 1);
        }
        p.drawPolygon(QPolygonF{QPointF(5, 5), QPointF(16, 12), QPointF(5, 19)});
        p.drawRoundedRect(QRectF(17, 5, 3, 14), 1, 1);
    }
    return QIcon(pixmap);
}
QIcon emptyCover(const QPalette &palette) {
    QPixmap cover(96, 54);
    cover.fill(palette.color(QPalette::Button));
    QPainter p(&cover);
    p.setPen(palette.color(QPalette::PlaceholderText));
    auto font = p.font();
    font.setPixelSize(24);
    p.setFont(font);
    p.drawText(cover.rect(), Qt::AlignCenter, "♫");
    return QIcon(cover);
}
QString colorSwatchStyle(const QColor &color) {
    return "background:" + color.name() + ";color:" + (color.lightness() > 145 ? "#182334;" : "#ffffff;");
}
} // namespace

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
        navigationButtons[i]->setIcon(navigationIcon(i, palette()));
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
    m_mode->addItems({"顺序播放", "列表播放", "随机播放", "单曲循环"});
    m_mode->setCurrentIndex(qBound(0, s->value("player/mode", 1).toInt(), 3));
    m_player.setMode(Player::Mode(m_mode->currentIndex()));
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
    connect(m_mode, &QComboBox::currentIndexChanged, this, [this](int mode) {
        m_player.setMode(Player::Mode(mode));
        m_settings->setValue("player/mode", mode);
    });
    connect(&m_player, &Player::queueChanged, m_mode, [this] {
        QSignalBlocker guard(m_mode);
        m_mode->setCurrentIndex(m_player.mode());
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
        m_toggle->setIcon(transportIcon(playing ? 3 : 1, Qt::white, devicePixelRatioF()));
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
    UiTheme::apply(this, theme);
    m_settings->setValue("ui/theme", theme);
    m_settings->sync();
    const QList<QPushButton *> buttons{m_navSearch, m_navLibrary, m_navPlayback, m_navWorkbench,
                                       m_navSettings};
    for (int i = 0; i < buttons.size(); ++i)
        buttons[i]->setIcon(navigationIcon(i, palette()));
    findChild<QPushButton *>("previousTrack")
        ->setIcon(transportIcon(0, palette().color(QPalette::ButtonText), devicePixelRatioF()));
    findChild<QPushButton *>("nextTrack")
        ->setIcon(transportIcon(2, palette().color(QPalette::ButtonText), devicePixelRatioF()));
    m_toggle->setIcon(transportIcon(m_player.media()->playbackState() == QMediaPlayer::PlayingState ? 3 : 1,
                                    Qt::white, devicePixelRatioF()));
    for (auto tree : {m_tracks, m_playlistTracks, m_browseTracks})
        for (int i = 0; i < tree->topLevelItemCount(); ++i) {
            auto item = tree->topLevelItem(i);
            item->setForeground(0, item->data(0, Qt::UserRole).toLongLong() == m_player.currentId()
                                       ? QBrush(UiTheme::palette(theme).color(QPalette::Accent))
                                       : QBrush());
        }
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

QWidget *MainWindow::createLibraryPage() {
    auto page = new QWidget;
    auto layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto toolbar = new QHBoxLayout;
    auto local = action("导入本地音频");
    m_search = new QLineEdit;
    m_search->setObjectName("libraryFilter");
    m_search->setPlaceholderText("筛选歌曲名称 / 歌手");
    toolbar->addWidget(m_search, 1);
    auto confirm = action("确认", "confirmLibraryFilter"),
         cancelFilter = action("取消", "cancelLibraryFilter");
    toolbar->addWidget(confirm);
    toolbar->addWidget(cancelFilter);
    toolbar->addWidget(local);
    auto headerWidget = new QWidget;
    headerWidget->setLayout(toolbar);
    toolbar->setContentsMargins(18, 0, 0, 0);
    m_headerTools->addWidget(headerWidget);
    auto split = new QSplitter;
    auto left = new QWidget;
    auto songs = new QVBoxLayout(left);
    songs->setContentsMargins(0, 0, 7, 0);
    auto leftHeader = new QHBoxLayout;
    m_heading = label("全部歌曲", "section");
    leftHeader->addWidget(m_heading, 1);
    m_sortMode = qBound(0, m_settings->value("library/sortMode", 0).toInt(), 2);
    m_sortAscending = m_settings->value("library/sortAscending", true).toBool();
    m_sortButton = action(QStringList{"A~Z", "time", "size"}[m_sortMode], "librarySort");
    m_sortDirection = action(m_sortAscending ? "↓" : "↑", "librarySortDirection");
    m_sortButton->setToolTip("切换：名称 / 时长 / 文件大小");
    m_unassignedTracks = new QCheckBox("显示无归属歌曲");
    m_unassignedTracks->setObjectName("showUnassignedTracks");
    m_unassignedTracks->setToolTip("仅显示尚未加入任何歌单的歌曲。");
    m_unassignedTracks->setChecked(m_settings->value("library/onlyUnassigned", false).toBool());
    leftHeader->addWidget(m_unassignedTracks);
    leftHeader->addWidget(m_sortButton);
    leftHeader->addWidget(m_sortDirection);
    songs->addLayout(leftHeader);
    m_summary = label("", "muted");
    songs->addWidget(m_summary);
    m_tracks = new TrackView;
    m_tracks->setObjectName("availableTracks");
    compactTracks(m_tracks);
    songs->addWidget(m_tracks, 1);
    split->addWidget(left);
    auto right = new QWidget;
    auto playlists = new QVBoxLayout(right);
    playlists->setContentsMargins(7, 0, 0, 0);
    auto rightHeader = new QHBoxLayout;
    m_back = action("← 返回歌单", "playlistBack");
    m_back->hide();
    m_playlistHeading = label("我的歌单", "section");
    m_playlistHeading->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    m_playlistHeading->setMinimumWidth(0);
    m_playlistHeading->setContextMenuPolicy(Qt::CustomContextMenu);
    auto create = action("＋ 新建");
    rightHeader->addWidget(m_back);
    rightHeader->addWidget(m_playlistHeading, 1);
    rightHeader->addWidget(create);
    m_playlistSequential = action("顺序播放", "libraryPlaylistSequential");
    m_playlistShuffle = action("随机播放", "libraryPlaylistShuffle");
    m_playlistSequential->setToolTip("按歌单顺序加入播放队列。");
    m_playlistShuffle->setToolTip("打乱歌单歌曲后加入播放队列。");
    rightHeader->addWidget(m_playlistSequential);
    rightHeader->addWidget(m_playlistShuffle);
    playlists->addLayout(rightHeader);
    m_playlistSummary = label("点击歌单整理已收录歌曲", "muted");
    playlists->addWidget(m_playlistSummary);
    m_playlistPages = new QStackedWidget;
    m_playlistPages->setObjectName("libraryPlaylistPages");
    m_playlists = new PlaylistView;
    m_playlists->setObjectName("playlistList");
    configureRecordCards(m_playlists, true);
    m_playlists->setAcceptDrops(true);
    m_playlists->setDragEnabled(true);
    m_playlists->setDragDropMode(QAbstractItemView::DragDrop);
    m_playlists->viewport()->setAcceptDrops(true);
    m_playlists->setContextMenuPolicy(Qt::CustomContextMenu);
    m_playlistPages->addWidget(m_playlists);
    m_playlistTracks = new TrackView;
    m_playlistTracks->setObjectName("playlistTracks");
    m_playlistTracks->acceptsTrackCopy = true;
    compactTracks(m_playlistTracks);
    m_playlistPages->addWidget(m_playlistTracks);
    playlists->addWidget(m_playlistPages, 1);
    split->addWidget(right);
    split->setSizes({500, 440});
    layout->addWidget(split, 1);
    auto transfer = new QHBoxLayout;
    m_addToPlaylist = action("收录选中 →", "collectTracks");
    m_removeFromPlaylist = action("← 从歌单移出", "removeMembership");
    transfer->addWidget(m_addToPlaylist);
    transfer->addWidget(m_removeFromPlaylist);
    transfer->addStretch();
    layout->addLayout(transfer);
    connect(local, &QPushButton::clicked, this, [this] {
        for (const auto &path : QFileDialog::getOpenFileNames(
                 this, "导入音频", {},
                 "音频 (*.m4s *.m4a *.flac *.mp3 *.wav *.opus *.ogg *.aac *.mp4 *.mka);;所有文件 (*)"))
            m_import.enqueue(path);
    });
    connect(create, &QPushButton::clicked, this, &MainWindow::createPlaylist);
    connect(confirm, &QPushButton::clicked, this, [this] {
        m_libraryNeedle = m_search->text().trimmed();
        refresh();
    });
    connect(m_search, &QLineEdit::returnPressed, confirm, &QPushButton::click);
    connect(cancelFilter, &QPushButton::clicked, this, [this] {
        m_search->clear();
        m_libraryNeedle.clear();
        refresh();
    });
    connect(m_sortButton, &QPushButton::clicked, this, [this] {
        m_sortMode = (m_sortMode + 1) % 3;
        m_sortButton->setText(QStringList{"A~Z", "time", "size"}[m_sortMode]);
        m_settings->setValue("library/sortMode", m_sortMode);
        refresh();
    });
    connect(m_unassignedTracks, &QCheckBox::toggled, this, [this](bool checked) {
        m_settings->setValue("library/onlyUnassigned", checked);
        refresh();
    });
    connect(m_playlistSequential, &QPushButton::clicked, this, [this] { queueLibraryPlaylist(m_playlist, false); });
    connect(m_playlistShuffle, &QPushButton::clicked, this, [this] { queueLibraryPlaylist(m_playlist, true); });
    connect(m_playlists, &PlaylistView::queueRequested, this, &MainWindow::queueLibraryPlaylist);
    connect(m_sortDirection, &QPushButton::clicked, this, [this] {
        m_sortAscending = !m_sortAscending;
        m_sortDirection->setText(m_sortAscending ? "↓" : "↑");
        m_settings->setValue("library/sortAscending", m_sortAscending);
        refresh();
    });
    connect(m_back, &QPushButton::clicked, this, [this] {
        m_playlist = 0;
        m_search->clear();
        m_libraryNeedle.clear();
        refresh();
    });
    connect(m_playlistHeading, &QLabel::customContextMenuRequested, this, &MainWindow::playlistMenu);
    connect(m_playlists, &QListWidget::itemClicked, this, [this](auto item) {
        if (!m_refreshing && item) {
            m_playlist = item->data(Qt::UserRole).toLongLong();
            refresh();
        }
    });
    connect(m_playlists, &PlaylistView::tracksDropped, m_library, &Library::addToPlaylist);
    connect(m_playlists, &QListWidget::customContextMenuRequested, this, &MainWindow::playlistMenu);
    connect(m_addToPlaylist, &QPushButton::clicked, this, [this] {
        if (m_playlist)
            m_library->addToPlaylist(m_playlist, m_tracks->selectedIds());
    });
    connect(m_removeFromPlaylist, &QPushButton::clicked, this, [this] {
        if (m_playlist)
            m_library->removeFromPlaylist(m_playlist, m_playlistTracks->selectedIds());
    });
    for (auto pair : {qMakePair(QKeySequence("Ctrl+A"), m_addToPlaylist),
                      qMakePair(QKeySequence("Ctrl+D"), m_removeFromPlaylist),
                      qMakePair(QKeySequence(Qt::Key_Escape), m_back)}) {
        auto shortcut = new QShortcut(pair.first, page);
        shortcut->setContext(Qt::WidgetWithChildrenShortcut);
        connect(shortcut, &QShortcut::activated, pair.second, &QPushButton::click);
        pair.second->setToolTip(pair.first.toString());
    }
    connect(m_playlistTracks, &TrackView::tracksAdded, this, [this](const auto &ids) {
        if (m_playlist)
            m_library->addToPlaylist(m_playlist, ids);
    });
    connect(m_playlistTracks, &TrackView::reordered, this, [this](const auto &ids) {
        if (m_playlist)
            m_library->reorder(m_playlist, ids);
    });
    for (auto view : {m_tracks, m_playlistTracks}) {
        view->installEventFilter(this);
        connect(view, &QTreeWidget::customContextMenuRequested, this, [this, view](const QPoint &pos) {
            auto item = view->itemAt(pos);
            if (item && !item->isSelected()) {
                view->clearSelection();
                item->setSelected(true);
            }
            m_contextTracks = view;
            trackMenu(pos);
            m_contextTracks = nullptr;
        });
        connect(view, &TrackView::filesDropped, this, [this](const auto &paths) {
            for (const auto &path : paths)
                m_import.enqueue(path);
        });
    }
    return page;
}

void MainWindow::refresh() {
    m_refreshing = true;
    const auto selected = m_tracks->selectedIds(), memberSelection = m_playlistTracks->selectedIds();
    const auto playlists = m_library->playlists();
    QString name;
    QSet<qint64> assigned;
    bool exists = m_playlist == 0;
    m_playlists->clear();
    for (const auto &p : playlists) {
        auto item = new QListWidgetItem(p.value("name").toString(), m_playlists);
        const auto id = p.value("id").toLongLong();
        item->setData(Qt::UserRole, id);
        item->setData(Qt::UserRole + 1, p.value("count"));
        qint64 playlistDuration = 0;
        for (const auto &track : m_library->tracks(id)) {
            playlistDuration += track.value("duration").toLongLong();
            assigned.insert(track.value("id").toLongLong());
        }
        item->setData(Qt::UserRole + 2, playlistDuration);
        item->setToolTip(p.value("name").toString());
        if (id == m_playlist) {
            exists = true;
            name = p.value("name").toString();
            m_playlists->setCurrentItem(item);
        }
    }
    if (!exists)
        m_playlist = 0;
    m_playlists->doItemsLayout();
    const auto members = m_playlist ? m_library->tracks(m_playlist) : QList<Track>{};
    QSet<qint64> collected;
    for (const auto &t : members)
        collected.insert(t.value("id").toLongLong());
    auto all = m_library->tracks(0, m_libraryNeedle);
    QCollator collator(QLocale(QLocale::Chinese, QLocale::China));
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::stable_sort(all.begin(), all.end(), [this, &collator](const Track &a, const Track &b) {
        int compared = 0;
        if (m_sortMode == 0)
            compared = collator.compare(a.value("title").toString(), b.value("title").toString());
        else {
            const auto key = m_sortMode == 1 ? "duration" : "filesize";
            const qint64 left = a.value(key).toLongLong(), right = b.value(key).toLongLong();
            compared = left < right ? -1 : left > right ? 1 : 0;
        }
        if (!compared) {
            const auto left = a.value("id").toLongLong(), right = b.value("id").toLongLong();
            compared = left < right ? -1 : left > right ? 1 : 0;
        }
        return m_sortAscending ? compared < 0 : compared > 0;
    });
    m_tracks->clear();
    m_playlistTracks->clear();
    m_tracks->playlist = 0;
    m_tracks->filtered = !m_libraryNeedle.isEmpty() || m_unassignedTracks->isChecked();
    m_playlistTracks->playlist = m_playlist;
    m_playlistTracks->filtered = false;
    auto append = [this](TrackView *view, const Track &t, const QList<qint64> &selection) {
        const auto id = t.value("id").toLongLong();
        auto item = new QTreeWidgetItem(
            view, {t.value("title").toString() + (t.value("storage").toString() == "url" ? "  [URL]" : ""),
                   t.value("artist").toString(), t.value("codec").toString().toUpper(),
                   displayTime(t.value("duration").toLongLong())});
        item->setData(0, Qt::UserRole, id);
        item->setToolTip(0, t.value("path").toString());
        if (selection.contains(id))
            item->setSelected(true);
        if (id == m_player.currentId())
            item->setForeground(0, UiTheme::forWidget(this).color(QPalette::Accent));
    };
    qint64 duration = 0;
    for (const auto &t : all) {
        if (m_unassignedTracks->isChecked() && assigned.contains(t.value("id").toLongLong()))
            continue;
        if (m_playlist && collected.contains(t.value("id").toLongLong()))
            continue;
        append(m_tracks, t, selected);
        duration += t.value("duration").toLongLong();
    }
    for (const auto &t : members)
        append(m_playlistTracks, t, memberSelection);
    m_heading->setText(m_playlist ? "可收录歌曲" : "全部歌曲");
    m_summary->setText(QString("%1 首 · %2%3")
                           .arg(m_tracks->topLevelItemCount())
                           .arg(displayTime(duration), m_playlist ? " · 当前歌单尚未收录" : ""));
    m_playlistHeading->setText(m_playlist ? name : "我的歌单");
    m_playlistHeading->setToolTip(m_playlist ? name : "我的歌单");
    m_playlistSummary->setText(
        m_playlist ? QString("已收录 %1 首 · 可拖入歌曲或右击管理").arg(members.size())
                   : QString("%1 个歌单 · 点击进入整理，歌曲可收录到多个歌单").arg(playlists.size()));
    m_playlistPages->setCurrentIndex(m_playlist ? 1 : 0);
    m_back->setVisible(m_playlist != 0);
    m_playlistSequential->setVisible(m_playlist != 0);
    m_playlistShuffle->setVisible(m_playlist != 0);
    m_playlistSequential->setEnabled(!members.isEmpty());
    m_playlistShuffle->setEnabled(!members.isEmpty());
    m_addToPlaylist->setEnabled(m_playlist != 0);
    m_removeFromPlaylist->setEnabled(m_playlist != 0);
    m_refreshing = false;
    refreshPlaybackBrowser();
    refreshPlaybackQueue();
}
void MainWindow::queueLibraryPlaylist(qint64 playlist, bool shuffled) {
    QList<qint64> ids;
    for (const auto &song : m_library->tracks(playlist)) ids << song.value("id").toLongLong();
    if (!playlist || ids.isEmpty()) { notify("这个歌单还没有歌曲"); return; }
    if (shuffled)
        for (int i = ids.size() - 1; i > 0; --i) ids.swapItemsAt(i, QRandomGenerator::global()->bounded(i + 1));
    m_player.enqueue(ids);
    notify(QString("已将 %1 首歌曲%2加入播放队列").arg(ids.size()).arg(shuffled ? "打乱后" : "按顺序"));
}

QWidget *MainWindow::createSearchPage() {
    auto page = new QWidget;
    page->setObjectName("searchPage");
    auto layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto searchRow = new QHBoxLayout;
    m_input = new QLineEdit;
    m_input->setObjectName("onlineSearchInput");
    m_input->setPlaceholderText("输入歌曲名称、BV 号或视频链接");
    m_searchButton = action("搜索", "primary");
    m_searchButton->setObjectName("onlineSearchButton");
    searchRow->addWidget(m_input, 1);
    searchRow->addWidget(m_searchButton);
    auto searchHeader = new QWidget;
    searchHeader->setLayout(searchRow);
    searchRow->setContentsMargins(18, 0, 0, 0);
    m_headerTools->addWidget(searchHeader);
    auto catalog = new QWidget;
    auto left = new QVBoxLayout(catalog);
    left->setContentsMargins(0, 0, 8, 0);
    auto resultHeader = new QHBoxLayout;
    resultHeader->addWidget(label("歌曲搜索", "section"));
    resultHeader->addStretch();
    auto urlCollect = action("URL 收录", "collectSearchUrl");
    auto selected = action("下载", "primary");
    selected->setObjectName("downloadSelected");
    resultHeader->addWidget(urlCollect);
    resultHeader->addWidget(selected);
    if (!lyricFeaturesEnabled()) {
        auto trial = action("试听", "trialSearchResult");
        resultHeader->addWidget(trial);
        connect(trial, &QPushButton::clicked, this, [this] {
            const auto item = m_results->currentItem();
            if (!item)
                return;
            const auto id = m_library->addUrl(item->data(0, Qt::UserRole).toMap(), true);
            if (id)
                m_player.setQueue({id});
        });
    }
    left->addLayout(resultHeader);
    m_resultStatus = label("", "muted");
    m_resultStatus->setWordWrap(true);
    left->addWidget(m_resultStatus);
    m_results = new QTreeWidget;
    m_results->setObjectName("searchResults");
    m_results->setColumnCount(4);
    m_results->setHeaderLabels({"封面", "视频标题", "时长", "UP主"});
    m_results->setRootIsDecorated(false);
    m_results->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_results->setIconSize(QSize(96, 54));
    m_results->header()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_results->header()->setStretchLastSection(false);
    m_results->header()->resizeSection(0, 112);
    m_results->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_results->header()->resizeSection(2, 70);
    m_results->header()->resizeSection(3, 100);
    left->addWidget(m_results, 1);
    auto pages = new QHBoxLayout;
    m_previousPage = action("← 上一页");
    m_nextPage = action("下一页 →");
    m_previousPage->setObjectName("previousSearchPage");
    m_nextPage->setObjectName("nextSearchPage");
    m_pageNumber = label("第 1 页", "muted");
    pages->addStretch();
    pages->addWidget(m_previousPage);
    pages->addWidget(m_pageNumber);
    pages->addWidget(m_nextPage);
    pages->addStretch();
    m_previousPage->setEnabled(false);
    m_nextPage->setEnabled(false);
    left->addLayout(pages);
    m_matchAfterDownload = new QCheckBox(page);
    m_matchAfterDownload->hide();
    m_settings->setValue("lyrics/matchAfterDownload", false);
    auto downloadPanel = new QWidget;
    downloadPanel->setObjectName("downloadPanel");
    downloadPanel->setMinimumWidth(300);
    auto downloadLayout = new QVBoxLayout(downloadPanel);
    downloadLayout->setContentsMargins(16, 0, 0, 0);
    auto jobs = new QHBoxLayout;
    jobs->addWidget(label("收录队列", "section"));
    jobs->addStretch();
    auto retry = action("重试下载"), cancel = action("取消下载");
    jobs->addWidget(retry);
    jobs->addWidget(cancel);
    downloadLayout->addLayout(jobs);
    m_jobs = new QListWidget;
    m_jobs->setObjectName("downloadJobs");
    m_jobs->setStyleSheet("QListWidget#downloadJobs::item{padding:0;}");
    m_jobs->setMinimumHeight(55);
    downloadLayout->addWidget(m_jobs, 1);
    connect(m_jobs, &QListWidget::itemDoubleClicked, this, &MainWindow::openDownload);
    if (lyricFeaturesEnabled()) {
        for (const auto &track : m_library->stagedTracks())
            completeDownload((track.value("storage").toString() == "url" ? "url-" : "saved-") +
                                 track.value("id").toString(), track.value("id").toLongLong());
    }
    auto searchSplit = new QSplitter;
    searchSplit->setObjectName("searchDownloadsSplit");
    searchSplit->setChildrenCollapsible(false);
    searchSplit->addWidget(catalog);
    searchSplit->addWidget(downloadPanel);
    searchSplit->setStretchFactor(0, 3);
    searchSplit->setStretchFactor(1, 1);
    searchSplit->setSizes({680, 360});
    layout->addWidget(searchSplit, 1);
    connect(m_searchButton, &QPushButton::clicked, this, [this] { beginSearch(); });
    connect(m_input, &QLineEdit::returnPressed, m_searchButton, &QPushButton::click);
    connect(m_previousPage, &QPushButton::clicked, this, [this] { beginSearch(m_resultPage - 1); });
    connect(m_nextPage, &QPushButton::clicked, this, [this] { beginSearch(m_resultPage + 1); });
    connect(m_results, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) {
        if (!item)
            return;
        QMessageBox choice(this);
        choice.setObjectName("searchResultAction");
        choice.setWindowTitle("收录歌曲");
        choice.setText(item->text(1));
        auto url = choice.addButton("URL 收录", QMessageBox::AcceptRole);
        auto download = choice.addButton("下载", QMessageBox::ActionRole);
        choice.addButton("取消", QMessageBox::RejectRole);
        choice.exec();
        if (choice.clickedButton() == url)
            collectUrls();
        else if (choice.clickedButton() == download)
            downloadResults();
    });
    connect(urlCollect, &QPushButton::clicked, this, &MainWindow::collectUrls);
    connect(selected, &QPushButton::clicked, this, [this] { downloadResults(); });
    connect(m_matchAfterDownload, &QCheckBox::toggled, this,
            [this](bool on) { m_settings->setValue("lyrics/matchAfterDownload", on); });
    connect(retry, &QPushButton::clicked, &m_import, &ImportService::retryFailed);
    connect(cancel, &QPushButton::clicked, &m_import, &ImportService::cancel);
    return page;
}
QWidget *MainWindow::createWorkbenchPage() {
    auto page = new QWidget;
    page->setObjectName("workbenchPage");
    auto layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    if (lyricFeaturesEnabled()) {
        auto split = new WorkbenchPanelSplitter(m_settings);
        m_lyricQueue = new LyricQueue(m_library, m_settings);
        auto left = new QWidget;
        auto leftLayout = new QVBoxLayout(left);
        leftLayout->setContentsMargins(0, 0, 0, 0);
        auto scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setMinimumWidth(325);
        auto workbench = createLyricWorkbench();
        auto actions = workbench->findChild<QWidget *>("workbenchActions");
        workbench->layout()->removeWidget(actions);
        scroll->setWidget(workbench);
        leftLayout->addWidget(scroll, 1);
        leftLayout->addWidget(actions);
        split->addWidget(left);
        split->addWidget(m_lyricQueue);
        split->setStretchFactor(0, 3);
        split->setStretchFactor(1, 1);
        layout->addWidget(split);
    }
    return page;
}
void MainWindow::beginSearch(int page) {
    if (page <= 1)
        m_searchQuery = m_input->text().trimmed();
    if (m_searchQuery.isEmpty())
        return;
    m_lookup.search(m_searchQuery, qMax(1, page));
}
void MainWindow::showSearchResults(const QList<QVariantMap> &items, int page, bool hasNext, bool video) {
    ++m_thumbnailGeneration;
    const auto generation = m_thumbnailGeneration;
    m_results->clear();
    m_resultPage = page;
    m_resultHasNext = hasNext;
    m_resultIsVideo = video;
    m_pageNumber->setText(video ? QString("%1 个分 P").arg(items.size()) : QString("第 %1 页").arg(page));
    m_previousPage->setEnabled(!video && page > 1);
    m_nextPage->setEnabled(!video && hasNext);
    const auto placeholder = emptyCover(UiTheme::forWidget(this));
    QSet<QString> covers;
    for (const auto &row : items) {
        auto item = new QTreeWidgetItem(m_results, {QString(), row.value("title").toString(),
                                                    displayTime(row.value("duration").toLongLong()),
                                                    row.value("uploader").toString()});
        item->setData(0, Qt::UserRole, row);
        item->setIcon(0, placeholder);
        item->setSizeHint(0, QSize(110, 72));
        item->setToolTip(1, row.value("title").toString() + "\n" + row.value("bvid").toString() + "\n" +
                                row.value("uploader").toString());
        if (!row.value("thumbnail").toString().isEmpty())
            covers.insert(row.value("thumbnail").toString());
    }
    if (m_results->topLevelItemCount())
        m_results->setCurrentItem(m_results->topLevelItem(0));
    for (const auto &url : covers) {
        QNetworkRequest request{QUrl(url)};
        request.setTransferTimeout(15000);
        request.setRawHeader("Referer", "https://www.bilibili.com/");
        auto reply = m_thumbnails.get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply, url, generation] {
            reply->deleteLater();
            if (generation != m_thumbnailGeneration || reply->error() != QNetworkReply::NoError)
                return;
            QPixmap pixmap;
            if (!pixmap.loadFromData(reply->readAll()))
                return;
            auto icon = QIcon(pixmap.scaled(96, 54, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            for (int i = 0; i < m_results->topLevelItemCount(); ++i) {
                auto item = m_results->topLevelItem(i);
                if (item->data(0, Qt::UserRole).toMap().value("thumbnail").toString() == url) {
                    item->setData(0, Qt::UserRole + 1, true);
                    item->setIcon(0, icon);
                }
            }
        });
    }
}
void MainWindow::downloadResults(bool all) {
    QList<QTreeWidgetItem *> items;
    if (all)
        for (int i = 0; i < m_results->topLevelItemCount(); ++i)
            items << m_results->topLevelItem(i);
    else
        items = m_results->selectedItems();
    if (items.isEmpty()) {
        m_resultStatus->setText("请先选择要下载的结果");
        return;
    }
    applyLyricSource();
    for (auto item : items)
        m_import.enqueue(item->data(0, Qt::UserRole).toMap().value("url").toString(),
                         lyricFeaturesEnabled());
}
void MainWindow::completeDownload(const QString &taskId, qint64 trackId) {
    if (!lyricFeaturesEnabled())
        return;
    const auto track = m_library->track(trackId);
    if (track.isEmpty())
        return;
    if (!track.value("staged").toBool()) {
        delete m_jobItems.take(taskId);
        return;
    }
    const bool url = track.value("storage").toString() == "url";
    updateDownloadTask(taskId, track.value("title").toString() + (url ? " [URL]" : ""),
                       url ? "URL 待收录" : "下载完成", trackId);
}
void MainWindow::updateDownloadTask(const QString &taskId, const QString &title, const QString &state,
                                    qint64 trackId) {
    auto item = m_jobItems.value(taskId);
    if (!item) {
        item = new QListWidgetItem;
        m_jobs->insertItem(0, item);
        m_jobItems[taskId] = item;
    }
    if (trackId)
        item->setData(Qt::UserRole, trackId);
    item->setToolTip(title + " · " + state);
    auto cell = m_jobs->itemWidget(item);
    if (!cell) {
        cell = new QWidget;
        cell->setObjectName("downloadEntry");
        auto card = new QVBoxLayout(cell);
        card->setContentsMargins(12, 10, 12, 10);
        card->setSpacing(6);
        auto name = label({}, "downloadTaskTitle");
        name->setWordWrap(true);
        name->setMinimumHeight(30);
        name->setMaximumHeight(name->fontMetrics().lineSpacing() * 2 + 4);
        name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        name->setAttribute(Qt::WA_TransparentForMouseEvents);
        card->addWidget(name, 1);
        auto status = label({}, "muted");
        status->setProperty("downloadStatus", true);
        status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        status->setAttribute(Qt::WA_TransparentForMouseEvents);
        card->addWidget(status);
        auto actions = new QHBoxLayout;
        actions->setSpacing(8);
        actions->addStretch();
        const QStringList names{"discardDownloadedTrack", "collectDownloadedTrack", "openDownloadedTrack"};
        const QStringList captions{"删除", "收录到乐库", "工作台"};
        for (int type = 0; type < 3; ++type) {
            auto button = action({}, names[type]);
            button->setProperty("downloadAction", type);
            button->setToolTip(captions[type]);
            button->setAccessibleName(captions[type]);
            button->setFixedSize(44, 32);
            button->setIconSize(QSize(20, 20));
            button->setStyleSheet("padding:0;");
            button->setIcon(downloadActionIcon(type, UiTheme::forWidget(this), devicePixelRatioF()));
            actions->addWidget(button);
            if (type == 0)
                connect(button, &QPushButton::clicked, this, [this, taskId] {
                    auto current = m_jobItems.value(taskId);
                    if (!current)
                        return;
                    if (current->data(Qt::UserRole).toLongLong())
                        discardDownload(current);
                    else
                        m_import.cancelTask(taskId);
                });
            else if (type == 1)
                connect(button, &QPushButton::clicked, this, [this, taskId] {
                    auto current = m_jobItems.value(taskId);
                    const auto id = current ? current->data(Qt::UserRole).toLongLong() : 0;
                    if (id && m_pendingCommit == id)
                        m_pendingCommit = 0;
                    if (id && m_library->saveWorkbench(id))
                        finishWorkbenchImport(id, "已导入乐库");
                });
            else
                connect(button, &QPushButton::clicked, this,
                        [this, taskId] { openDownload(m_jobItems.value(taskId)); });
        }
        card->addLayout(actions);
        item->setSizeHint(QSize(0, 128));
        m_jobs->setItemWidget(item, cell);
    }
    cell->findChild<QLabel *>("downloadTaskTitle")->setText(title);
    for (auto status : cell->findChildren<QLabel *>())
        if (status->property("downloadStatus").toBool())
            status->setText(state);
    const bool ready = item->data(Qt::UserRole).toLongLong() != 0;
    cell->findChild<QPushButton *>("collectDownloadedTrack")->setEnabled(ready);
    cell->findChild<QPushButton *>("openDownloadedTrack")->setEnabled(ready && lyricFeaturesEnabled());
}
void MainWindow::discardDownload(QListWidgetItem *item) {
    if (!item)
        return;
    const auto id = item->data(Qt::UserRole).toLongLong();
    const auto track = m_library->track(id);
    if (!id || track.isEmpty() || !track.value("staged").toBool())
        return;
    QStringList files;
    // Only downloaded UUID-named files belong to this task; imported local sources stay intact.
    const auto original = QFileInfo(track.value("original").toString());
    static const QRegularExpression ownedName("^([0-9a-fA-F-]{36})\\.source\\.");
    const auto match = ownedName.match(original.fileName());
    const QUrl source(track.value("source").toString());
    if (source.scheme() == "https" && match.hasMatch()) {
        const auto root = original.dir().canonicalPath();
        if (!root.isEmpty())
            for (const auto &file : QDir(root).entryInfoList(QDir::Files | QDir::Hidden | QDir::NoSymLinks))
                if (file.fileName().startsWith(match.captured(1) + ".") && file.dir().canonicalPath() == root)
                    files << file.absoluteFilePath();
    } else {
        const QFileInfo rendered(track.value("path").toString());
        static const QRegularExpression generatedName("^([0-9a-fA-F-]{36})\\.");
        const auto generated = generatedName.match(rendered.fileName());
        if (original.isFile() && original.canonicalFilePath() != rendered.canonicalFilePath() &&
            generated.hasMatch()) {
            const auto root = rendered.dir().canonicalPath();
            if (!root.isEmpty())
                for (const auto &file :
                     QDir(root).entryInfoList(QDir::Files | QDir::Hidden | QDir::NoSymLinks))
                    if (file.fileName().startsWith(generated.captured(1) + ".") &&
                        file.dir().canonicalPath() == root &&
                        file.canonicalFilePath() != original.canonicalFilePath())
                        files << file.absoluteFilePath();
        }
    }
    if (m_pendingCommit == id)
        m_pendingCommit = 0;
    if (m_lyricTrack == id)
        m_lyrics.cancel();
    m_lyricQueue->cancelTrack(id);
    m_workbenchTracks.remove(id);
    m_library->deleteTrack(id);
    QStringList remaining;
    for (const auto &file : files)
        if (QFile::exists(file) && !QFile::remove(file))
            remaining << file;
    if (!remaining.isEmpty())
        QTimer::singleShot(150, this, [this, remaining] {
            QStringList locked;
            for (const auto &file : remaining)
                if (QFile::exists(file) && !QFile::remove(file))
                    locked << file;
            if (!locked.isEmpty())
                notify("任务已移除，部分下载文件仍被占用：" + locked.join("；"));
        });
}
void MainWindow::openDownload(QListWidgetItem *item) {
    if (!item || !lyricFeaturesEnabled())
        return;
    const auto id = item->data(Qt::UserRole).toLongLong();
    const auto track = m_library->track(id);
    if (!id || track.isEmpty())
        return;
    m_workbenchTracks.insert(id);
    refreshLyricLibrary();
    selectLyricTrack(id, false);
    navigate(3);
}
void MainWindow::collectUrls() {
    const auto items = m_results->selectedItems();
    int count = 0;
    for (auto item : items) {
        const auto id = m_library->addUrl(item->data(0, Qt::UserRole).toMap(), true);
        if (id && m_library->track(id).value("staged").toBool()) {
            completeDownload("url-" + QString::number(id), id);
            ++count;
        }
    }
    m_resultStatus->setText(count ? QString("已加入收录队列 %1 首 URL 歌曲").arg(count)
                                 : "请选择尚未收录的歌曲；已在乐库的 URL 可右击载入工作台。");
}
void MainWindow::commitLyricTrack(bool skipLyrics) {
    if (!lyricFeaturesEnabled())
        return;
    if (!m_lyricTrack) {
        m_lyricStatus->setText("请选择歌曲源");
        return;
    }
    const auto id = m_lyricTrack;
    m_pendingCommit = 0;
    if (!rememberWorkbenchMetadata())
        return;
    if (skipLyrics || !Lrc::parse(m_library->workbenchTrack(m_lyricTrack).value("lrc").toString()).isEmpty()) {
        m_pendingCommit = 0;
        if (m_library->saveWorkbench(id, !skipLyrics))
            finishWorkbenchImport(id, "已导入乐库");
    } else {
        m_pendingCommit = m_lyricTrack;
        applyLyricSource();
        if (m_lyricSource->currentData().toString() == "offline")
            matchLyrics();
        else
            m_lyrics.fetch(m_lyricTrack, true, m_lyricLookup->text(), true);
    }
}
void MainWindow::loadLibraryTrack(qint64 id) {
    if (!lyricFeaturesEnabled())
        return;
    const auto track = m_library->track(id);
    if (!id || track.isEmpty())
        return;
    m_library->beginWorkbench(id);
    m_workbenchTracks.insert(id);
    refreshLyricLibrary();
    selectLyricTrack(id, false);
    navigate(3);
}
void MainWindow::saveWorkbenchTrack() {
    if (!m_lyricTrack)
        return;
    const auto id = m_lyricTrack;
    if (!rememberWorkbenchMetadata() || !m_library->saveWorkbench(id)) {
        m_lyricStatus->setText("请填写歌曲名称。");
        return;
    }
    finishWorkbenchImport(id, "已保存到乐库");
}
void MainWindow::finishWorkbenchImport(qint64 id, const QString &message) {
    m_workbenchTracks.remove(id);
    m_candidateCache.remove(id);
    m_candidateQueries.remove(id);
    if (m_pendingCommit == id)
        m_pendingCommit = 0;
    if (m_previewTrack == id) {
        m_previewTrack = 0;
        m_lyricFingerprint.clear();
        updateCurrent();
    }
    if (m_lyricTrack != id) {
        refreshLyricLibrary();
        m_importStatus->setText(message);
        return;
    }
    m_lyrics.cancel();
    m_workspaceFingerprint.clear();
    m_lyricTrack = 0;
    refreshLyricLibrary();
    selectLyricTrack(0, false);
    m_importStatus->setText(message);
}
bool MainWindow::rememberWorkbenchMetadata() {
    if (!m_lyricTrack || m_matchTitle->text().trimmed().isEmpty()) {
        m_lyricStatus->setText("请填写歌曲名称。");
        return false;
    }
    return m_library->setWorkbenchMetadata(m_lyricTrack, m_matchTitle->text(), m_matchArtist->text());
}

QWidget *MainWindow::createLyricWorkbench() {
    auto page = new QWidget;
    page->setStyleSheet("QPushButton{padding:6px 8px;} QLineEdit,QComboBox,QSpinBox{padding:5px;}");
    page->setMinimumWidth(310);
    auto layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 0, 0, 0);
    layout->setSizeConstraint(QLayout::SetDefaultConstraint);
    layout->setSpacing(7);
    auto heading = new QHBoxLayout;
    heading->addWidget(label("歌词工作台", "section"), 1);
    m_commitWithLyrics = action("导入乐库", "commitWithLyrics");
    m_commitWithoutLyrics = action("不配置歌词导入乐库", "commitWithoutLyrics");
    m_saveWorkbench = action("保存", "saveWorkbenchTrack");
    m_saveWorkbench->hide();
    heading->addWidget(m_commitWithLyrics);
    heading->addWidget(m_commitWithoutLyrics);
    heading->addWidget(m_saveWorkbench);
    layout->addLayout(heading);
    connect(m_commitWithLyrics, &QPushButton::clicked, this, [this] { commitLyricTrack(false); });
    connect(m_commitWithoutLyrics, &QPushButton::clicked, this, [this] { commitLyricTrack(true); });
    connect(m_saveWorkbench, &QPushButton::clicked, this, &MainWindow::saveWorkbenchTrack);
    m_lyricTracks = new QComboBox;
    m_lyricTracks->setObjectName("lyricTrackSelector");
    m_lyricTracks->setMinimumContentsLength(15);
    m_lyricTracks->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    auto sourceRow = new QHBoxLayout;
    sourceRow->addWidget(label("歌曲源"));
    sourceRow->addWidget(m_lyricTracks, 1);
    layout->addLayout(sourceRow);
    auto metadataRow = new QHBoxLayout;
    m_matchTitle = new QLineEdit;
    m_matchTitle->setObjectName("lyricSongTitle");
    m_matchArtist = new QLineEdit;
    m_matchArtist->setObjectName("lyricSongArtist");
    m_matchArtist->setPlaceholderText("可填写真实歌手，提高匹配准确度");
    metadataRow->addWidget(label("歌曲名"));
    metadataRow->addWidget(m_matchTitle, 1);
    metadataRow->addWidget(label("歌手"));
    metadataRow->addWidget(m_matchArtist, 1);
    layout->addLayout(metadataRow);
    auto remember = [this] {
        if (m_lyricTrack)
            m_library->setWorkbenchMetadata(m_lyricTrack, m_matchTitle->text(), m_matchArtist->text());
    };
    connect(m_matchTitle, &QLineEdit::textChanged, this, remember);
    connect(m_matchArtist, &QLineEdit::textChanged, this, remember);
    m_lyricLookup = new QLineEdit;
    m_lyricLookup->setObjectName("lyricLookupInput");
    m_lyricLookup->setPlaceholderText("可选：搜索关键词、塞壬官网或 LRCLIB 歌词链接");
    auto meta = new QFormLayout;
    auto lookupRow = new QHBoxLayout;
    lookupRow->addWidget(m_lyricLookup, 1);
    auto sourceOptions = action("歌词源", "onlineSourceOptions");
    sourceOptions->setToolTip("设置优先源和备用源；仅当当前源没有结果时查询下一个。");
    auto sourceMenu = new QMenu(sourceOptions);
    auto priorityMenu = sourceMenu->addMenu("优先查询");
    auto priorityGroup = new QActionGroup(sourceMenu);
    priorityGroup->setExclusive(true);
    for (const auto &entry : LyricsSourcesSearch::sources()) {
        auto preferred = priorityMenu->addAction(entry.second);
        preferred->setCheckable(true);
        preferred->setData(entry.first);
        priorityGroup->addAction(preferred);
        connect(preferred, &QAction::triggered, this, [this, entry] {
            m_settings->setValue("lyrics/primaryOnlineSource", entry.first);
            auto enabled = LyricsSourcesSearch::selectedSources(m_settings);
            if (!enabled.contains(entry.first)) enabled.prepend(entry.first);
            m_settings->setValue("lyrics/onlineSources", enabled);
            m_settings->sync();
        });
        auto enabled = sourceMenu->addAction(entry.second);
        enabled->setCheckable(true);
        enabled->setData(entry.first);
        connect(enabled, &QAction::triggered, this, [this, entry](bool checked) {
            auto enabled = LyricsSourcesSearch::selectedSources(m_settings);
            enabled.removeAll(entry.first);
            if (checked) enabled << entry.first;
            m_settings->setValue("lyrics/onlineSources", enabled);
            m_settings->sync();
        });
    }
    connect(sourceMenu, &QMenu::aboutToShow, this, [this, sourceMenu, priorityGroup] {
        const auto enabled = LyricsSourcesSearch::selectedSources(m_settings);
        for (auto item : sourceMenu->actions())
            if (item->isCheckable()) item->setChecked(enabled.contains(item->data().toString()));
        for (auto item : priorityGroup->actions())
            item->setChecked(item->data().toString() == m_settings->value("lyrics/primaryOnlineSource", "msr"));
    });
    sourceOptions->setMenu(sourceMenu);
    lookupRow->addWidget(sourceOptions);
    meta->addRow("在线搜索", lookupRow);
    layout->addLayout(meta);
    auto divider = new QFrame;
    divider->setObjectName("lyricSectionDivider");
    divider->setFrameShape(QFrame::HLine);
    divider->setProperty("separator", true);
    layout->addWidget(divider);
    layout->addWidget(label("歌词匹配", "section"));
    auto options = new QWidget;
    options->setObjectName("lyricMatchingOptions");
    options->setFixedHeight(recognitionFeaturesEnabled() ? 116 : 76);
    auto optionLayout = new QVBoxLayout(options);
    optionLayout->setContentsMargins(0, 0, 0, 0);
    optionLayout->setSpacing(6);
    layout->addWidget(options);
    m_lyricSource = new QComboBox(page);
    m_lyricSource->setObjectName("workbenchLyricSource");
    if (recognitionFeaturesEnabled())
        lyricSources(m_lyricSource);
    else
        m_lyricSource->addItem("线上匹配", "online");
    m_lyricSource->setCurrentIndex(
        qMax(0, m_lyricSource->findData(m_settings->value("lyrics/source", "auto"))));
    m_lyricSource->hide();
    auto matchingSource = new QHBoxLayout;
    auto categories = new QButtonGroup(page);
    categories->setExclusive(true);
    QList<QPushButton *> categoryButtons;
    for (int i = 0; i < m_lyricSource->count(); ++i) {
        auto button = action(m_lyricSource->itemText(i), QString("lyricCategory%1").arg(i));
        button->setCheckable(true);
        categories->addButton(button, i);
        matchingSource->addWidget(button);
        categoryButtons << button;
    }
    connect(categories, &QButtonGroup::idClicked, m_lyricSource, &QComboBox::setCurrentIndex);
    matchingSource->addStretch();
    optionLayout->addLayout(matchingSource);
    m_localLyricMethod = new QComboBox(page);
    m_localLyricMethod->setObjectName("localLyricMethod");
    m_localLyricMethod->addItem("识别字幕 · 优先使用同源视频", 1);
    if (alignmentFeaturesEnabled())
        m_localLyricMethod->addItem("导入歌词 · 按音频匹配时间", 0);
    m_localLyricMethod->hide();
    auto methodPages = new QStackedWidget;
    methodPages->setObjectName("lyricMethodPages");
    methodPages->setFixedHeight(34);
    methodPages->addWidget(new QWidget);
    auto methods = new QWidget;
    auto methodLayout = new QHBoxLayout(methods);
    methodLayout->setContentsMargins(0, 0, 0, 0);
    auto methodGroup = new QButtonGroup(page);
    methodGroup->setExclusive(true);
    QList<QPushButton *> methodButtons;
    const QStringList methodNames = alignmentFeaturesEnabled() ? QStringList{"识别字幕", "导入歌词"}
                                                              : QStringList{"识别字幕"};
    for (int i = 0; i < methodNames.size(); ++i) {
        auto button = action(methodNames[i], QString("localLyricOption%1").arg(i));
        button->setCheckable(true);
        methodGroup->addButton(button, i);
        methodLayout->addWidget(button);
        methodButtons << button;
    }
    connect(methodGroup, &QButtonGroup::idClicked, m_localLyricMethod, &QComboBox::setCurrentIndex);
    methodLayout->addStretch();
    methodPages->addWidget(methods);
    methodPages->addWidget(new QWidget);
    if (recognitionFeaturesEnabled())
        optionLayout->addWidget(methodPages);
    else {
        methodPages->setParent(page);
        methodPages->hide();
    }
    auto buttons = new QHBoxLayout;
    connect(m_lyricLookup, &QLineEdit::returnPressed, this, [this] {
        m_candidateQueries.remove(m_lyricTrack);
        matchLyrics();
    });
    auto match = action("线上匹配", "primary");
    match->setObjectName("matchLyrics");
    optionLayout->addLayout(buttons);
    auto updateSource = [this, match, methodPages, categoryButtons, methodButtons] {
        const auto source = m_lyricSource->currentData().toString();
        methodPages->setCurrentIndex(m_lyricSource->currentIndex());
        categoryButtons[m_lyricSource->currentIndex()]->setChecked(true);
        if (m_localLyricMethod->currentIndex() >= 0 &&
            m_localLyricMethod->currentIndex() < methodButtons.size())
            methodButtons[m_localLyricMethod->currentIndex()]->setChecked(true);
        match->setText(source == "online"                               ? (m_candidateQueries.contains(m_lyricTrack) ? "继续搜索" : "线上匹配")
                       : source == "local"                              ? "导入 LRC"
                       : m_localLyricMethod->currentData().toInt() == 1 ? "识别字幕"
                                                                        : "导入歌词");
    };
    connect(m_lyricSource, &QComboBox::currentIndexChanged, this, updateSource);
    connect(m_localLyricMethod, &QComboBox::currentIndexChanged, this, updateSource);
    updateSource();
    connect(&m_lyrics, &LyricService::activityChanged, match, [this, match](bool busy) {
        setLyricButtonBusy(match, busy || m_lyricQueue->activeFor(m_lyricTrack));
    });
    connect(m_lyricQueue, &LyricQueue::activity, match, [this, match](qint64, const QString &, bool) {
        const bool busy = m_lyrics.matching() || m_lyricQueue->activeFor(m_lyricTrack);
        setLyricButtonBusy(match, busy);
        m_lyricSpinner->setActive(busy);
    });
    m_lyricStatus = label("", "muted");
    m_lyricStatus->setObjectName("lyricMatchStatus");
    m_lyricStatus->setWordWrap(true);
    auto statusRow = new QHBoxLayout;
    m_lyricSpinner = new BusySpinner;
    m_lyricSpinner->setObjectName("lyricMatchSpinner");
    statusRow->addWidget(m_lyricSpinner);
    statusRow->addWidget(m_lyricStatus, 1);
    buttons->addLayout(statusRow, 1);
    m_candidates = new QTreeWidget;
    m_candidates->setObjectName("lyricCandidates");
    m_candidates->setColumnCount(2);
    m_candidates->setHeaderLabels({"候选版本 / 歌手", "时长差"});
    m_candidates->setRootIsDecorated(false);
    m_candidates->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_candidates->header()->resizeSection(1, 65);
    m_candidates->setMaximumHeight(130);
    m_candidates->setMinimumHeight(90);
    m_candidates->hide();
    layout->addWidget(m_candidates);
    m_candidatePreview = new QPlainTextEdit;
    m_candidatePreview->setReadOnly(true);
    m_candidatePreview->setMaximumHeight(100);
    m_candidatePreview->hide();
    layout->addWidget(m_candidatePreview);
    auto use = action("采用候选并试听");
    use->setObjectName("useLyricCandidate");
    use->hide();
    layout->addWidget(use);
    auto realign = action("使用候选文字，重新匹配时间");
    realign->setObjectName("realignLyricCandidate");
    realign->hide();
    layout->addWidget(realign);
    connect(realign, &QPushButton::clicked, this, [this] {
        auto item = m_candidates->currentItem();
        if (!item || m_candidateTrack != m_lyricTrack)
            return;
        if (m_lyrics.busy()) {
            m_lyricStatus->setText("请先取消正在运行的本机听写。");
            return;
        }
        auto c = item->data(0, Qt::UserRole).toMap();
        auto text = c.value("plainLyrics").toString();
        if (text.isEmpty())
            text = LyricWizard::plainText(c.value("syncedLyrics").toString());
        if (!recognitionFeaturesEnabled())
            return;
        LyricWizard wizard(m_library, m_settings, m_lyricTrack, text, this);
        wizard.setQueue(m_lyricQueue);
        wizard.exec();
    });
    m_lyricPreview = new QListWidget;
    m_lyricPreview->setObjectName("timedLyricPreview");
    m_lyricPreview->setStyleSheet("QListWidget#timedLyricPreview::item{padding:0;}"
                                  "QWidget#lyricLineCell{background:transparent;}");
    m_lyricPreview->setMinimumHeight(220);
    m_lyricPreview->setContextMenuPolicy(recognitionFeaturesEnabled() ? Qt::CustomContextMenu
                                                                      : Qt::NoContextMenu);
    connect(m_lyricPreview, &QListWidget::customContextMenuRequested, this, [this](const QPoint &point) {
        auto item = m_lyricPreview->itemAt(point);
        if (!item || !item->data(Qt::UserRole).isValid())
            return;
        const auto row = m_lyricPreview->row(item);
        QMenu menu(this);
        menu.addAction("在上方添加歌词", this, [this, row] { editLyricLine(row, 3); });
        menu.addAction("在下方添加歌词", this, [this, row] { editLyricLine(row, 2); });
        menu.addSeparator();
        menu.addAction("删除歌词", this, [this, row] { editLyricLine(row, 5); });
        menu.addAction("删除歌词并向上合并时间", this, [this, row] { editLyricLine(row, 6); });
        menu.addAction("删除歌词并向下合并时间", this, [this, row] { editLyricLine(row, 7); });
        menu.addSeparator();
        menu.addAction("进度条转到此", this, [this, row] { editLyricLine(row, 8); });
        menu.exec(m_lyricPreview->viewport()->mapToGlobal(point));
    });
    m_lyricPreview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(m_lyricPreview, 1);
    auto offsetRow = new QHBoxLayout;
    offsetRow->addWidget(label("整体偏移", "muted"));
    auto offsetControls = new QWidget;
    offsetControls->setObjectName("lyricOffsetControls");
    auto offsetButtons = new QHBoxLayout(offsetControls);
    offsetButtons->setContentsMargins(0, 0, 0, 0);
    offsetButtons->setSpacing(6);
    m_matchOffset = new QSpinBox;
    m_matchOffset->setObjectName("lyricTimeOffset");
    m_matchOffset->setRange(-600000, 600000);
    m_matchOffset->setSingleStep(100);
    m_matchOffset->setSuffix(" ms");
    m_matchOffset->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_matchOffset->setFixedWidth(m_matchOffset->fontMetrics().horizontalAdvance("-100000 ms") + 24);
    offsetButtons->addWidget(m_matchOffset);
    auto earlier = action("歌词提前显示", "lyricsShowEarlier");
    auto later = action("歌词延后显示", "lyricsShowLater");
    earlier->setToolTip("整首歌词提前显示 100 ms");
    later->setToolTip("整首歌词延后显示 100 ms");
    offsetButtons->addWidget(earlier);
    offsetButtons->addWidget(later);
    connect(earlier, &QPushButton::clicked, m_matchOffset, &QSpinBox::stepUp);
    connect(later, &QPushButton::clicked, m_matchOffset, &QSpinBox::stepDown);
    offsetRow->addWidget(offsetControls);
    auto timing = label("正值提前显示。试听时可实时调整；双击歌词跳转。", "muted");
    timing->setWordWrap(true);
    offsetRow->addWidget(timing, 1);
    layout->addLayout(offsetRow);
    auto actions = new QWidget;
    actions->setObjectName("workbenchActions");
    auto trialRow = new QHBoxLayout(actions);
    trialRow->setContentsMargins(10, 0, 8, 0);
    auto trial = action("▷ 试听当前歌曲", "primary");
    trial->setObjectName("trialLyrics");
    trialRow->addWidget(label("步长", "muted"));
    auto seekStep = new QDoubleSpinBox;
    seekStep->setObjectName("lyricPreviewSeekStep");
    seekStep->setDecimals(1);
    seekStep->setRange(.1, 100);
    seekStep->setSingleStep(.1);
    seekStep->setSuffix(" s");
    seekStep->setValue(m_settings->value("lyrics/previewSeekSeconds", 1.).toDouble());
    seekStep->setFixedWidth(seekStep->fontMetrics().horizontalAdvance("100.0 s") + 42);
    seekStep->setToolTip("前进、后退的时间步长，默认 1 秒；可调整到 0.1～100 秒。");
    trialRow->addWidget(seekStep);
    auto seekButtons = new QWidget;
    seekButtons->setObjectName("lyricPreviewSeekButtons");
    auto seekLayout = new QHBoxLayout(seekButtons);
    seekLayout->setContentsMargins(0, 0, 0, 0);
    seekLayout->setSpacing(6);
    auto backward = action("←", "lyricPreviewBackward");
    auto forward = action("→", "lyricPreviewForward");
    backward->setAccessibleName("后退");
    forward->setAccessibleName("前进");
    backward->setFixedWidth(40);
    forward->setFixedWidth(40);
    backward->setToolTip("按步长后退当前歌曲的播放进度，保留播放或暂停状态。");
    forward->setToolTip("按步长前进当前歌曲的播放进度，保留播放或暂停状态。");
    seekLayout->addWidget(backward);
    seekLayout->addWidget(forward);
    trialRow->addWidget(seekButtons);
    const auto updateSeek = [this, seekButtons] {
        seekButtons->setEnabled(m_lyricTrack && m_player.currentId() == m_lyricTrack &&
                                m_player.media()->isSeekable());
    };
    connect(&m_player, &Player::currentChanged, actions, updateSeek);
    connect(m_player.media(), &QMediaPlayer::seekableChanged, actions, updateSeek);
    updateSeek();
    connect(seekStep, &QDoubleSpinBox::valueChanged, this, [this](double seconds) {
        m_settings->setValue("lyrics/previewSeekSeconds", seconds);
    });
    const auto seek = [this, seekStep](int direction) {
        auto media = m_player.media();
        if (!m_lyricTrack || m_player.currentId() != m_lyricTrack || !media->isSeekable())
            return;
        const auto position = qMax(qint64(0), media->position() + direction * qRound64(seekStep->value() * 1000));
        media->setPosition(media->duration() > 0 ? qMin(position, media->duration()) : position);
    };
    connect(backward, &QPushButton::clicked, this, [seek] { seek(-1); });
    connect(forward, &QPushButton::clicked, this, [seek] { seek(1); });
    trialRow->addStretch();
    trialRow->addWidget(trial);
    trialRow->addWidget(match);
    layout->addWidget(actions);
    connect(m_lyricTracks, &QComboBox::currentIndexChanged, this, [this](int index) {
        auto id = m_lyricTracks->itemData(index).toLongLong();
        selectLyricTrack(id, true);
    });
    connect(m_lyricSource, &QComboBox::currentIndexChanged, this, [this] { applyLyricSource(); });
    connect(match, &QPushButton::clicked, this, [this] { matchLyrics(); });
    connect(trial, &QPushButton::clicked, this, &MainWindow::playLyricPreview);
    connect(m_matchOffset, &QSpinBox::valueChanged, this, [this](int value) {
        if (m_lyricTrack)
            m_library->setWorkbenchOffset(m_lyricTrack, value);
    });
    connect(m_candidates, &QTreeWidget::currentItemChanged, this, [this](auto item, auto) {
        if (item) {
            auto c = item->data(0, Qt::UserRole).toMap();
            auto text = c.value("syncedLyrics").toString();
            m_candidatePreview->setPlainText(
                Lrc::simplifiedChinese(text.isEmpty() ? c.value("plainLyrics").toString() : text,
                                       m_settings->value("lyrics/autoSimplifyChinese", true).toBool()));
        }
    });
    connect(m_candidates, &QTreeWidget::itemSelectionChanged, use, [this, use] {
        const auto item = m_candidates->currentItem();
        const bool timed =
            item &&
            !Lrc::parse(item->data(0, Qt::UserRole).toMap().value("syncedLyrics").toString()).isEmpty();
        use->setEnabled(recognitionFeaturesEnabled() || timed);
        use->setToolTip(timed ? "采用已有时间轴" : "仅有文本；在线版需选择已有时间轴的候选");
    });
    connect(use, &QPushButton::clicked, this, [this] {
        auto item = m_candidates->currentItem();
        if (!item || m_candidateTrack != m_lyricTrack)
            return;
        auto c = item->data(0, Qt::UserRole).toMap();
        if (Lrc::parse(c.value("syncedLyrics").toString()).isEmpty()) {
            if (m_lyrics.busy()) {
                m_lyricStatus->setText("请先取消正在运行的本机听写。");
                return;
            }
            if (!recognitionFeaturesEnabled()) {
                m_lyricStatus->setText("该候选只有文字，请选择已有时间轴的版本。");
                return;
            }
            LyricWizard wizard(m_library, m_settings, m_lyricTrack, c.value("plainLyrics").toString(), this);
            wizard.setQueue(m_lyricQueue);
            wizard.exec();
        } else {
            m_lyrics.useCandidate(m_lyricTrack, c, true);
            playLyricPreview();
        }
    });
    connect(m_lyricPreview, &QListWidget::itemDoubleClicked, this, [this](auto item) {
        if (!m_lyricTrack)
            return;
        editLyricLine(m_lyricPreview->row(item), 4);
    });
    return page;
}
void MainWindow::refreshLyricLibrary() {
    if (!lyricFeaturesEnabled())
        return;
    QSignalBlocker guard(m_lyricTracks);
    m_lyricTracks->clear();
    m_lyricTracks->addItem("", 0);
    if (!recognitionFeaturesEnabled())
        for (const auto &track : m_library->tracks())
            m_workbenchTracks.insert(track.value("id").toLongLong());
    for (const auto id : m_workbenchTracks) {
        const auto t = m_library->workbenchTrack(id);
        if (!t.isEmpty())
            m_lyricTracks->addItem(
                (t.value("staged").toBool() ? "[待入库] " : "") + t.value("title").toString() +
                    (t.value("storage").toString() == "url" ? " [URL]" : ""), id);
    }
    const int index = m_lyricTracks->findData(m_lyricTrack);
    if (index < 0)
        m_lyricTrack = 0;
    m_lyricTracks->setCurrentIndex(qMax(0, index));
}
void MainWindow::selectLyricTrack(qint64 id, bool match) {
    if (!lyricFeaturesEnabled())
        return;
    if (id != m_lyricTrack)
        m_pendingCommit = 0;
    if (m_lyricTrack && id != m_lyricTrack)
        m_library->setWorkbenchMetadata(m_lyricTrack, m_matchTitle->text(), m_matchArtist->text());
    if (!recognitionFeaturesEnabled())
        m_workbenchTracks.insert(id);
    m_lyricTrack = m_workbenchTracks.contains(id) && !m_library->track(id).isEmpty() ? id : 0;
    if (m_lyricSource->currentData().toString() == "online")
        findChild<QPushButton *>("matchLyrics")->setText(m_candidateQueries.contains(m_lyricTrack) ? "继续搜索" : "线上匹配");
    if (m_lyricTrack)
        m_library->beginWorkbench(m_lyricTrack);
    {
        QSignalBlocker guard(m_lyricTracks);
        m_lyricTracks->setCurrentIndex(qMax(0, m_lyricTracks->findData(m_lyricTrack)));
    }
    m_candidates->clear();
    m_candidates->hide();
    m_candidatePreview->hide();
    m_candidateTrack = 0;
    refreshLyricWorkbench(true);
    const bool busy = m_lyrics.matching() || m_lyricQueue->activeFor(m_lyricTrack);
    setLyricButtonBusy(findChild<QPushButton *>("matchLyrics"), busy);
    m_lyricSpinner->setActive(busy);
    if (m_candidateCache.contains(id))
        chooseLyrics(id, m_candidateCache.value(id));
    if (match && m_lyricTrack) {
        applyLyricSource();
        m_lyrics.fetch(m_lyricTrack, false, {}, true);
    }
}
void MainWindow::refreshLyricWorkbench(bool force) {
    if (!lyricFeaturesEnabled())
        return;
    const auto t = m_library->workbenchTrack(m_lyricTrack);
    if (t.isEmpty()) {
        m_candidates->clear();
        m_candidates->hide();
        m_candidatePreview->hide();
        m_candidateTrack = 0;
    }
    const auto fingerprint = QString::number(m_lyricTrack) + "|" + t.value("title").toString() + "|" +
                             t.value("artist").toString() + "|" + t.value("lrc").toString() +
                             QString::number(t.value("lyric_offset").toInt());
    if (!force && fingerprint == m_workspaceFingerprint)
        return;
    m_workspaceFingerprint = fingerprint;
    if (m_lyricLookup->property("track").toLongLong() != m_lyricTrack) {
        m_lyricLookup->clear();
        m_lyricLookup->setProperty("track", m_lyricTrack);
    }
    {
        QSignalBlocker titleGuard(m_matchTitle), artistGuard(m_matchArtist);
        if (m_matchTitle->text() != t.value("title").toString())
            m_matchTitle->setText(t.value("title").toString());
        if (m_matchArtist->text() != t.value("artist").toString())
            m_matchArtist->setText(t.value("artist").toString());
    }
    {
        QSignalBlocker guard(m_matchOffset);
        m_matchOffset->setValue(t.value("lyric_offset").toInt());
    }
    m_matchOffset->setEnabled(m_lyricTrack != 0);
    findChild<QWidget *>("lyricOffsetControls")->setEnabled(m_lyricTrack != 0);
    findChild<QWidget *>("lyricPreviewSeekButtons")->setEnabled(
        m_lyricTrack && m_player.currentId() == m_lyricTrack && m_player.media()->isSeekable());
    const bool selected = m_lyricTrack != 0;
    const bool staged = selected && t.value("staged").toBool();
    m_commitWithLyrics->setEnabled(staged);
    m_commitWithoutLyrics->setEnabled(staged);
    m_saveWorkbench->setVisible(selected && !staged);
    m_saveWorkbench->setEnabled(selected && !staged);
    m_workspaceLines = Lrc::parse(t.value("lrc").toString());
    m_lyricPreview->clear();
    for (const auto &line : m_workspaceLines) {
        const auto stamp = displayTime(line.ms) +
                           QString(".%1").arg(qMax(qint64(0), line.ms) % 1000, 3, 10, QLatin1Char('0'));
        auto item = new QListWidgetItem(stamp + "  " + line.text, m_lyricPreview);
        item->setData(Qt::UserRole, line.ms);
        const int row = m_lyricPreview->row(item);
        auto cell = new QWidget;
        cell->setObjectName("lyricLineCell");
        auto cellLayout = new QHBoxLayout(cell);
        cellLayout->setContentsMargins(6, 2, 4, 2);
        auto text = new QLabel(item->text());
        text->setTextFormat(Qt::PlainText);
        text->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        text->setMinimumWidth(0);
        text->setAttribute(Qt::WA_TransparentForMouseEvents);
        cellLayout->addWidget(text, 1);
        item->setText({});
        item->setToolTip(stamp + "  " + line.text);
        const QStringList captions{"▴", "▾", "×"};
        const int operations[]{-1, 1, 0};
        for (int i = 0; recognitionFeaturesEnabled() && i < 3; ++i) {
            auto button = new QPushButton(captions[i]);
            button->setObjectName(QString("lyricLine%1_%2").arg(row).arg(i));
            button->setFixedSize(26, 26);
            button->setStyleSheet("padding:0;");
            button->setToolTip(i == 0 ? "提前 100 ms" : i == 1 ? "延后 100 ms" : "删除歌词并向上合并时间");
            cellLayout->addWidget(button);
            connect(button, &QPushButton::clicked, this,
                    [this, row, operation = operations[i]] { editLyricLine(row, operation); });
        }
        item->setSizeHint(QSize(0, line.text.contains('\n') ? 58 : 36));
        m_lyricPreview->setItemWidget(item, cell);
    }
    if (m_workspaceLines.isEmpty() && m_lyricTrack)
        m_lyricPreview->addItem("暂无时间轴歌词，可匹配或导入。");
    m_lyricStatus->setText(t.value("lyric_source").toString().isEmpty()
                               ? (m_lyricTrack ? "等待匹配时间轴歌词" : "")
                               : "当前歌词来源：" + t.value("lyric_source").toString());
    syncLyricPreview();
}
void MainWindow::saveLyricLines(const QList<LyricLine> &lines) {
    m_library->setWorkbenchLyrics(m_lyricTrack, Lrc::serialize(lines), "手动调整");
}
void MainWindow::editLyricLine(int row, int operation) {
    auto lines = m_workspaceLines;
    if (row < 0 || row >= lines.size())
        return;
    if (operation == 8) {
        playLyricPreview();
        m_player.media()->setPosition(qMax(qint64(0), lines[row].ms - m_matchOffset->value()));
        return;
    }
    if (!recognitionFeaturesEnabled()) {
        playLyricPreview();
        m_player.media()->setPosition(qMax(qint64(0), lines[row].ms - m_matchOffset->value()));
        return;
    }
    if (operation == 4) {
        bool ok = false;
        const auto text = QInputDialog::getMultiLineText(this, "修改歌词", "歌词", lines[row].text, &ok);
        if (!ok)
            return;
        lines[row].text = text.trimmed();
    } else if (operation == 2 || operation == 3) {
        bool ok = false;
        const auto text = QInputDialog::getMultiLineText(this, "添加歌词", "歌词", {}, &ok);
        if (!ok || text.trimmed().isEmpty())
            return;
        if (operation == 2) {
            const auto next = row + 1 < lines.size() ? lines[row + 1].ms : lines[row].ms + 2000;
            lines.insert(row + 1, {lines[row].ms + (next - lines[row].ms) / 2, text.trimmed()});
        } else {
            const auto previous = row > 0 ? lines[row - 1].ms : qMax(qint64(0), lines[row].ms - 2000);
            lines.insert(row, {previous + (lines[row].ms - previous) / 2, text.trimmed()});
        }
    } else if (operation == 0 || operation == 6) {
        lines.removeAt(row);
    } else if (operation == 5) {
        lines[row].text.clear();
    } else if (operation == 7) {
        const auto removedStart = lines[row].ms;
        lines.removeAt(row);
        if (row < lines.size())
            lines[row].ms = removedStart;
    } else {
        const auto lower = row > 0 ? lines[row - 1].ms + 1 : 0;
        const auto upper = row + 1 < lines.size() ? lines[row + 1].ms - 1 : lines[row].ms + 100;
        lines[row].ms = qMax(lower, qMin(upper, lines[row].ms + operation * 100));
    }
    saveLyricLines(lines);
}
void MainWindow::applyLyricSource() {
    if (!lyricFeaturesEnabled())
        return;
    m_settings->setValue("lyrics/source", m_lyricSource->currentData());
}
void MainWindow::matchLyrics(bool offline) {
    if (!recognitionFeaturesEnabled())
        offline = false;
    if (!lyricFeaturesEnabled())
        return;
    if (!m_lyricTrack) {
        m_lyricStatus->setText("请先选择歌曲，或下载搜索结果。");
        return;
    }
    if (!rememberWorkbenchMetadata()) {
        m_lyricStatus->setText("请填写歌曲名称。");
        return;
    }
    applyLyricSource();
    m_candidateCache.remove(m_lyricTrack);
    m_candidates->clear();
    m_candidates->hide();
    m_candidatePreview->hide();
    if (recognitionFeaturesEnabled() && (offline || m_lyricSource->currentData().toString() == "offline")) {
        openLyricWizard();
    } else if (recognitionFeaturesEnabled() && m_lyricSource->currentData().toString() == "local") {
        const auto path = QFileDialog::getOpenFileName(this, "导入时间轴歌词", {}, "LRC (*.lrc)");
        if (!path.isEmpty())
            m_lyrics.importFile(m_lyricTrack, path, true);
    } else
        m_lyrics.fetch(m_lyricTrack, true, m_lyricLookup->text(), true);
}
void MainWindow::openLyricWizard(const QString &text, int method) {
    if (!recognitionFeaturesEnabled())
        return;
    if (!m_lyricTrack) {
        m_lyricStatus->setText("请先选择歌曲源。");
        return;
    }
    if (!rememberWorkbenchMetadata())
        return;
    if (method < 0)
        method = m_lyricSource->currentData().toString() == "online" ? 2
                 : m_lyricSource->currentData().toString() == "local"
                     ? 0
                     : m_localLyricMethod->currentData().toInt();
    LyricWizard wizard(m_library, m_settings, m_lyricTrack, text, this);
    wizard.setQueue(m_lyricQueue);
    wizard.findChild<QComboBox *>("lyricMethod")->setCurrentIndex(method);
    wizard.exec();
}
void MainWindow::playLyricPreview() {
    if (!lyricFeaturesEnabled())
        return;
    if (!m_lyricTrack)
        return;
    m_previewTrack = m_lyricTrack;
    m_lyricFingerprint.clear();
    if (m_player.currentId() != m_lyricTrack)
        m_player.setQueue({m_lyricTrack});
    else
        m_player.media()->play();
    updateCurrent();
}
void MainWindow::syncLyricPreview() {
    if (!lyricFeaturesEnabled())
        return;
    if (m_player.currentId() != m_lyricTrack || m_workspaceLines.isEmpty())
        return;
    const int index = Lrc::indexAt(m_workspaceLines, m_player.media()->position() + m_matchOffset->value());
    if (index >= 0 && index != m_lyricPreview->currentRow()) {
        m_lyricPreview->setCurrentRow(index);
        m_lyricPreview->scrollToItem(m_lyricPreview->item(index), QAbstractItemView::PositionAtCenter);
    }
}
void MainWindow::chooseLyrics(qint64 id, const QList<QVariantMap> &items) {
    if (!lyricFeaturesEnabled())
        return;
    m_candidateCache[id] = items;
    const auto track = m_library->workbenchTrack(id);
    m_candidateQueries[id] = track.value("title").toString().trimmed() + "\n" +
                             track.value("artist").toString().trimmed() + "\n" +
                             (id == m_lyricTrack ? m_lyricLookup->text().trimmed() : QString());
    if (id != m_lyricTrack)
        return;
    if (m_lyricSource->currentData().toString() == "online")
        findChild<QPushButton *>("matchLyrics")->setText("继续搜索");
    m_candidateTrack = id;
    m_candidates->clear();
    for (const auto &candidate : items) {
        auto item = new QTreeWidgetItem(
            m_candidates,
            {candidate.value("trackName").toString() + " / " + candidate.value("artistName").toString() +
                 (candidate.contains("score") ? QString(" · %1%").arg(candidate.value("score").toInt())
                                              : QString()),
             candidate.value("syncedLyrics").toString().isEmpty()
                 ? "纯文本"
                 : QString::number(candidate.value("difference").toDouble(), 'f', 1) + "s"});
        item->setData(0, Qt::UserRole, candidate);
        item->setToolTip(0, candidate.value("sourceUrl").toString());
    }
    // Each click appends only this batch to the result queue.
    // All versions are viewed and applied from the recognition queue.
    m_candidates->hide();
    m_candidatePreview->hide();
    if (!items.isEmpty())
        m_candidates->setCurrentItem(m_candidates->topLevelItem(0));
    m_lyricStatus->setText(items.isEmpty() ? QString() : "在线结果已加入识别队列，双击查看并应用。");
}

QWidget *MainWindow::createSettingsPage() {
    auto page = new QWidget;
    page->setObjectName("settingsPage");
    auto rootLayout = new QVBoxLayout(page);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(16);
    auto categories = new QHBoxLayout;
    auto categoryGroup = new QButtonGroup(page);
    categoryGroup->setObjectName("settingsCategories");
    auto sections = new QStackedWidget;
    sections->setObjectName("settingsSections");
    QList<QVBoxLayout *> sectionLayouts;
    const QStringList captions{"通用设置", "进度条", "歌词显示", "资源与工具保存", "工作台设置"};
    for (int i = 0; i < captions.size(); ++i) {
        auto button = action(captions[i], QString("settingsCategory%1").arg(i));
        button->setCheckable(true);
        button->setProperty("settingsTab", true);
        categoryGroup->addButton(button, i);
        if (i == 4) categories->insertWidget(1, button);
        else categories->addWidget(button);
        auto content = new QWidget;
        content->setObjectName(QString("settingsSection%1").arg(i));
        if (i == 2 || i == 3 || i == 4) {
            content->setMaximumWidth(i == 4 ? 1000 : 820);
            content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        }
        auto contentLayout = new QVBoxLayout(content);
        contentLayout->setContentsMargins(0, 0, 18, 0);
        contentLayout->setSpacing(16);
        sectionLayouts << contentLayout;
        auto scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        scroll->setWidget(content);
        sections->addWidget(scroll);
        if (i == 2 && !lyricFeaturesEnabled())
            button->setEnabled(false);
    }
    categories->addStretch();
    rootLayout->addLayout(categories);
    rootLayout->addWidget(sections, 1);
    connect(categoryGroup, &QButtonGroup::idClicked, sections, &QStackedWidget::setCurrentIndex);
    categoryGroup->button(0)->setChecked(true);
    auto generalLayout = sectionLayouts[0];
    auto lyricLayout = sectionLayouts[2];
    auto resourceLayout = sectionLayouts[3];
    auto workbenchLayout = sectionLayouts[4];
    auto layout = sectionLayouts[1];
    workbenchLayout->addWidget(label("工作台设置", "section"));
    const auto divider = [](QVBoxLayout *target, const QString &name) {
        auto line = new QFrame;
        line->setObjectName(name);
        line->setFrameShape(QFrame::HLine);
        line->setProperty("separator", true);
        line->setMaximumWidth(900);
        target->addWidget(line);
    };
    generalLayout->addWidget(label("通用设置", "section"));
    auto simplify = new QCheckBox("繁体中文自动转简体");
    simplify->setObjectName("settingsAutoSimplifyChinese");
    simplify->setChecked(m_settings->value("lyrics/autoSimplifyChinese", true).toBool());
    simplify->setToolTip("对在线匹配、字幕识别、导入及已有歌词生效；保留原文和时间轴，关闭后可查看原文。");
    generalLayout->addWidget(simplify);
    connect(simplify, &QCheckBox::toggled, this, [this](bool enabled) {
        m_settings->setValue("lyrics/autoSimplifyChinese", enabled);
        m_settings->sync();
        m_library->setAutoSimplifyLyrics(enabled);
        if (lyricFeaturesEnabled())
            m_lyricQueue->reloadTextSettings();
        if (lyricFeaturesEnabled() && m_candidates->currentItem()) {
            const auto candidate = m_candidates->currentItem()->data(0, Qt::UserRole).toMap();
            const auto timed = candidate.value("syncedLyrics").toString();
            m_candidatePreview->setPlainText(Lrc::simplifiedChinese(
                timed.isEmpty() ? candidate.value("plainLyrics").toString() : timed, enabled));
        }
    });

    lyricLayout->addWidget(label("歌词显示", "section"));
    auto choiceRow = [page](QFormLayout *form, const QString &caption, const QString &name,
                            const QList<QPair<QString, QString>> &options, const QVariant &selected) {
        auto widget = new QWidget;
        auto row = new QHBoxLayout(widget);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(14);
        auto group = new QButtonGroup(page);
        group->setObjectName(name);
        group->setExclusive(true);
        for (int i = 0; i < options.size(); ++i) {
            auto box = new QCheckBox(options[i].first);
            box->setObjectName(name + "_" + options[i].second);
            box->setProperty("value", options[i].second);
            group->addButton(box, i);
            row->addWidget(box);
            if (options[i].second == selected.toString())
                box->setChecked(true);
        }
        if (!group->checkedButton())
            group->button(0)->setChecked(true);
        row->addStretch();
        form->addRow(caption, widget);
        return group;
    };
    auto appearanceForm = new QFormLayout;
    auto themeChoices =
        choiceRow(appearanceForm, "界面主题", "settingsTheme", {{"深色蓝色", "dark"}, {"浅粉玫瑰", "pink"}},
                  UiTheme::normalized(m_settings->value("ui/theme", "dark").toString()));
    connect(themeChoices, &QButtonGroup::idClicked, this, [this, themeChoices] {
        applyTheme(themeChoices->checkedButton()->property("value").toString());
    });
    if (lyricFeaturesEnabled()) {
        auto onlineOptions = new QWidget;
        onlineOptions->setMaximumWidth(900);
        auto onlineForm = new QFormLayout(onlineOptions);
        onlineForm->setContentsMargins(0, 0, 0, 0);
        QList<QPair<QString, QString>> providerChoices;
        for (const auto &entry : LyricsSourcesSearch::sources()) providerChoices << qMakePair(entry.second, entry.first);
        auto primary = choiceRow(onlineForm, "优先歌词源", "settingsPrimaryOnlineSource",
                                 providerChoices,
                                 m_settings->value("lyrics/primaryOnlineSource", "msr"));
        auto enabledRow = new QHBoxLayout;
        const auto selected = LyricsSourcesSearch::selectedSources(m_settings);
        for (const auto &entry : LyricsSourcesSearch::sources()) {
            auto box = new QCheckBox(entry.second);
            box->setObjectName("onlineSource_" + entry.first);
            box->setChecked(selected.contains(entry.first));
            enabledRow->addWidget(box);
            connect(box, &QCheckBox::toggled, this, [this, entry](bool checked) {
                auto values = LyricsSourcesSearch::selectedSources(m_settings);
                values.removeAll(entry.first);
                if (checked) values << entry.first;
                m_settings->setValue("lyrics/onlineSources", values);
                m_settings->sync();
            });
        }
        enabledRow->addStretch();
        onlineForm->addRow("启用备用源", enabledRow);
        connect(primary, &QButtonGroup::idClicked, this, [this, primary] {
            const auto value = primary->checkedButton()->property("value").toString();
            m_settings->setValue("lyrics/primaryOnlineSource", value);
            findChild<QCheckBox *>("onlineSource_" + value)->setChecked(true);
            m_settings->sync();
        });
        auto policy = label("按优先源、备用源依次查询，命中即停；每批最多 5 个结果。请求间隔至少 1 秒，遇到限流停止。", "muted");
        policy->setWordWrap(true);
        onlineForm->addRow(policy);
        workbenchLayout->addWidget(onlineOptions);
    }
    auto audioOutput = new QWidget;
    audioOutput->setMaximumWidth(820);
    auto audioForm = new QFormLayout(audioOutput);
    audioForm->setContentsMargins(0, 0, 0, 0);
    auto deviceRow = new QHBoxLayout;
    auto outputs = new QComboBox;
    outputs->setObjectName("settingsAudioOutput");
    outputs->setMinimumWidth(220);
    outputs->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    outputs->setToolTip("默认跟随系统输出；也可固定耳机或扬声器。设备断开时临时跟随系统，重新接入后自动恢复。");
    outputs->installEventFilter(this);
    auto detect = action("重新检测", "detectAudioOutputs");
    deviceRow->addWidget(outputs, 1);
    deviceRow->addWidget(detect);
    audioForm->addRow("音频输出", deviceRow);
    auto outputStatus = label({}, "muted");
    outputStatus->setObjectName("audioOutputStatus");
    outputStatus->setWordWrap(true);
    audioForm->addRow(QString(), outputStatus);
    auto refreshOutputs = [this, outputs, outputStatus] {
        const QSignalBlocker guard(outputs);
        outputs->clear();
        const auto preferred = m_player.outputDeviceId();
        const auto system = QMediaDevices::defaultAudioOutput();
        outputs->addItem(system.isNull() ? "跟随系统默认（暂无输出设备）"
                                        : "跟随系统默认 · " + system.description(), QByteArray());
        const auto devices = QMediaDevices::audioOutputs();
        for (const auto &device : devices)
            outputs->addItem(device.description() + (device.id() == system.id() ? "（系统默认）" : ""),
                             device.id());
        int selected = outputs->findData(preferred);
        if (selected < 0) {
            outputs->addItem(m_settings->value("audio/outputDeviceName", "已选择的设备").toString() +
                                 "（未连接，暂用系统默认）", preferred);
            selected = outputs->count() - 1;
        }
        outputs->setCurrentIndex(selected);
        const auto actual = m_player.audio()->device();
        outputStatus->setText(actual.isNull() ? "未检测到音频输出设备，接入后自动恢复。"
                                              : "当前输出：" + actual.description());
    };
    connect(&m_player, &Player::audioOutputsChanged, page, refreshOutputs);
    connect(outputs, &QComboBox::activated, page, [this, outputs](int index) {
        const auto id = outputs->itemData(index).toByteArray();
        m_settings->setValue("audio/outputDevice", id.toBase64());
        m_settings->setValue("audio/outputDeviceName", outputs->itemText(index));
        m_settings->sync();
        m_player.setOutputDeviceId(id);
    });
    connect(detect, &QPushButton::clicked, page, [this] { m_player.refreshAudioOutputs(true); });
    refreshOutputs();
    auto closeToTray = new QCheckBox("关闭主窗口时最小化到托盘");
    closeToTray->setObjectName("settingsCloseToTray");
    closeToTray->setChecked(m_settings->value("application/closeToTray", true).toBool());
    closeToTray->setToolTip("关闭按钮隐藏主窗口，播放、下载和识别继续；从托盘菜单选择退出可结束程序。");
    generalLayout->addWidget(closeToTray);
    connect(closeToTray, &QCheckBox::toggled, this, [this](bool enabled) {
        m_settings->setValue("application/closeToTray", enabled);
        m_settings->sync();
    });
    auto autoStart = new QCheckBox("开机自启动（启动到托盘）");
    autoStart->setObjectName("settingsAutoStart");
    autoStart->setChecked(DesktopIntegration::startupEnabled());
    autoStart->setToolTip(
        "为当前 Windows 用户启用开机启动，使用当前程序和资料库路径；取消勾选即移除启动项。");
#ifndef Q_OS_WIN
    autoStart->setEnabled(false);
#endif
    generalLayout->addWidget(autoStart);
    auto startupStatus = label("", "muted");
    startupStatus->setObjectName("startupSettingStatus");
    startupStatus->setWordWrap(true);
    startupStatus->hide();
    generalLayout->addWidget(startupStatus);
    connect(autoStart, &QCheckBox::toggled, this, [this, autoStart, startupStatus](bool enabled) {
        QString error;
        if (!DesktopIntegration::setStartupEnabled(enabled, m_library->directory(), &error)) {
            const QSignalBlocker guard(autoStart);
            autoStart->setChecked(DesktopIntegration::startupEnabled());
            startupStatus->setText(error);
            startupStatus->show();
            return;
        }
        m_settings->setValue("application/autoStart", enabled);
        m_settings->sync();
        startupStatus->clear();
        startupStatus->hide();
        notify(enabled ? "已开启开机自启动" : "已关闭开机自启动");
    });
    divider(generalLayout, "generalDuckingDivider");
    generalLayout->addWidget(label("自动消声", "section"));
    auto duckEnabled = new QCheckBox("自动降低音乐音量");
    duckEnabled->setObjectName("settingsDuckingEnabled");
    duckEnabled->setChecked(m_settings->value("audioDucking/enabled", false).toBool());
    generalLayout->addWidget(duckEnabled);
    auto duckOptions = new QWidget;
    duckOptions->setMaximumWidth(820);
    auto duckForm = new QFormLayout(duckOptions);
    duckForm->setContentsMargins(0, 0, 0, 0);
    auto triggers = new QHBoxLayout;
    auto onSpeech = new QCheckBox("麦克风检测到说话");
    auto onMic = new QCheckBox("其他应用正在使用麦克风");
    auto onOtherAudio = new QCheckBox("其他应用正在发声");
    onSpeech->setObjectName("settingsDuckingOnSpeech");
    onMic->setObjectName("settingsDuckingOnMicrophone");
    onOtherAudio->setObjectName("settingsDuckingOnOtherAudio");
    onSpeech->setChecked(m_settings->value("audioDucking/onSpeech", true).toBool());
    onMic->setChecked(m_settings->value("audioDucking/onMicrophone", true).toBool());
    onOtherAudio->setChecked(m_settings->value("audioDucking/onOtherAudio", true).toBool());
    onOtherAudio->setToolTip(
        "检测其他应用的实际播放音量，排除 BiliWave 自身；静音或无声的播放不会触发，不录制其他应用音频。");
    onSpeech->setToolTip("读取默认麦克风的音量判断说话，不保存或上传音频。较响的环境声音也可能触发。");
    onMic->setToolTip(
        "检测 Windows 麦克风使用状态，不读取其他应用录音。应用内静音但仍占用麦克风时也会触发。");
    triggers->addWidget(onSpeech);
    triggers->addWidget(onMic);
    triggers->addWidget(onOtherAudio);
    triggers->addStretch();
    duckForm->addRow("触发条件", triggers);
    auto reduction = new QSpinBox;
    reduction->setObjectName("settingsDuckingReduction");
    reduction->setRange(0, 100);
    reduction->setSuffix(" %");
    reduction->setMaximumWidth(140);
    reduction->setValue(qBound(0, m_settings->value("audioDucking/reduction", 70).toInt(), 100));
    reduction->setToolTip("相对当前音乐音量降低的比例。70% 表示保留原音量的 30%；100% 为静音。");
    duckForm->addRow("降低幅度", reduction);
    generalLayout->addWidget(duckOptions, 0, Qt::AlignLeft);
    auto duckStatus = label("已关闭", "muted");
    duckStatus->setObjectName("settingsDuckingStatus");
    duckStatus->setWordWrap(true);
    duckStatus->setMaximumWidth(820);
    generalLayout->addWidget(duckStatus);
    duckOptions->setEnabled(duckEnabled->isChecked());
    connect(&m_ducking, &AutoDucking::statusChanged, duckStatus, &QLabel::setText);
    auto applyDucking = [this, duckEnabled, duckOptions, onSpeech, onMic, onOtherAudio, reduction] {
        m_settings->setValue("audioDucking/enabled", duckEnabled->isChecked());
        m_settings->setValue("audioDucking/onSpeech", onSpeech->isChecked());
        m_settings->setValue("audioDucking/onMicrophone", onMic->isChecked());
        m_settings->setValue("audioDucking/onOtherAudio", onOtherAudio->isChecked());
        m_settings->setValue("audioDucking/reduction", reduction->value());
        m_settings->sync();
        duckOptions->setEnabled(duckEnabled->isChecked());
        m_ducking.reloadSettings();
    };
    for (auto box : {duckEnabled, onSpeech, onMic, onOtherAudio})
        connect(box, &QCheckBox::toggled, this, applyDucking);
    connect(reduction, &QSpinBox::valueChanged, this, applyDucking);
    divider(generalLayout, "generalThemeDivider");
    generalLayout->addLayout(appearanceForm);
    divider(generalLayout, "generalAudioOutputDivider");
    generalLayout->addWidget(audioOutput);
    auto showFloat = new QCheckBox("显示顶部进度条"), showLyrics = new QCheckBox("显示桌面歌词");
    showFloat->setObjectName("settingsShowFloat");
    showLyrics->setObjectName("settingsShowLyrics");
    showFloat->setChecked(m_settings->value("windows/showFloat", true).toBool());
    showLyrics->setChecked(m_settings->value("windows/showLyrics", true).toBool());
    layout->addWidget(showFloat);
    if (lyricFeaturesEnabled())
        lyricLayout->addWidget(showLyrics);
    else {
        showLyrics->setParent(page);
        showLyrics->hide();
    }
    connect(showFloat, &QCheckBox::toggled, m_floatButton, &QPushButton::setChecked);
    connect(showLyrics, &QCheckBox::toggled, m_lyricButton, &QPushButton::setChecked);
    connect(m_floatButton, &QPushButton::toggled, showFloat, &QCheckBox::setChecked);
    connect(m_lyricButton, &QPushButton::toggled, showLyrics, &QCheckBox::setChecked);
    layout->addWidget(label("顶部进度条", "section"));
    auto progressForm = new QFormLayout;
    auto colorControl = [this, page, progressForm](const QString &caption, const QString &key,
                                                   const QString &fallback, const QString &name) {
        auto row = new QHBoxLayout;
        auto input = new QLineEdit(m_settings->value(key, fallback).toString());
        input->setObjectName(name);
        input->setMaximumWidth(150);
        auto choose = action("选择颜色");
        row->addWidget(input);
        row->addWidget(choose);
        row->addStretch();
        auto apply = [this, input, choose, key, fallback] {
            const QColor color(input->text().trimmed());
            if (!color.isValid()) {
                input->setText(m_settings->value(key, fallback).toString());
                notify("请输入有效颜色，例如 #FF3355");
                return;
            }
            input->setText(color.name());
            choose->setStyleSheet(colorSwatchStyle(color));
            m_settings->setValue(key, color.name());
            m_settings->sync();
            m_float->applySettings();
        };
        choose->setStyleSheet(colorSwatchStyle(QColor(input->text())));
        connect(input, &QLineEdit::editingFinished, page, apply);
        connect(choose, &QPushButton::clicked, page, [page, input, apply] {
            const auto color = QColorDialog::getColor(QColor(input->text()), page, "进度条颜色");
            if (color.isValid()) {
                input->setText(color.name());
                apply();
            }
        });
        progressForm->addRow(caption, row);
    };
    colorControl("已播放颜色", "progress/leftColor", "#ff3355", "progressLeftColor");
    colorControl("未播放颜色", "progress/rightColor", "#327dff", "progressRightColor");
    colorControl("待播短点颜色", "progress/queueMarkerColor", "#ffffff", "progressQueueMarkerColor");
    auto markerLength = new QSpinBox, markerLimit = new QSpinBox;
    markerLength->setObjectName("progressQueueMarkerLength");
    markerLength->setRange(1, 128);
    markerLength->setSuffix(" px");
    markerLength->setValue(qBound(1, m_settings->value("progress/queueMarkerLength", 10).toInt(), 128));
    markerLength->setMaximumWidth(180);
    markerLimit->setObjectName("progressQueueMarkerLimit");
    markerLimit->setRange(0, 10);
    markerLimit->setSuffix(" 个");
    markerLimit->setValue(qBound(0, m_settings->value("progress/queueMarkerLimit", 10).toInt(), 10));
    markerLimit->setMaximumWidth(180);
    progressForm->addRow("待播短点长度", markerLength);
    progressForm->addRow("待播短点上限", markerLimit);
    connect(markerLength, &QSpinBox::valueChanged, page, [this](int value) {
        m_settings->setValue("progress/queueMarkerLength", value);
        m_settings->sync();
        m_float->applySettings();
    });
    connect(markerLimit, &QSpinBox::valueChanged, page, [this](int value) {
        m_settings->setValue("progress/queueMarkerLimit", value);
        m_settings->sync();
        m_float->applySettings();
    });
    auto fadeHeight = new QSpinBox;
    fadeHeight->setObjectName("progressFadeHeight");
    fadeHeight->setRange(1, 512);
    fadeHeight->setSuffix(" px");
    fadeHeight->setValue(qBound(1, m_settings->value("progress/fadeHeight", 24).toInt(), 512));
    fadeHeight->setMaximumWidth(180);
    fadeHeight->setToolTip("实际屏幕像素高度，向下逐渐透明；修改后立即生效。");
    progressForm->addRow("向下渐隐高度", fadeHeight);
    connect(fadeHeight, &QSpinBox::valueChanged, page, [this](int value) {
        m_settings->setValue("progress/fadeHeight", value);
        m_settings->sync();
        m_float->applySettings();
    });
    auto gradientWidth = new QSpinBox;
    gradientWidth->setObjectName("progressGradientWidth");
    gradientWidth->setRange(0, 4096);
    gradientWidth->setSuffix(" px");
    gradientWidth->setValue(qBound(0, m_settings->value("progress/gradientWidth", 80).toInt(), 4096));
    gradientWidth->setMaximumWidth(180);
    gradientWidth->setToolTip("红蓝交界处的横向渐变宽度，单位为实际屏幕像素；0 表示直接分界。");
    progressForm->addRow("渐变宽度", gradientWidth);
    connect(gradientWidth, &QSpinBox::valueChanged, page, [this](int value) {
        m_settings->setValue("progress/gradientWidth", value);
        m_settings->sync();
        m_float->applySettings();
    });
    layout->addLayout(progressForm);
    auto lyricPane = lyricLayout->parentWidget();
    if (!lyricFeaturesEnabled())
        lyricPane->hide();
    layout = workbenchLayout;
    auto form = new QFormLayout;
    form->setVerticalSpacing(14);
    auto source = choiceRow(form, "默认获取来源", "settingsLyricSource",
                            {{"线上匹配", "online"}, {"本机识别", "offline"}, {"导入 LRC", "local"}},
                            m_settings->value("lyrics/source", "online"));
    auto ocrDevice = choiceRow(form, "字幕识别计算", "settingsOcrDevice",
                               {{"自动（优先 GPU）", "auto"}, {"GPU", "gpu"}, {"CPU", "cpu"}},
                               m_settings->value("lyrics/ocrDevice", "auto"));
    auto ocrLanguage = choiceRow(form, "默认字幕语言", "settingsOcrLanguage",
                                 {{"中英", "zh"},
                                  {"俄语", "ru"},
                                  {"意大利语", "it"},
                                  {"英汉双语", "en_zh"},
                                  {"俄汉双语", "ru_zh"},
                                  {"意汉双语", "it_zh"}},
                                 m_settings->value("lyrics/ocrLanguage", "zh"));
    auto autoFetch = new QCheckBox("切歌后自动获取歌词");
    autoFetch->setChecked(m_settings->value("lyrics/autoFetch", true).toBool());
    form->addRow(autoFetch);
    auto language = choiceRow(form, "歌词配时语言", "settingsAlignLanguage",
                              {{"自动检测", "auto"},
                               {"中文", "zh"},
                               {"英文", "en"},
                               {"日文", "ja"},
                               {"韩文", "ko"},
                               {"俄语", "ru"},
                               {"意大利语", "it"}},
                              m_settings->value("lyrics/language", "auto"));
    if (!alignmentFeaturesEnabled()) {
        form->labelForField(language->buttons().first()->parentWidget())->setVisible(false);
        language->buttons().first()->parentWidget()->hide();
    }
    if (!recognitionFeaturesEnabled()) {
        for (auto button : source->buttons())
            if (button != source->button(0)) { button->setEnabled(false); button->hide(); }
        for (auto group : {ocrDevice, ocrLanguage, language}) {
            auto field = group->buttons().first()->parentWidget();
            form->labelForField(field)->hide();
            field->hide();
        }
        source->button(0)->setChecked(true);
    }
    auto generalPane = new QWidget;
    generalPane->setLayout(form);
    generalPane->setObjectName("workbenchDefaultOptions");
    generalPane->setMaximumWidth(900);
    workbenchLayout->addWidget(generalPane);
    layout = lyricLayout;
    form = new QFormLayout;
    form->setVerticalSpacing(14);
    auto opacity = new QSlider(Qt::Horizontal);
    opacity->setObjectName("settingsLyricOpacity");
    opacity->setRange(0, 100);
    opacity->setValue(m_settings->value("lyrics/opacity", 100).toInt());
    auto opacityText = label("", "muted");
    auto opacityRow = new QHBoxLayout;
    opacityRow->addWidget(opacity, 1);
    opacityRow->addWidget(opacityText);
    auto showOpacity = [opacityText](int value) { opacityText->setText(QString("%1%").arg(value)); };
    showOpacity(opacity->value());
    connect(opacity, &QSlider::valueChanged, page, showOpacity);
    form->addRow("左上不透明度", opacityRow);
    auto size = new QSpinBox;
    size->setObjectName("settingsLyricPrimarySize");
    size->setRange(10, 96);
    size->setValue(m_settings->value("lyrics/fontSize", 30).toInt());
    form->addRow("左上字号", size);
    auto color = action("选择歌词颜色");
    auto colorValue = std::make_shared<QString>(m_settings->value("lyrics/color", "#a4f0d1").toString());
    color->setStyleSheet(colorSwatchStyle(QColor(*colorValue)));
    connect(color, &QPushButton::clicked, page, [page, color, colorValue] {
        auto chosen = QColorDialog::getColor(QColor(*colorValue), page);
        if (chosen.isValid()) {
            *colorValue = chosen.name();
            color->setStyleSheet(colorSwatchStyle(QColor(*colorValue)));
        }
    });
    color->setObjectName("settingsLyricPrimaryColor");
    form->addRow("左上颜色", color);
    auto secondarySize = new QSpinBox;
    secondarySize->setObjectName("settingsLyricSecondarySize");
    secondarySize->setRange(10, 96);
    secondarySize->setValue(
        m_settings->value("lyrics/secondaryFontSize", qMax(12, int(size->value() * .65))).toInt());
    form->addRow("右下字号", secondarySize);
    auto secondaryOpacity = new QSlider(Qt::Horizontal);
    secondaryOpacity->setObjectName("settingsLyricSecondaryOpacity");
    secondaryOpacity->setRange(0, 100);
    secondaryOpacity->setValue(m_settings->value("lyrics/secondaryOpacity", opacity->value() / 2).toInt());
    auto secondaryOpacityRow = new QHBoxLayout;
    auto secondaryOpacityText = label(QString::number(secondaryOpacity->value()) + "%", "muted");
    secondaryOpacityRow->addWidget(secondaryOpacity, 1);
    secondaryOpacityRow->addWidget(secondaryOpacityText);
    connect(secondaryOpacity, &QSlider::valueChanged, page, [secondaryOpacityText](int value) {
        secondaryOpacityText->setText(QString::number(value) + "%");
    });
    form->addRow("右下不透明度", secondaryOpacityRow);
    auto secondaryColor = action("选择歌词颜色", "settingsLyricSecondaryColor");
    auto secondaryColorValue =
        std::make_shared<QString>(m_settings->value("lyrics/secondaryColor", *colorValue).toString());
    secondaryColor->setStyleSheet(colorSwatchStyle(QColor(*secondaryColorValue)));
    connect(secondaryColor, &QPushButton::clicked, page, [page, secondaryColor, secondaryColorValue] {
        const auto chosen = QColorDialog::getColor(QColor(*secondaryColorValue), page);
        if (chosen.isValid()) {
            *secondaryColorValue = chosen.name();
            secondaryColor->setStyleSheet(colorSwatchStyle(QColor(*secondaryColorValue)));
        }
    });
    form->addRow("右下颜色", secondaryColor);
    auto horizontalOffset = new QSpinBox, verticalOffset = new QSpinBox;
    horizontalOffset->setObjectName("settingsLyricHorizontalOffset");
    verticalOffset->setObjectName("settingsLyricVerticalOffset");
    horizontalOffset->setRange(-600, 1200);
    verticalOffset->setRange(0, 400);
    horizontalOffset->setSuffix(" px");
    verticalOffset->setSuffix(" px");
    horizontalOffset->setValue(m_settings->value("lyrics/horizontalOffset", 180).toInt());
    verticalOffset->setValue(m_settings->value("lyrics/verticalOffset", 80).toInt());
    form->addRow("右下水平位差", horizontalOffset);
    form->addRow("右下垂直位差", verticalOffset);
    auto locked = new QCheckBox("锁定桌面歌词位置"),
         through = new QCheckBox("鼠标穿透（在此取消可恢复拖动）");
    locked->setChecked(m_settings->value("lyrics/locked", false).toBool());
    through->setChecked(m_settings->value("lyrics/clickThrough", false).toBool());
    form->addRow(locked);
    form->addRow(through);
    layout->addLayout(form);
    auto explanation = label("双语歌词：外语左上、中文右下；单语歌词：当前句左上、下一句右下。位差以左上文字"
                             "起点为基准，单位为像素。",
                             "muted");
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    layout = resourceLayout;
    layout->addWidget(label("资源与本机工具", "section"));
    auto paths = new QFormLayout;
    paths->setVerticalSpacing(12);
    QMap<QString, QLineEdit *> edits;
    auto pathRow = [&, page](const QString &key, const QString &name, const QString &value, bool directory) {
        auto row = new QHBoxLayout;
        auto edit = new QLineEdit(value);
        auto nameKey = key;
        nameKey.replace('/', '_');
        edit->setObjectName("settingsPath_" + nameKey);
        auto browse = action("选择");
        row->addWidget(edit, 1);
        row->addWidget(browse);
        edits[key] = edit;
        paths->addRow(name, row);
        connect(browse, &QPushButton::clicked, page, [page, edit, directory] {
            auto path = directory ? QFileDialog::getExistingDirectory(page, {}, edit->text())
                                  : QFileDialog::getOpenFileName(page, {}, edit->text());
            if (!path.isEmpty())
                edit->setText(path);
        });
    };
    pathRow(
        "download/directory", "音频保存目录",
        m_settings->value("download/directory", QDir(m_library->directory()).filePath("media")).toString(),
        true);
    pathRow("download/cookies", "可选 Cookie 文件", m_settings->value("download/cookies").toString(), false);
    QStringList tools{"yt-dlp", "ffmpeg", "ffprobe"};
    for (const auto &tool : tools)
        pathRow("tools/" + tool, tool, toolPath(m_settings, tool), false);
    if (recognitionFeaturesEnabled()) {
        if (alignmentFeaturesEnabled())
            pathRow("lyrics/alignmentModel", "已有歌词对齐模型",
                m_settings->value("lyrics/alignmentModel", QDir(resourceRoot()).filePath("models/small.pt"))
                    .toString(),
                false);
        pathRow("tools/lyric-python", "歌词工具运行环境",
                m_settings
                    ->value("tools/lyric-python",
                            QDir(resourceRoot()).filePath("tools/lyrics-runtime/python.exe"))
                    .toString(),
                false);
    }
    layout->addLayout(paths);
    auto note = label(lyricFeaturesEnabled()
                          ? "Cookie 使用本机 Netscape 格式文件。在线歌词仅发送检索信息，本机识别不上传音频。"
                          : "Cookie 使用本机 Netscape 格式文件。",
                      "muted");
    note->setWordWrap(true);
    layout->addWidget(note);
    auto actions = new QHBoxLayout;
    auto save = action("保存设置", "primary"), data = action("打开资料库目录");
    save->setObjectName("saveSettings");
    actions->addWidget(save);
    actions->addWidget(data);
    actions->addStretch();
    rootLayout->addLayout(actions);
    for (auto sectionLayout : sectionLayouts)
        sectionLayout->addStretch();
    connect(data, &QPushButton::clicked, this,
            [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(m_library->directory())); });
    connect(save, &QPushButton::clicked, page, [=, this] {
        if (lyricFeaturesEnabled()) {
            m_settings->setValue("lyrics/source", source->checkedButton()->property("value"));
            m_settings->setValue("lyrics/ocrDevice", ocrDevice->checkedButton()->property("value"));
            m_settings->setValue("lyrics/ocrLanguage", ocrLanguage->checkedButton()->property("value"));
            m_settings->setValue("lyrics/autoFetch", autoFetch->isChecked());
            m_settings->setValue("lyrics/opacity", opacity->value());
            m_settings->setValue("lyrics/fontSize", size->value());
            m_settings->setValue("lyrics/color", *colorValue);
            m_settings->setValue("lyrics/secondaryFontSize", secondarySize->value());
            m_settings->setValue("lyrics/secondaryOpacity", secondaryOpacity->value());
            m_settings->setValue("lyrics/secondaryColor", *secondaryColorValue);
            m_settings->setValue("lyrics/horizontalOffset", horizontalOffset->value());
            m_settings->setValue("lyrics/verticalOffset", verticalOffset->value());
            m_settings->setValue("lyrics/locked", locked->isChecked());
            m_settings->setValue("lyrics/clickThrough", through->isChecked());
            m_settings->setValue("lyrics/language", language->checkedButton()->property("value"));
            m_lyricSource->setCurrentIndex(
                qMax(0, m_lyricSource->findData(source->checkedButton()->property("value"))));
        }
        for (auto it = edits.cbegin(); it != edits.cend(); ++it)
            m_settings->setValue(it.key(), it.value()->text().trimmed());
        m_settings->sync();
        m_overlay->applySettings();
        notify("设置已保存");
    });
    return page;
}
