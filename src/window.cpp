#include "window.h"
#include "lyricworkflow.h"
#include "theme.h"
#include <QApplication>
#include <QBoxLayout>
#include <QCheckBox>
#include <QCloseEvent>
#include <QColorDialog>
#include <QCursor>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDrag>
#include <QDropEvent>
#include <QEventLoop>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QMenu>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QRegularExpression>
#include <QSaveFile>
#include <QScreen>
#include <QShortcut>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStandardPaths>
#include <QStyle>
#include <QTabWidget>
#include <algorithm>
#include <memory>

QString displayTime(qint64 ms) {
    const auto s = qMax(qint64(0), ms) / 1000;
    return QString("%1:%2").arg(s / 60, 2, 10, QChar('0')).arg(s % 60, 2, 10, QChar('0'));
}
static QByteArray encodeIds(const QList<qint64> &ids) {
    QJsonArray array;
    for (auto id : ids)
        array << QString::number(id);
    return QJsonDocument(array).toJson(QJsonDocument::Compact);
}
BusySpinner::BusySpinner(QWidget *parent) : QWidget(parent) {
    setFixedSize(18, 18);
    m_timer.setInterval(75);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        m_angle = (m_angle + 25) % 360;
        update();
    });
    hide();
}
void BusySpinner::setActive(bool active) {
    setVisible(active);
    if (active)
        m_timer.start();
    else
        m_timer.stop();
}
void BusySpinner::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(UiTheme::forWidget(this).color(QPalette::Accent), 2, Qt::SolidLine, Qt::RoundCap));
    painter.drawArc(rect().adjusted(3, 3, -3, -3), -m_angle * 16, 250 * 16);
}
static QList<qint64> decodeIds(const QByteArray &data) {
    QList<qint64> ids;
    for (auto v : QJsonDocument::fromJson(data).array()) {
        const auto id = v.toString().toLongLong();
        if (id > 0 && !ids.contains(id))
            ids << id;
    }
    return ids;
}
QList<qint64> droppedTracks(const QMimeData *m, Library *l) {
    if (m->hasFormat(TrackMime))
        return decodeIds(m->data(TrackMime));
    QList<qint64> ids;
    if (m->hasFormat(PlaylistMime))
        for (const auto &t : l->tracks(m->data(PlaylistMime).toLongLong()))
            ids << t.value("id").toLongLong();
    return ids;
}
TrackView::TrackView(QWidget *p) : QTreeWidget(p) {
    setColumnCount(4);
    setHeaderLabels({tr("歌曲 / 标题"), tr("歌手"), tr("源音轨"), tr("时长")});
    setRootIsDecorated(false);
    setAlternatingRowColors(true);
    setSelectionMode(ExtendedSelection);
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(true);
    setContextMenuPolicy(Qt::CustomContextMenu);
    setUniformRowHeights(true);
    header()->setSectionResizeMode(0, QHeaderView::Stretch);
    header()->resizeSection(1, 130);
    header()->resizeSection(2, 240);
    header()->resizeSection(3, 70);
    setMinimumWidth(250);
}
QList<qint64> TrackView::selectedIds() const {
    QList<qint64> ids;
    for (auto item : selectedItems())
        ids << item->data(0, Qt::UserRole).toLongLong();
    return ids;
}
QPixmap musicDragPixmap(int, qreal ratio, const QColor &color) {
    QPixmap pixmap(QSize(44, 44) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter painter(&pixmap);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(QPen(color, 3, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    painter.setBrush(color);
    painter.drawLine(QPointF(16, 31), QPointF(16, 13));
    painter.drawLine(QPointF(16, 13), QPointF(33, 9));
    painter.drawLine(QPointF(33, 9), QPointF(33, 27));
    painter.drawEllipse(QRectF(7, 28, 9, 7));
    painter.drawEllipse(QRectF(24, 24, 9, 7));
    return pixmap;
}
namespace {
#ifdef Q_OS_WIN
// An application-local drag avoids the mandatory forbidden cursor of Windows OLE.
class MusicDrag : public QObject {
  public:
    MusicDrag(TrackView *source, QMimeData *mime, const QPixmap &pixmap)
        : m_source(source), m_mime(mime),
          m_badge(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                               Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus) {
        m_badge.setObjectName("musicDragIndicator");
        m_badge.setAttribute(Qt::WA_TranslucentBackground);
        m_badge.setAttribute(Qt::WA_TransparentForMouseEvents);
        m_badge.setAttribute(Qt::WA_ShowWithoutActivating);
        m_badge.setStyleSheet("background:transparent;");
        m_badge.setPixmap(pixmap);
        m_badge.resize(pixmap.deviceIndependentSize().toSize());
    }
    void exec() {
        m_badge.move(QCursor::pos() - QPoint(22, 22));
        QApplication::setOverrideCursor(Qt::BlankCursor);
        m_badge.show();
        m_source->viewport()->grabMouse(Qt::BlankCursor);
        qApp->installEventFilter(this);
        connect(qApp, &QCoreApplication::aboutToQuit, &m_loop, &QEventLoop::quit);
        m_loop.exec();
        qApp->removeEventFilter(this);
        if (m_source)
            m_source->viewport()->releaseMouse();
        m_badge.hide();
        QApplication::restoreOverrideCursor();
        if (!m_drop || !m_source)
            return;
        QAbstractItemView *target = nullptr;
        // Mouse capture and transparent tool windows can make native widgetAt()
        // return no widget. Resolve the application's visible drop view directly.
        for (auto view : m_source->window()->findChildren<QAbstractItemView *>()) {
            auto viewport = view->viewport();
            if (view->isVisible() && view->acceptDrops() &&
                QRect(viewport->mapToGlobal(QPoint()), viewport->size()).contains(m_position)) {
                target = view;
                break;
            }
        }
        if (!target)
            return;
        auto viewport = target->viewport();
        const QPoint point = viewport->mapFromGlobal(m_position);
        QDragEnterEvent enter(point, Qt::CopyAction | Qt::MoveAction, m_mime, Qt::LeftButton, m_modifiers);
        QApplication::sendEvent(viewport, &enter);
        if (!enter.isAccepted())
            return;
        QDropEvent drop(QPointF(point), Qt::CopyAction | Qt::MoveAction, m_mime, Qt::LeftButton, m_modifiers);
        QApplication::sendEvent(viewport, &drop);
    }

  protected:
    bool eventFilter(QObject *watched, QEvent *event) override {
        if (event->type() == QEvent::MouseMove) {
            auto mouse = static_cast<QMouseEvent *>(event);
            m_badge.move(mouse->globalPosition().toPoint() - QPoint(22, 22));
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            auto mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton) {
                m_position = mouse->globalPosition().toPoint();
                m_modifiers = mouse->modifiers();
                m_drop = true;
                m_loop.quit();
            }
            return true;
        }
        if ((event->type() == QEvent::KeyPress && static_cast<QKeyEvent *>(event)->key() == Qt::Key_Escape) ||
            event->type() == QEvent::ApplicationDeactivate ||
            (event->type() == QEvent::Close && m_source && watched == m_source->window())) {
            m_loop.quit();
            return event->type() == QEvent::KeyPress;
        }
        return false;
    }

  private:
    QPointer<TrackView> m_source;
    QMimeData *m_mime;
    QLabel m_badge;
    QEventLoop m_loop;
    QPoint m_position;
    Qt::KeyboardModifiers m_modifiers;
    bool m_drop = false;
};
#endif
bool ownTrackDrag(const TrackView *view, const QDropEvent *event) {
    return event->source() == view ||
           (view->playlist &&
            event->mimeData()->data("application/x-biliwave-origin").toLongLong() == view->playlist);
}
} // namespace
void TrackView::startDrag(Qt::DropActions) {
    auto ids = selectedIds();
    if (ids.isEmpty())
        return;
    auto mime = new QMimeData;
    mime->setData(TrackMime, encodeIds(ids));
    mime->setData("application/x-biliwave-origin", QByteArray::number(playlist));
    const auto pixmap =
        musicDragPixmap(ids.size(), devicePixelRatioF(), UiTheme::forWidget(this).color(QPalette::Accent));
#ifdef Q_OS_WIN
    MusicDrag drag(this, mime, pixmap);
    drag.exec();
    delete mime;
#else
    auto drag = new QDrag(this);
    drag->setMimeData(mime);
    drag->setPixmap(pixmap);
    drag->setHotSpot(QPoint(22, 22));
    drag->exec(Qt::CopyAction | Qt::MoveAction, Qt::CopyAction);
#endif
}
void TrackView::dragEnterEvent(QDragEnterEvent *e) {
    if (e->mimeData()->hasUrls() || (acceptsTrackCopy && e->mimeData()->hasFormat(TrackMime)) ||
        (playlist && !filtered && ownTrackDrag(this, e)))
        e->acceptProposedAction();
}
void TrackView::dragMoveEvent(QDragMoveEvent *e) {
    if (e->mimeData()->hasUrls() || (acceptsTrackCopy && e->mimeData()->hasFormat(TrackMime)) ||
        (playlist && !filtered && ownTrackDrag(this, e)))
        e->acceptProposedAction();
}
void TrackView::dropEvent(QDropEvent *e) {
    if (e->mimeData()->hasUrls()) {
        QStringList paths;
        for (const auto &u : e->mimeData()->urls())
            if (u.isLocalFile())
                paths << u.toLocalFile();
        emit filesDropped(paths);
        e->acceptProposedAction();
        return;
    }
    if (acceptsTrackCopy && playlist && !ownTrackDrag(this, e) && e->mimeData()->hasFormat(TrackMime)) {
        emit tracksAdded(decodeIds(e->mimeData()->data(TrackMime)));
        e->setDropAction(Qt::CopyAction);
        e->accept();
        return;
    }
    if (!playlist || filtered || !ownTrackDrag(this, e))
        return;
    const auto moving = decodeIds(e->mimeData()->data(TrackMime));
    auto target = itemAt(e->position().toPoint());
    const auto targetId = target ? target->data(0, Qt::UserRole).toLongLong() : 0;
    if (moving.contains(targetId))
        return;
    QList<qint64> order;
    for (int i = 0; i < topLevelItemCount(); ++i) {
        auto id = topLevelItem(i)->data(0, Qt::UserRole).toLongLong();
        if (id == targetId)
            order += moving;
        if (!moving.contains(id))
            order << id;
    }
    if (!targetId)
        order += moving;
    emit reordered(order);
    e->acceptProposedAction();
}
void PlaylistView::startDrag(Qt::DropActions) {
    auto item = currentItem();
    if (!item || !item->data(Qt::UserRole).toLongLong())
        return;
    auto mime = new QMimeData;
    mime->setData(PlaylistMime, QByteArray::number(item->data(Qt::UserRole).toLongLong()));
    auto drag = new QDrag(this);
    drag->setMimeData(mime);
    drag->exec(Qt::CopyAction);
}
void PlaylistView::dragEnterEvent(QDragEnterEvent *e) {
    if (e->mimeData()->hasFormat(TrackMime))
        e->acceptProposedAction();
}
void PlaylistView::dragMoveEvent(QDragMoveEvent *e) {
    auto item = itemAt(e->position().toPoint());
    if (item && item->data(Qt::UserRole).toLongLong() && e->mimeData()->hasFormat(TrackMime))
        e->acceptProposedAction();
    else
        e->ignore();
}
void PlaylistView::dropEvent(QDropEvent *e) {
    auto item = itemAt(e->position().toPoint());
    if (item && item->data(Qt::UserRole).toLongLong() && e->mimeData()->hasFormat(TrackMime)) {
        emit tracksDropped(item->data(Qt::UserRole).toLongLong(), decodeIds(e->mimeData()->data(TrackMime)));
        e->setDropAction(Qt::CopyAction);
        e->accept();
    }
}

LyricOverlay::LyricOverlay(QSettings *s)
    : QWidget(nullptr,
              Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus),
      m_settings(s) {
    setAttribute(Qt::WA_TranslucentBackground);
    resize(900, 160);
    setMinimumSize(500, 120);
    auto available = QGuiApplication::primaryScreen()->availableGeometry();
    move(s->value("windows/lyrics", QPoint(available.center().x() - width() / 2, available.bottom() - 230))
             .toPoint());
    if (!QGuiApplication::screenAt(frameGeometry().center()))
        move(available.center().x() - width() / 2, available.bottom() - 230);
    applySettings();
}
void LyricOverlay::setLines(const QString &current, const QString &next) {
    if (m_current == current && m_next == next)
        return;
    m_current = current;
    m_next = next;
    update();
}
void LyricOverlay::applySettings() {
    const bool visible = isVisible();
    setWindowFlag(Qt::WindowTransparentForInput, m_settings->value("lyrics/clickThrough", false).toBool());
    if (visible)
        show();
    const int x = m_settings->value("lyrics/horizontalOffset", 180).toInt();
    const int y = m_settings->value("lyrics/verticalOffset", 80).toInt();
    const int size = m_settings->value("lyrics/fontSize", 30).toInt();
    const int secondary = m_settings->value("lyrics/secondaryFontSize", qMax(12, int(size * .65))).toInt();
    resize(qMax(900, qAbs(x) + 720), qMax(160, y + qMax(size, secondary) * 2 + 25));
    update();
}
void LyricOverlay::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const double opacity = m_settings->value("lyrics/opacity", 100).toInt() / 100.;
    const auto color = QColor(m_settings->value("lyrics/color", "#a4f0d1").toString());
    auto draw = [&](const QString &text, QRect rect, int size, double alpha, const QColor &inkColor,
                    Qt::Alignment align) {
        if (text.isEmpty())
            return;
        QFont f("Microsoft YaHei UI", size, QFont::DemiBold);
        p.setFont(f);
        const auto fm = p.fontMetrics();
        auto clipped = fm.elidedText(text, Qt::ElideRight, rect.width());
        auto bounds = fm.boundingRect(rect, align, clipped);
        QPainterPath path;
        path.addText(bounds.left(), bounds.top() + fm.ascent(), f, clipped);
        // One flat color; transparency comes from the line opacity only.
        QImage layer(this->size() * devicePixelRatioF(), QImage::Format_ARGB32_Premultiplied);
        layer.setDevicePixelRatio(devicePixelRatioF());
        layer.fill(Qt::transparent);
        {
            QPainter ink(&layer);
            ink.setRenderHint(QPainter::Antialiasing);
            ink.setPen(Qt::NoPen);
            ink.setBrush(inkColor);
            ink.drawPath(path);
        }
        p.setOpacity(alpha);
        p.drawImage(QPointF(0, 0), layer);
    };
    const int size = m_settings->value("lyrics/fontSize", 30).toInt();
    const int dx = m_settings->value("lyrics/horizontalOffset", 180).toInt();
    const int dy = m_settings->value("lyrics/verticalOffset", 80).toInt();
    const int primaryX = 22 + qMax(0, -dx), secondaryX = primaryX + dx;
    const int secondarySize =
        m_settings->value("lyrics/secondaryFontSize", qMax(12, int(size * .65))).toInt();
    const double secondaryOpacity =
        m_settings->value("lyrics/secondaryOpacity", int(opacity * 50)).toInt() / 100.;
    const QColor secondaryColor(m_settings->value("lyrics/secondaryColor", color.name()).toString());
    draw(m_current, QRect(primaryX, 10, width() - primaryX - 22, size * 2 + 10), size, opacity, color,
         Qt::AlignLeft | Qt::AlignTop);
    draw(m_next, QRect(secondaryX, 10 + dy, width() - secondaryX - 22, secondarySize * 2 + 10), secondarySize,
         secondaryOpacity, secondaryColor, Qt::AlignLeft | Qt::AlignTop);
}
void LyricOverlay::mousePressEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton && !m_settings->value("lyrics/locked", false).toBool()) {
        m_moving = true;
        m_drag = e->globalPosition().toPoint() - pos();
    }
}
void LyricOverlay::mouseMoveEvent(QMouseEvent *e) {
    if (m_moving)
        move(e->globalPosition().toPoint() - m_drag);
}
void LyricOverlay::mouseReleaseEvent(QMouseEvent *) {
    m_moving = false;
    m_settings->setValue("windows/lyrics", pos());
}

MainWindow::~MainWindow() {
    delete m_float;
    delete m_overlay;
}
void MainWindow::notify(const QString &text) {
    m_status->setText(
        QFontMetrics(m_status->font()).elidedText(text, Qt::ElideRight, qMax(300, m_status->width())));
    m_status->setToolTip(text);
}
qint64 MainWindow::selectedTrack() const {
    auto ids = (m_contextTracks ? m_contextTracks : m_tracks)->selectedIds();
    return ids.isEmpty() ? 0 : ids.first();
}
void MainWindow::playSelection(TrackView *view) {
    if (!view)
        view = m_tracks;
    auto selected = view->selectedIds();
    auto id = selected.isEmpty() ? 0 : selected.first();
    if (!id)
        return;
    QList<qint64> ids;
    for (int i = 0; i < view->topLevelItemCount(); ++i)
        ids << view->topLevelItem(i)->data(0, Qt::UserRole).toLongLong();
    m_player.setQueue(ids, id);
}
void MainWindow::createPlaylist() {
    bool ok = false;
    auto name = QInputDialog::getText(this, tr("新建歌单"), tr("歌单名称"), QLineEdit::Normal, {}, &ok);
    if (ok && !name.trimmed().isEmpty()) {
        auto id = m_library->createPlaylist(name);
        if (id) {
            m_playlist = id;
            refresh();
        }
    }
}
void MainWindow::playlistMenu(const QPoint &position) {
    auto item = m_playlists->itemAt(position);
    QMenu menu(this);
    menu.addAction(tr("新建歌单"), this, &MainWindow::createPlaylist);
    if (item || m_playlist) {
        auto id = m_playlist ? m_playlist : item->data(Qt::UserRole).toLongLong();
        if (id) {
            menu.addAction(tr("重命名"), this, [this, id] {
                bool ok;
                QString oldName;
                for (const auto &p : m_library->playlists())
                    if (p.value("id").toLongLong() == id)
                        oldName = p.value("name").toString();
                auto name = QInputDialog::getText(this, tr("重命名歌单"), tr("名称"), QLineEdit::Normal,
                                                  oldName, &ok);
                if (ok)
                    m_library->renamePlaylist(id, name);
            });
            menu.addAction(tr("删除歌单"), this, [this, id] {
                if (QMessageBox::question(this, tr("删除歌单"),
                                          tr("删除此歌单？歌曲仍保留在歌曲库和其他歌单中。")) ==
                    QMessageBox::Yes)
                    m_library->deletePlaylist(id);
            });
        }
    }
    menu.exec(m_playlist ? m_playlistHeading->mapToGlobal(position)
                         : m_playlists->viewport()->mapToGlobal(position));
}
namespace {
QString fileIdentity(const QString &path) {
    const QFileInfo file(path);
    const auto canonical = file.canonicalFilePath();
    return QDir::cleanPath(canonical.isEmpty() ? file.absoluteFilePath() : canonical).toCaseFolded();
}
QSet<QString> relatedSongFiles(const Track &track, bool generatedFamily = true) {
    QSet<QString> files;
    if (track.value("storage").toString() == "url")
        return files;
    const auto add = [&files](const QString &path) {
        const QFileInfo file(path);
        if (file.isFile() || file.isSymLink())
            files.insert(file.absoluteFilePath());
    };
    static const QRegularExpression owned(
        "^([0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})\\.");
    for (const auto &key : {"path", "original"}) {
        const auto path = track.value(key).toString();
        if (path.isEmpty())
            continue;
        const QFileInfo file(path);
        add(path);
        if (file.isSymLink())
            continue;
        const auto stem = file.dir().filePath(file.completeBaseName());
        for (const auto &suffix :
             {".lrc", ".srt", ".vtt", ".ass", ".lyrics.txt", ".jpg", ".jpeg", ".png", ".webp", ".json"})
            add(stem + suffix);
        const auto match = owned.match(file.fileName());
        if (generatedFamily && match.hasMatch()) {
            const auto root = file.dir().canonicalPath();
            for (const auto &sibling :
                 file.dir().entryInfoList(QDir::Files | QDir::Hidden | QDir::NoSymLinks))
                if (sibling.fileName().startsWith(match.captured(1) + ".") &&
                    sibling.dir().canonicalPath() == root)
                    files.insert(sibling.absoluteFilePath());
        }
    }
    return files;
}
} // namespace
void MainWindow::trackMenu(const QPoint &position) {
    auto view = m_contextTracks ? m_contextTracks : m_tracks;
    const auto id = selectedTrack();
    if (!id)
        return;
    const auto ids = view->selectedIds();
    auto t = m_library->track(id);
    QMenu menu(this);
    auto add = menu.addMenu(tr("加入歌单"));
    for (const auto &p : m_library->playlists())
        add->addAction(p.value("name").toString(), this,
                       [this, p, ids] { m_library->addToPlaylist(p.value("id").toLongLong(), ids); });
    add->addAction(tr("新建歌单…"), this, [this, ids] {
        createPlaylist();
        if (m_playlist)
            m_library->addToPlaylist(m_playlist, ids);
    });
    menu.addAction(tr("重命名歌曲"), this, [this, id, t] {
        bool ok;
        auto title = QInputDialog::getText(this, tr("重命名歌曲"), tr("歌曲名称"), QLineEdit::Normal,
                                           t.value("title").toString(), &ok);
        if (ok)
            m_library->updateTrack(id, title, t.value("artist").toString());
    });
    menu.addAction(tr("编辑歌名 / 歌手"), this, [this, id, t] {
        QDialog dialog(this);
        dialog.setWindowTitle(tr("编辑歌曲信息"));
        auto form = new QFormLayout(&dialog);
        auto title = new QLineEdit(t.value("title").toString()),
             artist = new QLineEdit(t.value("artist").toString());
        form->addRow(tr("歌曲名称"), title);
        form->addRow(tr("歌手（不是视频上传者）"), artist);
        auto buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        form->addRow(buttons);
        connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        if (dialog.exec() == QDialog::Accepted)
            m_library->updateTrack(id, title->text(), artist->text());
    });
    menu.addSeparator();
    if (lyricFeaturesEnabled()) {
        {
            auto workbench = menu.addAction(tr("载入工作台"), this, [this, id] { loadLibraryTrack(id); });
            workbench->setObjectName("loadTrackToWorkbench");
            workbench->setEnabled(!m_workbenchTracks.contains(id));
        }
        auto exportSong = menu.addAction(tr("导出歌曲及歌词…"), this, [this, id, t] {
            auto name = t.value("title").toString();
            name.replace(QRegularExpression(R"([\\/:*?"<>|])"), "_");
            const bool local = t.value("storage").toString() != "url";
            const auto ext = QFileInfo(t.value("path").toString()).suffix();
            if (local)
                name += "." + ext;
            const auto path = QFileDialog::getSaveFileName(
                this, tr("导出歌曲及同名歌词"),
                QDir(QStandardPaths::writableLocation(QStandardPaths::MusicLocation)).filePath(name),
                local ? QString("音频 (*.%1)").arg(ext) : tr("音频（保留原格式） (*)"));
            if (!path.isEmpty())
                m_exports.exportTrack(id, path);
        });
        exportSong->setObjectName("exportSongWithLyrics");
        if (recognitionFeaturesEnabled())
            menu.addAction(tr("导入 LRC"), this, [this, id] {
                auto path = QFileDialog::getOpenFileName(this, tr("导入歌词"), {}, "LRC (*.lrc)");
                if (!path.isEmpty())
                    m_lyrics.importFile(id, path);
            });
        if (recognitionFeaturesEnabled())
            menu.addAction(tr("编辑 / 导出时间轴歌词"), this, [this, id, t] {
                QDialog dialog(this);
                dialog.setWindowTitle(tr("时间轴歌词 · UTF-8 LRC"));
                dialog.resize(620, 480);
                auto layout = new QVBoxLayout(&dialog);
                auto editor = new QPlainTextEdit(t.value("lrc").toString());
                layout->addWidget(editor);
                auto buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
                auto exportButton = buttons->addButton(tr("导出 LRC"), QDialogButtonBox::ActionRole);
                layout->addWidget(buttons);
                connect(exportButton, &QPushButton::clicked, this, [this, editor] {
                    auto path =
                        QFileDialog::getSaveFileName(this, tr("导出歌词"), "lyrics.lrc", "LRC (*.lrc)");
                    if (path.isEmpty())
                        return;
                    QSaveFile file(path);
                    if (file.open(QIODevice::WriteOnly)) {
                        file.write(editor->toPlainText().toUtf8());
                        file.commit();
                    }
                });
                connect(buttons, &QDialogButtonBox::accepted, &dialog, [this, &dialog, editor, id] {
                    if (!editor->toPlainText().isEmpty() && Lrc::parse(editor->toPlainText()).isEmpty()) {
                        QMessageBox::information(&dialog, tr("缺少时间轴"),
                                                 tr("请输入带 [mm:ss.xx] 时间标签的歌词"));
                        return;
                    }
                    m_library->setLyrics(id, editor->toPlainText(), "手动编辑");
                    dialog.accept();
                });
                connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
                dialog.exec();
            });
    }
    menu.addSeparator();
    if (t.value("storage").toString() != "url")
        menu.addAction(tr("打开文件所在目录"), this, [t] {
            QDesktopServices::openUrl(
                QUrl::fromLocalFile(QFileInfo(t.value("path").toString()).absolutePath()));
        });
    if (!t.value("source").toString().isEmpty())
        menu.addAction(tr("打开原视频"), this,
                       [t] { QDesktopServices::openUrl(QUrl(t.value("source").toString())); });
    if (m_playlist && view == m_playlistTracks)
        menu.addAction(tr("仅从当前歌单移除"), this,
                       [this, ids] { m_library->removeFromPlaylist(m_playlist, ids); });
    menu.addAction(tr("从歌曲库移除（保留文件）"), this, [this, ids] {
        if (QMessageBox::question(
                this, tr("移除歌曲"),
                tr("从歌曲库及所有歌单中移除 %1 首歌曲？磁盘文件会保留。").arg(ids.size())) ==
            QMessageBox::Yes)
            for (auto selected : ids)
                m_library->deleteTrack(selected);
    });
    auto deleteFiles = menu.addAction(tr("删除歌曲及相关文件…"), this, [this, ids] {
        const QSet<qint64> selected(ids.begin(), ids.end());
        QSet<QString> protectedPaths;
        auto all = m_library->tracks();
        all.append(m_library->stagedTracks());
        for (const auto &track : all)
            if (!selected.contains(track.value("id").toLongLong()))
                for (const auto &path : relatedSongFiles(track, false))
                    protectedPaths.insert(fileIdentity(path));
        QMap<qint64, QSet<QString>> filesByTrack;
        QSet<QString> files, shared;
        for (auto id : ids) {
            auto paths = relatedSongFiles(m_library->track(id));
            for (auto it = paths.begin(); it != paths.end();) {
                if (protectedPaths.contains(fileIdentity(*it))) {
                    shared.insert(*it);
                    it = paths.erase(it);
                } else
                    ++it;
            }
            filesByTrack[id] = paths;
            files.unite(paths);
        }
        auto list = files.values();
        list.sort(Qt::CaseInsensitive);
        QMessageBox confirmation(
            QMessageBox::Warning, tr("删除歌曲及相关文件"),
            tr("删除选中 %1 首歌曲及 %2 个相关文件，并清除歌词、歌单收录与关联任务？此操作无法撤销。")
                .arg(ids.size())
                .arg(files.size()),
            QMessageBox::Yes | QMessageBox::No, this);
        confirmation.setObjectName("deleteSongFilesConfirmation");
        confirmation.setDefaultButton(QMessageBox::No);
        QString detail = list.isEmpty() ? tr("没有找到可删除的本地文件，仅清除歌曲记录。") : list.join('\n');
        if (!shared.isEmpty())
            detail += tr("\n\n以下文件仍被其他歌曲使用，将保留：\n") + shared.values().join('\n');
        confirmation.setDetailedText(detail);
        if (confirmation.exec() != QMessageBox::Yes)
            return;
        for (auto id : ids) {
            if (m_player.currentId() == id) {
                m_player.media()->stop();
                m_player.media()->setSource({});
            }
            if (lyricFeaturesEnabled()) {
                m_lyricQueue->cancelTrack(id);
                if (m_lyricTrack == id)
                    m_lyrics.cancel();
            }
            if (m_pendingCommit == id)
                m_pendingCommit = 0;
        }
        auto removeFiles = [](const QSet<QString> &paths) {
            QSet<QString> failed;
            for (const auto &path : paths)
                if ((QFileInfo::exists(path) || QFileInfo(path).isSymLink()) && !QFile::remove(path))
                    failed.insert(path);
            return failed;
        };
        auto finish = [this, ids, filesByTrack](const QSet<QString> &failed) {
            QStringList failures = failed.values();
            for (auto id : ids) {
                if (filesByTrack.value(id).intersects(failed))
                    continue;
                m_workbenchTracks.remove(id);
                m_candidateCache.remove(id);
                m_candidateQueries.remove(id);
                if (!m_library->deleteTrack(id))
                    failures << tr("歌曲记录 %1 删除失败").arg(id);
            }
            notify(failures.isEmpty() ? tr("歌曲及相关保存已删除")
                                      : tr("部分内容删除失败，相关歌曲记录已保留：") + failures.join("；"));
        };
        const auto failed = removeFiles(files);
        if (failed.isEmpty())
            finish({});
        else
            QTimer::singleShot(180, this, [failed, removeFiles, finish] { finish(removeFiles(failed)); });
    });
    deleteFiles->setObjectName("deleteSongFiles");
    menu.exec(view->viewport()->mapToGlobal(position));
}
void MainWindow::updateCurrent() {
    const auto t = m_previewTrack && m_previewTrack == m_player.currentId()
                       ? m_library->workbenchTrack(m_previewTrack)
                       : m_library->track(m_player.currentId());
    if (t.isEmpty()) {
        m_lines.clear();
        m_current->setText(tr("准备好，听一首喜欢的歌"));
        updateLyrics();
        return;
    }
    m_current->setText(
        QFontMetrics(m_current->font())
            .elidedText(t.value("title").toString() + (t.value("artist").toString().isEmpty()
                                                           ? ""
                                                           : "  /  " + t.value("artist").toString()),
                        Qt::ElideRight, qMax(300, m_current->width())));
    auto fingerprint = t.value("lrc").toString() + QString::number(t.value("lyric_offset").toInt());
    if (fingerprint != m_lyricFingerprint) {
        m_lyricFingerprint = fingerprint;
        m_lines = Lrc::parse(t.value("lrc").toString());
        m_bilingualLyrics = std::any_of(m_lines.cbegin(), m_lines.cend(), [](const LyricLine &line) {
            return !Lrc::bilingualLines(line.text).first.isEmpty();
        });
    }
    m_float->refresh();
    updateLyrics();
}
void MainWindow::updateLyrics() {
    if (!lyricFeaturesEnabled())
        return;
    const auto t = m_previewTrack && m_previewTrack == m_player.currentId()
                       ? m_library->workbenchTrack(m_previewTrack)
                       : m_library->track(m_player.currentId());
    const int index = Lrc::indexAt(m_lines, m_player.media()->position() + t.value("lyric_offset").toInt());
    QString current, next;
    if (m_lines.isEmpty()) {
        current = t.value("title").toString();
        next = t.isEmpty() ? tr("拖入歌曲或歌单，开始聆听") : tr("尚无时间轴歌词 · 可匹配或导入 LRC");
    } else {
        if (index >= 0)
            current = m_lines[index].text;
        if (index + 1 < m_lines.size())
            next = m_lines[index + 1].text;
        const auto pair = Lrc::bilingualLines(current);
        if (m_bilingualLyrics) {
            next.clear();
            if (!pair.first.isEmpty()) {
                current = pair.first;
                next = pair.second;
            }
        }
    }
    m_currentLyric->setText(QFontMetrics(m_currentLyric->font())
                                .elidedText(current, Qt::ElideRight, qMax(300, m_currentLyric->width())));
    m_nextLyric->setText(
        QFontMetrics(m_nextLyric->font()).elidedText(next, Qt::ElideRight, qMax(300, m_nextLyric->width())));
    m_overlay->setLines(current, next);
}
void MainWindow::settingsDialog() {
    navigate(4);
}
bool MainWindow::eventFilter(QObject *watched, QEvent *event) {
    if (m_pages->currentIndex() == 1 && (watched == m_tracks || watched == m_playlistTracks) &&
        (event->type() == QEvent::ShortcutOverride || event->type() == QEvent::KeyPress)) {
        auto key = static_cast<QKeyEvent *>(event);
        QPushButton *target = nullptr;
        if (key->modifiers() == Qt::ControlModifier && key->key() == Qt::Key_A)
            target = m_addToPlaylist;
        if (key->modifiers() == Qt::ControlModifier && key->key() == Qt::Key_D)
            target = m_removeFromPlaylist;
        if (key->modifiers() == Qt::NoModifier && key->key() == Qt::Key_Escape)
            target = m_back;
        if (target) {
            key->accept();
            if (event->type() == QEvent::KeyPress && target->isEnabled() && target->isVisible())
                target->click();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}
void MainWindow::showMainWindow() {
    if (isMinimized())
        setWindowState(windowState() & ~Qt::WindowMinimized);
    show();
    raise();
    activateWindow();
}
bool MainWindow::hideToTray() {
    if (!m_tray || !m_tray->isVisible() || !QSystemTrayIcon::isSystemTrayAvailable())
        return false;
    saveSession();
    hide();
    return true;
}
void MainWindow::exitApplication() {
    m_quitting = true;
    close();
}
void MainWindow::saveSession() {
    m_settings->setValue("player/mode", m_player.mode());
    m_settings->setValue("player/volume", qRound(m_player.volume() * 100));
    m_settings->setValue("windows/main", saveGeometry());
    m_settings->setValue("windows/lyrics", m_overlay->pos());
    m_settings->setValue("player/current", m_player.currentId());
    m_settings->setValue("player/currentQueueIndex", m_player.currentQueueIndex());
    m_settings->setValue("player/position", m_player.media()->position());
    QVariantList queue;
    for (auto id : m_player.queue())
        queue << id;
    m_settings->setValue("player/queue", queue);
    m_settings->sync();
}
void MainWindow::closeEvent(QCloseEvent *event) {
    if (!m_quitting && !qGuiApp->isSavingSession() &&
        m_settings->value("application/closeToTray", true).toBool() && hideToTray()) {
        event->ignore();
        return;
    }
    m_quitting = true;
    saveSession();
    m_ducking.stop();
    m_stream.cancel();
    m_player.cancelStream();
    m_lookup.cancel();
    m_import.cancel();
    m_lyrics.cancel();
    m_float->hide();
    m_overlay->hide();
    m_tray->hide();
    event->accept();
    QApplication::quit();
}
