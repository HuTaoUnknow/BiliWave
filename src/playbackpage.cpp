#include "theme.h"
#include "window.h"
#include <QBoxLayout>
#include <QButtonGroup>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QHeaderView>
#include <QMenu>
#include <QMouseEvent>
#include <QHelpEvent>
#include <QToolTip>
#include <QPersistentModelIndex>
#include <QPainter>
#include <QPainterPath>
#include <QScrollBar>
#include <QScopedValueRollback>
#include <QSplitter>
#include <QStyledItemDelegate>
#include <algorithm>

namespace {
QPushButton *playButton(const QString &text, const QString &name) {
    auto button = new QPushButton(text);
    button->setObjectName(name);
    button->setCursor(Qt::PointingHandCursor);
    return button;
}
QLabel *playLabel(const QString &text, const QString &name = "muted") {
    auto label = new QLabel(text);
    label->setObjectName(name);
    label->setTextFormat(Qt::PlainText);
    return label;
}
QList<qint64> trackIds(TrackView *view) {
    QList<qint64> ids;
    for (int i = 0; i < view->topLevelItemCount(); ++i)
        ids << view->topLevelItem(i)->data(0, Qt::UserRole).toLongLong();
    return ids;
}
// Vector artwork stays sharp at all Windows display scales.
QRectF recordQueueAction(const QRect &rect, int action) {
    const auto cell = QRectF(rect).adjusted(5, 5, -5, -5);
    const qreal side = qMin(cell.width() - 12, qreal(160));
    return {cell.right() - 60 + action * 32, cell.top() + 4 + side + 13, 28, 28};
}
void drawRecordQueueAction(QPainter *p, const QRectF &rect, bool shuffled, const QPalette &palette,
                           bool enabled, bool hovered) {
    p->save();
    p->translate(rect.topLeft());
    p->setBrush(palette.color(hovered && enabled ? QPalette::AlternateBase : QPalette::Button));
    p->setPen(QPen(palette.color(QPalette::Mid), 1));
    p->drawRoundedRect(QRectF(0, 0, 28, 28), 4, 4);
    p->setPen(QPen(palette.color(enabled ? QPalette::Active : QPalette::Disabled, QPalette::ButtonText),
                   1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p->setBrush(Qt::NoBrush);
    if (shuffled) {
        p->drawLine(QPointF(6, 8), QPointF(22, 20));
        p->drawLine(QPointF(6, 20), QPointF(22, 8));
        p->drawPolyline(QPolygonF{QPointF(17, 7), QPointF(22, 8), QPointF(21, 13)});
        p->drawPolyline(QPolygonF{QPointF(17, 21), QPointF(22, 20), QPointF(21, 15)});
    } else {
        for (int y : {8, 14, 20}) p->drawLine(QPointF(5, y), QPointF(14, y));
        p->drawLine(QPointF(21, 7), QPointF(21, 21));
        p->drawPolyline(QPolygonF{QPointF(17, 17), QPointF(21, 21), QPointF(25, 17)});
    }
    p->restore();
}
class RecordCardDelegate : public QStyledItemDelegate {
  public:
    explicit RecordCardDelegate(PlaylistView *view) : QStyledItemDelegate(view), m_view(view) {
        view->viewport()->installEventFilter(this);
    }
    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override {
        return {184, 226};
    }
    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override {
        const auto palette = option.widget ? UiTheme::forWidget(option.widget) : option.palette;
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const auto cell = QRectF(option.rect).adjusted(5, 5, -5, -5);
        const qreal side = qMin(cell.width() - 12, qreal(160));
        QRectF box(cell.center().x() - side / 2, cell.top() + 4, side, side);
        const bool selected = option.state & QStyle::State_Selected;
        const bool hovered = option.state & QStyle::State_MouseOver;
        p->setPen(QPen(selected ? palette.color(QPalette::Accent) : palette.color(QPalette::Mid),
                       selected ? 2 : 1));
        p->setBrush(palette.color(hovered ? QPalette::AlternateBase : QPalette::Button));
        p->drawRoundedRect(box, 11, 11);
        const auto point = [&](qreal x, qreal y) {
            return QPointF(box.left() + x * side, box.top() + y * side);
        };
        const auto center = point(.47, .53);
        const qreal radius = side * .355;
        p->setBrush(palette.color(QPalette::Base));
        p->setPen(QPen(palette.color(QPalette::Mid), 1));
        p->drawEllipse(center, radius, radius);
        p->setBrush(Qt::NoBrush);
        for (int i = 0; i < 9; ++i) {
            const auto r = radius * (.95 - i * .065);
            p->setPen(QPen(palette.color(i % 2 ? QPalette::Mid : QPalette::AlternateBase), .8));
            p->drawEllipse(center, r, r);
        }
        p->setPen(QPen(palette.color(QPalette::Mid), 1));
        p->setBrush(palette.color(QPalette::Accent));
        p->drawEllipse(center, radius * .265, radius * .265);
        p->setBrush(palette.color(QPalette::Button));
        p->setPen(Qt::NoPen);
        p->drawEllipse(center, 3, 3);
        // Tonearm descends from the upper right corner onto the record.
        QPainterPath arm;
        arm.moveTo(point(.84, .16));
        arm.lineTo(point(.84, .36));
        arm.lineTo(point(.69, .62));
        p->setPen(
            QPen(palette.color(QPalette::PlaceholderText), 4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p->drawPath(arm);
        p->setBrush(palette.color(QPalette::Text));
        p->setPen(QPen(palette.color(QPalette::Mid), 1));
        p->drawEllipse(point(.84, .16), 6, 6);
        p->drawLine(point(.69, .62), point(.655, .67));
        auto font = option.font;
        font.setBold(true);
        p->setFont(font);
        p->setPen(palette.color(QPalette::Text));
        const bool actions = m_view->property("recordQueueActions").toBool();
        QRectF title(cell.left() + (actions ? 1 : 0), box.bottom() + (actions ? 8 : 11),
                     actions ? cell.width() - 66 : cell.width(), 24);
        p->drawText(
            title, actions ? Qt::AlignLeft | Qt::AlignVCenter : Qt::AlignCenter,
            QFontMetrics(font).elidedText(index.data().toString(), Qt::ElideRight, int(title.width())));
        font.setBold(false);
        font.setPixelSize(12);
        p->setFont(font);
        p->setPen(palette.color(QPalette::PlaceholderText));
        p->drawText(QRectF(title.left(), title.bottom() + 2, title.width(), 20),
                    actions ? Qt::AlignLeft | Qt::AlignVCenter : Qt::AlignCenter,
                    QString("%1 首 · %2")
                        .arg(index.data(Qt::UserRole + 1).toInt())
                        .arg(displayTime(index.data(Qt::UserRole + 2).toLongLong())));
        if (actions) {
            const auto cursor = m_view->viewport()->mapFromGlobal(QCursor::pos());
            for (int i = 0; i < 2; ++i) {
                const auto rect = recordQueueAction(option.rect, i);
                drawRecordQueueAction(p, rect, i == 1, palette, index.data(Qt::UserRole + 1).toInt() > 0,
                                      rect.contains(cursor));
            }
        }
        p->restore();
    }
  protected:
    bool eventFilter(QObject *object, QEvent *event) override {
        if (object != m_view->viewport() || !m_view->property("recordQueueActions").toBool())
            return QStyledItemDelegate::eventFilter(object, event);
        const auto hit = [this](const QPoint &pos) {
            const auto index = m_view->indexAt(pos);
            if (!index.isValid()) return qMakePair(index, -1);
            const auto rect = m_view->visualItemRect(m_view->item(index.row()));
            for (int i = 0; i < 2; ++i)
                if (recordQueueAction(rect, i).contains(pos)) return qMakePair(index, i);
            return qMakePair(index, -1);
        };
        if (event->type() == QEvent::MouseMove) m_view->viewport()->update();
        if (event->type() == QEvent::ToolTip) {
            auto help = static_cast<QHelpEvent *>(event);
            const auto target = hit(help->pos());
            if (target.second >= 0) {
                QToolTip::showText(help->globalPos(), target.second ? "打乱后加入播放队列" : "按歌单顺序加入播放队列", m_view);
                return true;
            }
        }
        if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonDblClick) {
            auto mouse = static_cast<QMouseEvent *>(event);
            const auto target = hit(mouse->position().toPoint());
            if (mouse->button() == Qt::LeftButton && target.second >= 0) {
                if (event->type() == QEvent::MouseButtonPress) { m_pressed = target.first; m_action = target.second; }
                return true;
            }
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            auto mouse = static_cast<QMouseEvent *>(event);
            const auto target = hit(mouse->position().toPoint());
            if (m_action >= 0) {
                const int action = m_action;
                const auto pressed = m_pressed;
                m_action = -1;
                m_pressed = QPersistentModelIndex();
                if (mouse->button() == Qt::LeftButton && pressed.isValid() && target.first == pressed &&
                    target.second == action && pressed.data(Qt::UserRole + 1).toInt() > 0)
                    emit m_view->queueRequested(pressed.data(Qt::UserRole).toLongLong(), action == 1);
                return true;
            }
            if (target.second >= 0) return true;
        }
        return QStyledItemDelegate::eventFilter(object, event);
    }
  private:
    PlaylistView *m_view;
    QPersistentModelIndex m_pressed;
    int m_action = -1;
};
class PlayingNote : public QWidget {
  public:
    PlayingNote(Player *player, quint64 key, QWidget *parent = nullptr)
        : QWidget(parent), m_player(player), m_key(key) {
        setObjectName(QString("playingNote_%1").arg(key));
        setFixedSize(36, 36);
        setStyleSheet("background:transparent;");
        setAttribute(Qt::WA_TransparentForMouseEvents);
        setAccessibleName("播放状态");
        m_timer.setInterval(40);
        connect(&m_timer, &QTimer::timeout, this, [this] {
            m_angle = (m_angle + 5) % 360;
            setProperty("angle", m_angle);
            update();
        });
        connect(player->media(), &QMediaPlayer::playbackStateChanged, this, [this] { updateState(); });
        updateState();
    }

  protected:
    void showEvent(QShowEvent *event) override {
        QWidget::showEvent(event);
        updateState();
    }
    void hideEvent(QHideEvent *event) override {
        QWidget::hideEvent(event);
        updateState();
    }
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        p.translate(18, 18);
        p.rotate(m_angle);
        const bool current = m_player && m_key == m_player->currentEntry();
        const QColor color =
            UiTheme::forWidget(this).color(current ? QPalette::Accent : QPalette::PlaceholderText);
        p.setPen(QPen(color, 1.4));
        p.setBrush(Qt::NoBrush);
        p.drawArc(QRectF(-13, -13, 26, 26), 20 * 16, 285 * 16);
        p.setPen(QPen(color, 2.1, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawLine(QPointF(-2, 6), QPointF(-2, -6));
        p.drawLine(QPointF(-2, -6), QPointF(7, -8));
        p.drawLine(QPointF(7, -8), QPointF(7, 4));
        p.setBrush(color);
        p.drawEllipse(QRectF(-7, 3, 5, 4));
        p.drawEllipse(QRectF(2, 1, 5, 4));
    }

  private:
    void updateState() {
        const bool rotating = isVisible() && m_player && m_key == m_player->currentEntry() &&
                              m_player->media()->playbackState() == QMediaPlayer::PlayingState;
        setProperty("rotating", rotating);
        if (rotating)
            m_timer.start();
        else
            m_timer.stop();
        update();
    }
    QPointer<Player> m_player;
    quint64 m_key;
    QTimer m_timer;
    int m_angle = 0;
};
QIcon queueActionIcon(int type, qreal ratio, const QColor &color) {
    QPixmap pixmap(QSize(20, 20) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(color, 1.7, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    if (type == 2) {
        p.drawLine(QPointF(5, 5), QPointF(15, 15));
        p.drawLine(QPointF(5, 15), QPointF(15, 5));
    } else {
        if (type == 1) {
            p.translate(0, 20);
            p.scale(1, -1);
        }
        p.drawLine(QPointF(4, 3), QPointF(16, 3));
        p.drawLine(QPointF(10, 17), QPointF(10, 7));
        p.drawLine(QPointF(6, 11), QPointF(10, 7));
        p.drawLine(QPointF(10, 7), QPointF(14, 11));
    }
    return QIcon(pixmap);
}
} // namespace
void configureRecordCards(PlaylistView *view, bool queueActions) {
    view->setProperty("recordQueueActions", queueActions);
    view->setViewMode(QListView::IconMode);
    view->setFlow(QListView::LeftToRight);
    view->setWrapping(true);
    view->setResizeMode(QListView::Adjust);
    view->setMovement(QListView::Static);
    view->setGridSize(QSize(184, 226));
    view->setSpacing(4);
    view->setItemDelegate(new RecordCardDelegate(view));
    view->setMouseTracking(true);
    view->setStyleSheet(
        "QListWidget{background:transparent;border:0;} QListWidget::item{padding:0;border:0;}");
}

PlaybackQueueView::PlaybackQueueView(Library *library, QWidget *parent)
    : QTreeWidget(parent), m_library(library) {
    setColumnCount(1);
    setHeaderHidden(true);
    setRootIsDecorated(false);
    setAcceptDrops(true);
    setDragDropMode(QAbstractItemView::DropOnly);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setUniformRowHeights(true);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    header()->setSectionResizeMode(0, QHeaderView::Stretch);
    setStyleSheet("QTreeWidget::item{padding:0;} QWidget#queueEntry{background:transparent;}");
}
void PlaybackQueueView::updateGeometries() {
    if (m_updatingGeometry)
        return;
    QScopedValueRollback<bool> guard(m_updatingGeometry, true);
    QTreeWidget::updateGeometries();
    int rowsHeight = 0;
    for (int row = 0; row < topLevelItemCount(); ++row)
        rowsHeight += rowHeight(indexFromItem(topLevelItem(row)));
    const auto margins = viewportMargins();
    const int available = viewport()->height() + margins.top();
    const int padding = rowsHeight > 0 ? qMax(0, available - rowsHeight) : 0;
    if (padding != margins.top())
        setViewportMargins(margins.left(), padding, margins.right(), margins.bottom());
}
bool PlaybackQueueView::event(QEvent *event) {
    // The empty area reserved above the bottom-aligned viewport belongs to the
    // scroll area itself, so forward its drops through the same queue handlers.
    if (event->type() == QEvent::DragEnter) {
        dragEnterEvent(static_cast<QDragEnterEvent *>(event));
        return true;
    }
    if (event->type() == QEvent::DragMove) {
        dragMoveEvent(static_cast<QDragMoveEvent *>(event));
        return true;
    }
    if (event->type() == QEvent::Drop) {
        dropEvent(static_cast<QDropEvent *>(event));
        return true;
    }
    return QTreeWidget::event(event);
}
void PlaybackQueueView::dragEnterEvent(QDragEnterEvent *event) {
    if (event->mimeData()->hasFormat(TrackMime) || event->mimeData()->hasFormat(PlaylistMime))
        event->acceptProposedAction();
}
void PlaybackQueueView::dragMoveEvent(QDragMoveEvent *event) {
    if (event->mimeData()->hasFormat(TrackMime) || event->mimeData()->hasFormat(PlaylistMime))
        event->acceptProposedAction();
}
void PlaybackQueueView::dropEvent(QDropEvent *event) {
    const auto ids = droppedTracks(event->mimeData(), m_library);
    if (ids.isEmpty())
        return;
    emit tracksDropped(ids);
    event->setDropAction(Qt::CopyAction);
    event->accept();
}

QWidget *MainWindow::createPlaybackPage() {
    auto page = new QWidget;
    page->setObjectName("playbackPage");
    auto layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto split = new QSplitter;
    split->setChildrenCollapsible(false);
    auto left = new QWidget;
    auto browser = new QVBoxLayout(left);
    browser->setContentsMargins(0, 0, 16, 0);
    browser->setSpacing(12);
    auto toggleRow = new QHBoxLayout;
    m_browseSongs = playButton("歌曲", "playbackSongsTab");
    m_browsePlaylists = playButton("歌单", "playbackPlaylistsTab");
    auto group = new QButtonGroup(this);
    group->setExclusive(true);
    for (auto button : {m_browseSongs, m_browsePlaylists}) {
        button->setCheckable(true);
        button->setMinimumWidth(86);
        group->addButton(button);
        toggleRow->addWidget(button);
    }
    m_browseSongs->setChecked(true);
    toggleRow->addStretch();
    browser->addLayout(toggleRow);
    auto filterRow = new QHBoxLayout;
    m_browseFilter = new QLineEdit;
    m_browseFilter->setObjectName("playbackFilter");
    m_browseFilter->setPlaceholderText("筛选歌曲名称");
    m_browseFilter->setClearButtonEnabled(true);
    filterRow->addWidget(m_browseFilter, 1);
    auto playAll = playButton("播放列表", "playbackPlayAll");
    playAll->setProperty("primary", true);
    filterRow->addWidget(playAll);
    browser->addLayout(filterRow);
    auto headingRow = new QHBoxLayout;
    m_browseBack = playButton("← 返回歌单", "playbackPlaylistBack");
    headingRow->addWidget(m_browseBack);
    m_browseHeading = playLabel("全部歌曲", "section");
    headingRow->addWidget(m_browseHeading, 1);
    browser->addLayout(headingRow);
    m_browseSummary = playLabel("");
    browser->addWidget(m_browseSummary);
    m_browsePages = new QStackedWidget;
    m_browseTracks = new TrackView;
    m_browseTracks->setObjectName("playbackTracks");
    m_browseTracks->hideColumn(1);
    m_browseTracks->hideColumn(2);
    m_browseTracks->setAcceptDrops(false);
    m_browseTracks->setDragDropMode(QAbstractItemView::DragOnly);
    m_browseTracks->setContextMenuPolicy(Qt::CustomContextMenu);
    m_browseTracks->header()->setStretchLastSection(false);
    m_browseTracks->header()->setSectionResizeMode(3, QHeaderView::Fixed);
    m_browseTracks->header()->resizeSection(3, 82);
    m_browseTracks->setHeaderLabels({"歌曲名称", "", "", "时长"});
    m_browseTracks->setColumnCount(5);
    m_browseTracks->headerItem()->setText(4, "");
    m_browseTracks->header()->setSectionResizeMode(4, QHeaderView::Fixed);
    m_browseTracks->header()->resizeSection(4, 40);
    m_browsePages->addWidget(m_browseTracks);
    m_browsePlaylistCards = new PlaylistView;
    m_browsePlaylistCards->setObjectName("playbackPlaylistCards");
    configureRecordCards(m_browsePlaylistCards);
    m_browsePlaylistCards->setViewMode(QListView::IconMode);
    m_browsePlaylistCards->setFlow(QListView::LeftToRight);
    m_browsePlaylistCards->setWrapping(true);
    m_browsePlaylistCards->setResizeMode(QListView::Adjust);
    m_browsePlaylistCards->setMovement(QListView::Static);
    m_browsePlaylistCards->setGridSize(QSize(184, 226));
    m_browsePlaylistCards->setSpacing(4);
    m_browsePlaylistCards->setDragEnabled(true);
    m_browsePlaylistCards->setAcceptDrops(false);
    m_browsePlaylistCards->setDragDropMode(QAbstractItemView::DragOnly);
    m_browsePlaylistCards->setMouseTracking(true);
    m_browsePlaylistCards->setStyleSheet(
        "QListWidget{background:transparent;border:0;} QListWidget::item{padding:0;border:0;}");
    m_browsePages->addWidget(m_browsePlaylistCards);
    browser->addWidget(m_browsePages, 1);
    split->addWidget(left);
    auto right = new QWidget;
    right->setObjectName("playbackQueuePane");
    right->setMinimumWidth(480);
    right->setMaximumWidth(690);
    auto queue = new QVBoxLayout(right);
    queue->setContentsMargins(16, 0, 0, 0);
    queue->setSpacing(10);
    auto queueHeading = new QHBoxLayout;
    queueHeading->addWidget(playLabel("播放队列", "section"), 1);
    auto clear = playButton("清空", "clearPlaybackQueue");
    queueHeading->addWidget(clear);
    queue->addLayout(queueHeading);
    m_playbackQueue = new PlaybackQueueView(m_library);
    m_playbackQueue->setObjectName("playbackQueue");
    m_playbackQueue->setMinimumHeight(90);
    m_playbackQueue->setContextMenuPolicy(Qt::CustomContextMenu);
    queue->addWidget(m_playbackQueue, 1);
    split->addWidget(right);
    split->setStretchFactor(0, 1);
    split->setStretchFactor(1, 0);
    split->setSizes({670, 480});
    layout->addWidget(split, 1);
    connect(m_browseSongs, &QPushButton::clicked, this, [this] {
        m_browsePlaylist = 0;
        m_browseFilter->clear();
        refreshPlaybackBrowser();
    });
    connect(m_browsePlaylists, &QPushButton::clicked, this, [this] {
        m_browsePlaylist = 0;
        m_browseFilter->clear();
        refreshPlaybackBrowser();
    });
    connect(m_browseBack, &QPushButton::clicked, this, [this] {
        m_browsePlaylist = 0;
        m_browseFilter->clear();
        refreshPlaybackBrowser();
    });
    connect(m_browseFilter, &QLineEdit::textChanged, this, &MainWindow::refreshPlaybackBrowser);
    connect(m_browsePlaylistCards, &QListWidget::itemClicked, this, [this](auto item) {
        m_browsePlaylist = item->data(Qt::UserRole).toLongLong();
        m_browseFilter->clear();
        refreshPlaybackBrowser();
    });
    connect(m_browseTracks, &QTreeWidget::itemDoubleClicked, this, [this] { playSelection(m_browseTracks); });
    connect(playAll, &QPushButton::clicked, this, [this] {
        if (m_browsePages->currentIndex() == 0)
            m_player.setQueue(trackIds(m_browseTracks));
        else if (auto item = m_browsePlaylistCards->currentItem())
            m_player.playPlaylist(item->data(Qt::UserRole).toLongLong());
        else
            notify("请先点击一个歌单，再播放其中的歌曲。");
    });
    connect(m_browseTracks, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        auto item = m_browseTracks->itemAt(pos);
        if (!item)
            return;
        if (!item->isSelected()) {
            m_browseTracks->clearSelection();
            item->setSelected(true);
        }
        const auto ids = m_browseTracks->selectedIds();
        QMenu menu(this);
        menu.addAction("立即播放", this, [this] { playSelection(m_browseTracks); });
        menu.addAction("加入播放队列", this, [this, ids] { m_player.enqueue(ids); });
        menu.exec(m_browseTracks->viewport()->mapToGlobal(pos));
    });
    connect(m_playbackQueue, &PlaybackQueueView::tracksDropped, &m_player, &Player::enqueue);
    connect(m_playbackQueue, &QTreeWidget::itemDoubleClicked, this,
            [this](auto item) { m_player.jumpToEntry(item->data(0, Qt::UserRole + 1).toULongLong()); });
    connect(m_playbackQueue, &QTreeWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        auto item = m_playbackQueue->itemAt(pos);
        if (!item)
            return;
        const auto key = item->data(0, Qt::UserRole + 1).toULongLong();
        QMenu menu(this);
        menu.addAction("立即播放", this, [this, key] { m_player.jumpToEntry(key); });
        menu.addAction("从播放队列移除", this, [this, key] { m_player.removeEntry(key); });
        menu.exec(m_playbackQueue->viewport()->mapToGlobal(pos));
    });
    connect(clear, &QPushButton::clicked, this, [this] { m_player.setQueue({}); });
    connect(&m_player, &Player::currentChanged, this, &MainWindow::refreshPlaybackBrowser);
    return page;
}

void MainWindow::refreshPlaybackBrowser() {
    const auto selection = m_browseTracks->selectedIds();
    const auto filter = m_browseFilter->text().trimmed();
    const bool playlists = m_browsePlaylists->isChecked();
    const auto allPlaylists = m_library->playlists();
    QString title;
    bool found = false;
    for (const auto &playlist : allPlaylists)
        if (playlist.value("id").toLongLong() == m_browsePlaylist) {
            title = playlist.value("name").toString();
            found = true;
        }
    if (!found)
        m_browsePlaylist = 0;
    const bool cards = playlists && !m_browsePlaylist;
    m_browsePages->setCurrentIndex(cards ? 1 : 0);
    m_browseBack->setVisible(playlists && m_browsePlaylist);
    m_browseFilter->setPlaceholderText(cards ? "筛选歌单名称" : "筛选歌曲名称");
    m_browseHeading->setText(cards ? "我的歌单" : (m_browsePlaylist ? title : "全部歌曲"));
    if (cards) {
        m_browsePlaylistCards->clear();
        for (const auto &playlist : allPlaylists) {
            if (!playlist.value("name").toString().contains(filter, Qt::CaseInsensitive))
                continue;
            const auto id = playlist.value("id").toLongLong();
            qint64 duration = 0;
            for (const auto &track : m_library->tracks(id))
                duration += track.value("duration").toLongLong();
            auto item = new QListWidgetItem(playlist.value("name").toString(), m_browsePlaylistCards);
            item->setData(Qt::UserRole, id);
            item->setData(Qt::UserRole + 1, playlist.value("count"));
            item->setData(Qt::UserRole + 2, duration);
            item->setToolTip(playlist.value("name").toString() + " · 单击查看歌曲，拖入右侧加入队列");
            item->setData(Qt::AccessibleTextRole, playlist.value("name").toString() + "歌单");
        }
        m_browseSummary->setText(QString("%1 个歌单 · 单击卡片查看歌曲").arg(m_browsePlaylistCards->count()));
        return;
    }
    m_browseTracks->clear();
    qint64 duration = 0;
    for (const auto &track : m_library->tracks(m_browsePlaylist)) {
        if (!track.value("title").toString().contains(filter, Qt::CaseInsensitive))
            continue;
        const auto id = track.value("id").toLongLong();
        auto item = new QTreeWidgetItem(
            m_browseTracks,
            {track.value("title").toString() + (track.value("storage").toString() == "url" ? "  [URL]" : ""),
             {},
             {},
             displayTime(track.value("duration").toLongLong()),
             {}});
        item->setData(0, Qt::UserRole, id);
        item->setToolTip(0, track.value("title").toString());
        if (selection.contains(id))
            item->setSelected(true);
        if (id == m_player.currentId())
            item->setForeground(0, UiTheme::forWidget(this).color(QPalette::Accent));
        auto add = playButton("→", QString("enqueueTrack_%1").arg(id));
        add->setToolTip("加入播放队列");
        add->setStyleSheet("padding:2px 5px;border:0;background:transparent;");
        m_browseTracks->setItemWidget(item, 4, add);
        connect(add, &QPushButton::clicked, this, [this, id] { m_player.enqueue({id}); });
        duration += track.value("duration").toLongLong();
    }
    m_browseSummary->setText(QString("%1 首 · %2 · 双击播放，或拖入右侧队列")
                                 .arg(m_browseTracks->topLevelItemCount())
                                 .arg(displayTime(duration)));
}

void MainWindow::refreshPlaybackQueue() {
    auto pending = m_player.pendingEntries();
    m_playbackQueue->clear();
    QList<Player::QueueEntry> visible;
    for (int i = pending.size() - 1; i >= 0; --i)
        visible << pending[i];
    for (const auto &entry : m_player.entries())
        if (entry.key == m_player.currentEntry())
            visible << entry;
    for (const auto &entry : visible) {
        const auto track = m_library->track(entry.track);
        const bool current = entry.key == m_player.currentEntry();
        const QString title =
            track.value("title").toString() + (track.value("storage").toString() == "url" ? "  [URL]" : "");
        auto item = new QTreeWidgetItem(m_playbackQueue, {title});
        item->setData(0, Qt::UserRole, entry.track);
        item->setData(0, Qt::UserRole + 1, entry.key);
        item->setData(0, Qt::AccessibleTextRole, (current ? "当前播放：" : "待播：") + title);
        item->setToolTip(0, (current ? "当前播放：" : "待播：") + title);
        item->setText(0, {});
        auto cell = new QWidget;
        cell->setObjectName("queueEntry");
        auto row = new QHBoxLayout(cell);
        row->setContentsMargins(4, 3, 4, 3);
        row->setSpacing(3);
        const QStringList names{"queueTop", "queueBottom", "queueUp", "queueDown"};
        const QStringList captions{"", "", "▴", "▾"};
        const QStringList tips{"置顶（最后播放）", "置底（下一首）", "上移", "下移"};
        const Player::QueueMove moves[]{Player::Top, Player::Bottom, Player::Up, Player::Down};
        for (int pair = 0; pair < 2; ++pair) {
            auto stack = new QVBoxLayout;
            stack->setSpacing(1);
            for (int j = 0; j < 2; ++j) {
                const int index = pair * 2 + j;
                auto button = playButton(captions[index], names[index] + "_" + QString::number(entry.key));
                button->setFixedSize(23, 21);
                if (index < 2) {
                    button->setIcon(queueActionIcon(index, devicePixelRatioF(),
                                                    UiTheme::forWidget(this).color(QPalette::ButtonText)));
                    button->setIconSize(QSize(18, 18));
                }
                button->setStyleSheet("padding:0;font-size:14px;border-radius:3px;");
                button->setToolTip(tips[index]);
                button->setEnabled(!current);
                stack->addWidget(button);
                connect(button, &QPushButton::clicked, this,
                        [this, key = entry.key, move = moves[index]] { m_player.moveEntry(key, move); });
            }
            row->addLayout(stack);
        }
        row->addWidget(new PlayingNote(&m_player, entry.key));
        auto name = playLabel(title, current ? "currentQueueSong" : "queueSong");
        name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        name->setMinimumWidth(0);
        name->setAttribute(Qt::WA_TransparentForMouseEvents);

        row->addWidget(name, 1);
        auto remove = playButton("", QString("dequeueEntry_%1").arg(entry.key));
        remove->setIcon(
            queueActionIcon(2, devicePixelRatioF(), UiTheme::forWidget(this).color(QPalette::ButtonText)));
        remove->setIconSize(QSize(18, 18));
        remove->setFixedSize(23, 21);
        remove->setStyleSheet("padding:0;font-size:14px;border-radius:3px;");
        remove->setToolTip("从播放队列移除这一项");
        row->addWidget(remove);
        connect(remove, &QPushButton::clicked, this, [this, key = entry.key] { m_player.removeEntry(key); });
        item->setSizeHint(0, QSize(0, 54));
        m_playbackQueue->setItemWidget(item, 0, cell);
    }
    m_playbackQueue->scrollToBottom();
}
