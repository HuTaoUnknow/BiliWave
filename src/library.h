#pragma once
#include <QHash>
#include <QObject>
#include <QPair>
#include <QSqlDatabase>
#include <QVariantMap>
#include <QSet>

using Track = QVariantMap;
// SQLite is used only on this object's thread. Missing file sizes are measured on a
// worker and written back here; fileSizesUpdated() is emitted after that write.
class Library : public QObject {
    Q_OBJECT
  public:
    explicit Library(const QString &directory, QObject *parent = nullptr);
    ~Library();
    bool isOpen() const;
    QString error() const {
        return m_error;
    }
    QString directory() const {
        return m_directory;
    }
    qint64 addTrack(const Track &track);
    qint64 addUrl(const QVariantMap &result, bool staged = false);
    bool commitTrack(qint64 id);
    QList<Track> stagedTracks() const;
    Track track(qint64 id) const;
    // Identity, path and labels only. Does not read or convert stored lyrics.
    Track trackHeader(qint64 id) const;
    QHash<qint64, Track> trackHeaders(const QList<qint64> &ids) const;
    QList<Track> tracks(qint64 playlist = 0, const QString &search = {}) const;
    QList<Track> trackSummaries(qint64 playlist = 0, const QString &search = {}) const;
    QSet<qint64> trackIds() const;
    QSet<qint64> assignedTrackIds() const;
    QList<QVariantMap> playlists() const;
    qint64 createPlaylist(const QString &name);
    bool renamePlaylist(qint64 id, const QString &name);
    bool deletePlaylist(qint64 id);
    bool addToPlaylist(qint64 playlist, const QList<qint64> &ids);
    bool removeFromPlaylist(qint64 playlist, const QList<qint64> &ids);
    bool reorder(qint64 playlist, const QList<qint64> &ids);
    bool deleteTrack(qint64 id);
    bool updateTrack(qint64 id, const QString &title, const QString &artist);
    bool setLyrics(qint64 id, const QString &lrc, const QString &source);
    void setAutoSimplifyLyrics(bool enabled);
    bool setOffset(qint64 id, int milliseconds);
    bool beginWorkbench(qint64 id);
    Track workbenchTrack(qint64 id) const;
    bool setWorkbenchMetadata(qint64 id, const QString &title, const QString &artist);
    bool setWorkbenchLyrics(qint64 id, const QString &lrc, const QString &source, int offset = 0);
    bool setWorkbenchOffset(qint64 id, int milliseconds);
    bool saveWorkbench(qint64 id, bool includeLyrics = true);
  signals:
    void changed();
    void workbenchChanged(qint64 id);
    void fileSizesUpdated();

  private:
    void startFileSizeScan();
    void applyMeasuredFileSizes(const QList<QPair<qint64, qint64>> &measured);
    Track lyricView(Track track) const;
    QList<Track> queryTracks(qint64 playlist, const QString &search, bool lyrics) const;
    bool m_autoSimplifyLyrics = true;
    QSqlDatabase m_db;
    QString m_directory, m_connection, m_error;
};
