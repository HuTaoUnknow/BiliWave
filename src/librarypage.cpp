#include "lyricworkflow.h"
#include "theme.h"
#include "window.h"
#include <QBoxLayout>
#include <QCollator>
#include <QFileDialog>
#include <QHeaderView>
#include <QShortcut>
#include <QSplitter>
#include <algorithm>
#include <memory>

#include "uihelpers.h"
using namespace Ui;

namespace {
void compactTracks(TrackView *view) {
    view->header()->setStretchLastSection(false);
    view->hideColumn(1);
    view->hideColumn(2);
    view->setHeaderLabels({"歌曲名称", "歌手", "音轨", "时长"});
    view->header()->setSectionResizeMode(3, QHeaderView::Fixed);
    view->header()->resizeSection(3, 75);
}
} // namespace

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
    playlists->addLayout(rightHeader);
    m_playlistSummary = label("点击歌单整理已收录歌曲", "muted");
    playlists->addWidget(m_playlistSummary);
    m_playlistPages = new QStackedWidget;
    m_playlistPages->setObjectName("libraryPlaylistPages");
    m_playlists = new PlaylistView;
    m_playlists->setObjectName("playlistList");
    configureRecordCards(m_playlists);
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
        connect(view, &TrackView::filesDropped, &m_import, &ImportService::enqueueLocalFiles);
    }
    return page;
}

void MainWindow::refresh() {
    m_refreshing = true;
    const auto selected = m_tracks->selectedIds(), memberSelection = m_playlistTracks->selectedIds();
    const auto playlists = m_library->playlists();
    QString name;
    const auto assigned = m_unassignedTracks->isChecked() ? m_library->assignedTrackIds() : QSet<qint64>{};
    bool exists = m_playlist == 0;
    m_playlists->clear();
    for (const auto &p : playlists) {
        auto item = new QListWidgetItem(p.value("name").toString(), m_playlists);
        const auto id = p.value("id").toLongLong();
        item->setData(Qt::UserRole, id);
        item->setData(Qt::UserRole + 1, p.value("count"));
        item->setData(Qt::UserRole + 2, p.value("duration"));
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
    const auto members = m_playlist ? m_library->trackSummaries(m_playlist) : QList<Track>{};
    QSet<qint64> collected;
    for (const auto &t : members)
        collected.insert(t.value("id").toLongLong());
    auto all = m_library->trackSummaries(0, m_libraryNeedle);
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
            view, {trackTitle(t),
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
    m_addToPlaylist->setEnabled(m_playlist != 0);
    m_removeFromPlaylist->setEnabled(m_playlist != 0);
    m_refreshing = false;
    refreshPlaybackBrowser();
}
