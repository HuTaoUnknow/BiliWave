#pragma once
#include "autoducking.h"
#include "features.h"
#include "lrc.h"
#include "player.h"
#include "searchservice.h"
#include "services.h"
#include "streamservice.h"
#include <QCheckBox>
#include <QComboBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMainWindow>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QStackedWidget>
#include <QSystemTrayIcon>
#include <QTreeWidget>
class QScreen;
class LyricQueue;

constexpr auto TrackMime = "application/x-biliwave-tracks";
constexpr auto PlaylistMime = "application/x-biliwave-playlist";
QList<qint64> droppedTracks(const QMimeData *mime, Library *library);
QString displayTime(qint64 ms);
QPixmap musicDragPixmap(int count = 1, qreal devicePixelRatio = 1., const QColor &color = QColor("#287eff"));
class PlaylistView;
void configureRecordCards(PlaylistView *view);
class BusySpinner : public QWidget {
    Q_OBJECT
  public:
    explicit BusySpinner(QWidget *parent = nullptr);
    void setActive(bool active);
    bool active() const {
        return m_timer.isActive();
    }

  protected:
    void paintEvent(QPaintEvent *) override;

  private:
    QTimer m_timer;
    int m_angle = 0;
};

class TrackView : public QTreeWidget {
    Q_OBJECT
  public:
    explicit TrackView(QWidget *parent = nullptr);
    qint64 playlist = 0;
    bool filtered = false;
    bool acceptsTrackCopy = false;
    QList<qint64> selectedIds() const;
  signals:
    void filesDropped(const QStringList &paths);
    void reordered(const QList<qint64> &ids);
    void tracksAdded(const QList<qint64> &ids);

  protected:
    void startDrag(Qt::DropActions) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dragMoveEvent(QDragMoveEvent *) override;
    void dropEvent(QDropEvent *) override;
};
class PlaylistView : public QListWidget {
    Q_OBJECT
  public:
    using QListWidget::QListWidget;
  signals:
    void tracksDropped(qint64 playlist, const QList<qint64> &tracks);

  protected:
    void startDrag(Qt::DropActions) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dragMoveEvent(QDragMoveEvent *) override;
    void dropEvent(QDropEvent *) override;
};
class PlaybackQueueView : public QTreeWidget {
    Q_OBJECT
  public:
    explicit PlaybackQueueView(Library *library, QWidget *parent = nullptr);
  signals:
    void tracksDropped(const QList<qint64> &ids);

  protected:
    void dragEnterEvent(QDragEnterEvent *) override;
    void dragMoveEvent(QDragMoveEvent *) override;
    void dropEvent(QDropEvent *) override;

  private:
    Library *m_library;
};
class FloatBar : public QWidget {
    Q_OBJECT
  public:
    FloatBar(Player *player, QSettings *settings);
    void refresh();
    void applySettings();

  protected:
    void paintEvent(QPaintEvent *) override;
    void showEvent(QShowEvent *) override;
    bool event(QEvent *) override;

  private:
    void anchorToPrimaryScreen();
    Player *m_player;
    QSettings *m_settings;
    QPointer<QScreen> m_screen;
    QColor m_leftColor, m_rightColor;
    QColor m_queueMarkerColor;
    int m_queueMarkerLength = 10, m_queueMarkerLimit = 10;
    int m_fadeHeight = 24, m_gradientWidth = 80;
};
class LyricOverlay : public QWidget {
    Q_OBJECT
  public:
    explicit LyricOverlay(QSettings *settings);
    void setLines(const QString &current, const QString &next);
    void applySettings();

  protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;

  private:
    QSettings *m_settings;
    QString m_current, m_next;
    QPoint m_drag;
    bool m_moving = false;
};
class MainWindow : public QMainWindow {
    Q_OBJECT
  public:
    MainWindow(Library *library, QSettings *settings, QWidget *parent = nullptr);
    ~MainWindow();
    Player *player() {
        return &m_player;
    }
    ImportService *imports() {
        return &m_import;
    }
    LyricService *lyrics() {
        return &m_lyrics;
    }
    AudioExportService *exports() {
        return &m_exports;
    }
    FloatBar *floatBar() {
        return m_float;
    }
    LyricOverlay *overlay() {
        return m_overlay;
    }
    void refresh();
    void showMainWindow();
    bool hideToTray();
    void exitApplication();
    void navigate(int page);
    void showSearchResults(const QList<QVariantMap> &items, int page, bool hasNext, bool video);
    void loadLibraryTrack(qint64 id);

  protected:
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

  private:
    void playSelection(TrackView *view = nullptr);
    void createPlaylist();
    void trackMenu(const QPoint &position);
    void playlistMenu(const QPoint &position);
    void settingsDialog();
    void applyTheme(const QString &name);
    QWidget *createSettingsPage();
    QWidget *createSearchPage();
    QWidget *createWorkbenchPage();
    QWidget *createPlaybackPage();
    QWidget *createLibraryPage();
    void refreshPlaybackBrowser();
    void refreshPlaybackQueue();
    QWidget *createLyricWorkbench();
    void beginSearch(int page = 1);
    void downloadResults(bool all = false);
    void collectUrls();
    void completeDownload(const QString &taskId, qint64 trackId);
    void updateDownloadTask(const QString &taskId, const QString &title, const QString &state,
                            qint64 trackId = 0);
    void openDownload(QListWidgetItem *item);
    void discardDownload(QListWidgetItem *item);
    void commitLyricTrack(bool skipLyrics);
    void saveWorkbenchTrack();
    void selectLyricTrack(qint64 id, bool match = false);
    void refreshLyricLibrary();
    void refreshLyricWorkbench(bool force = false);
    void applyLyricSource();
    void matchLyrics(bool offline = false);
    void openLyricWizard(const QString &text = {}, int method = -1);
    void playLyricPreview();
    void syncLyricPreview();
    void updateCurrent();
    void updateLyrics();
    void chooseLyrics(qint64 id, const QList<QVariantMap> &items);
    void editLyricLine(int row, int operation);
    void saveLyricLines(const QList<LyricLine> &lines);
    qint64 selectedTrack() const;
    void notify(const QString &text);
    void saveSession();
    Library *m_library;
    QSettings *m_settings;
    Player m_player;
    AutoDucking m_ducking;
    ImportService m_import;
    LyricService m_lyrics;
    AudioExportService m_exports;
    SearchService m_lookup;
    StreamService m_stream;
    QNetworkAccessManager m_thumbnails;
    FloatBar *m_float;
    LyricOverlay *m_overlay;
    PlaylistView *m_playlists;
    TrackView *m_tracks;
    TrackView *m_playlistTracks;
    TrackView *m_contextTracks = nullptr;
    QStackedWidget *m_pages, *m_playlistPages;
    QStackedWidget *m_headerTools;
    QPushButton *m_navSearch, *m_navLibrary, *m_navPlayback, *m_navWorkbench, *m_navSettings, *m_back,
        *m_addToPlaylist, *m_removeFromPlaylist;
    QPushButton *m_browseSongs, *m_browsePlaylists, *m_browseBack;
    QStackedWidget *m_browsePages;
    TrackView *m_browseTracks;
    PlaylistView *m_browsePlaylistCards;
    PlaybackQueueView *m_playbackQueue;
    QLineEdit *m_browseFilter;
    QLabel *m_browseHeading, *m_browseSummary, *m_queueSummary, *m_queueEmpty;
    qint64 m_browsePlaylist = 0;
    QLabel *m_pageTitle, *m_pageSubtitle, *m_playlistHeading, *m_playlistSummary;
    QTreeWidget *m_results, *m_candidates;
    QPushButton *m_searchButton, *m_previousPage, *m_nextPage;
    QLabel *m_resultStatus, *m_pageNumber, *m_lyricStatus;
    BusySpinner *m_lyricSpinner;
    QPushButton *m_sortButton, *m_sortDirection;
    QPushButton *m_commitWithLyrics, *m_commitWithoutLyrics, *m_saveWorkbench;
    int m_sortMode = 0;
    bool m_sortAscending = true;
    QString m_libraryNeedle;
    qint64 m_pendingCommit = 0;
    QSet<qint64> m_workbenchTracks;
    QLabel *m_importStatus;
    QComboBox *m_lyricTracks, *m_lyricSource;
    QComboBox *m_localLyricMethod;
    LyricQueue *m_lyricQueue;
    QLineEdit *m_matchTitle, *m_matchArtist, *m_lyricLookup;
    QSpinBox *m_matchOffset;
    QListWidget *m_lyricPreview;
    QPlainTextEdit *m_candidatePreview;
    QCheckBox *m_matchAfterDownload;
    QMap<qint64, QList<QVariantMap>> m_candidateCache;
    QMap<qint64, QString> m_candidateQueries;
    qint64 m_lyricTrack = 0, m_candidateTrack = 0;
    QString m_workspaceFingerprint, m_searchQuery;
    QList<LyricLine> m_workspaceLines;
    int m_resultPage = 1;
    quint64 m_thumbnailGeneration = 0;
    bool m_resultHasNext = false, m_resultIsVideo = false;
    QListWidget *m_jobs;
    QLineEdit *m_search, *m_input;
    QLabel *m_heading, *m_summary, *m_current, *m_currentLyric, *m_nextLyric, *m_status, *m_time;
    QSlider *m_progress;
    QPushButton *m_toggle, *m_floatButton, *m_lyricButton;
    QComboBox *m_mode;
    QSystemTrayIcon *m_tray = nullptr;
    bool m_quitting = false;
    QList<LyricLine> m_lines;
    bool m_bilingualLyrics = false;
    QMap<QString, QListWidgetItem *> m_jobItems;
    qint64 m_playlist = 0;
    QString m_lyricFingerprint;
    bool m_refreshing = false;
};
