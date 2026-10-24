#pragma once
#include <QObject>
#include <QSqlDatabase>
#include <QVariantMap>

using Track = QVariantMap;
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
    QList<Track> tracks(qint64 playlist = 0, const QString &search = {}) const;
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
    bool setOffset(qint64 id, int milliseconds);
  signals:
    void changed();

  private:
    QSqlDatabase m_db;
    QString m_directory, m_connection, m_error;
};
