#include "library.h"
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>
#include <QSqlRecord>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>

static QVariantMap row(QSqlQuery &q) {
    QVariantMap result;
    const auto rec = q.record();
    for (int i = 0; i < rec.count(); ++i)
        result.insert(rec.fieldName(i), q.value(i));
    return result;
}
Library::Library(const QString &directory, QObject *parent) : QObject(parent), m_directory(directory) {
    QDir().mkpath(directory);
    m_connection = QUuid::createUuid().toString();
    m_db = QSqlDatabase::addDatabase("QSQLITE", m_connection);
    m_db.setDatabaseName(QDir(directory).filePath("library.sqlite"));
    if (!m_db.open()) {
        m_error = m_db.lastError().text();
        return;
    }
    QSqlQuery q(m_db);
    const QStringList schema = {
        "PRAGMA foreign_keys=ON",
        "PRAGMA journal_mode=WAL",
        "PRAGMA busy_timeout=5000",
        "CREATE TABLE IF NOT EXISTS tracks (id INTEGER PRIMARY KEY, title TEXT NOT NULL, artist TEXT DEFAULT "
        "'', path TEXT NOT NULL UNIQUE, original TEXT DEFAULT '', source TEXT DEFAULT '', codec TEXT DEFAULT "
        "'', bitrate INTEGER DEFAULT 0, sample_rate INTEGER DEFAULT 0, channels INTEGER DEFAULT 0, duration "
        "INTEGER DEFAULT 0, lrc TEXT DEFAULT '', lyric_source TEXT DEFAULT '', lyric_offset INTEGER DEFAULT "
        "0, created TEXT DEFAULT CURRENT_TIMESTAMP)",
        "CREATE TABLE IF NOT EXISTS playlists (id INTEGER PRIMARY KEY, name TEXT NOT NULL)",
        "CREATE TABLE IF NOT EXISTS playlist_tracks (playlist_id INTEGER REFERENCES playlists(id) ON DELETE "
        "CASCADE, track_id INTEGER REFERENCES tracks(id) ON DELETE CASCADE, position INTEGER NOT NULL, "
        "PRIMARY KEY(playlist_id,track_id))"};
    for (const auto &sql : schema)
        if (!q.exec(sql)) {
            m_error = q.lastError().text();
            m_db.close();
            return;
        }
    const auto columns = m_db.record("tracks");
    for (const auto &column : QStringList{"storage", "staged", "filesize"}) {
        if (!columns.contains(column)) {
            const QString type =
                column == "storage" ? "TEXT NOT NULL DEFAULT 'local'" : "INTEGER NOT NULL DEFAULT 0";
            if (!q.exec("ALTER TABLE tracks ADD COLUMN " + column + " " + type)) {
                m_error = q.lastError().text();
                m_db.close();
                return;
            }
        }
    }
    QSqlQuery sizes(m_db);
    sizes.exec("SELECT id,path FROM tracks WHERE storage='local' AND filesize=0");
    while (sizes.next()) {
        QSqlQuery update(m_db);
        update.prepare("UPDATE tracks SET filesize=? WHERE id=?");
        update.addBindValue(QFileInfo(sizes.value(1).toString()).size());
        update.addBindValue(sizes.value(0));
        update.exec();
    }
}
Library::~Library() {
    m_db.close();
    m_db = QSqlDatabase();
    QSqlDatabase::removeDatabase(m_connection);
}
bool Library::isOpen() const {
    return m_db.isOpen();
}
qint64 Library::addTrack(const Track &t) {
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO "
              "tracks(title,artist,path,original,source,codec,bitrate,sample_rate,channels,duration,storage,"
              "staged,filesize) "
              "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(path) DO NOTHING");
    const QStringList keys = {"title",   "artist",  "path",        "original", "source",
                              "codec",   "bitrate", "sample_rate", "channels", "duration",
                              "storage", "staged",  "filesize"};
    for (const auto &key : keys)
        q.addBindValue(t.value(key, key == "storage" ? QVariant("local")
                                    : key == "filesize"
                                        ? QVariant(QFileInfo(t.value("path").toString()).size())
                                    : key == "staged" || key == "bitrate" || key == "sample_rate" ||
                                            key == "channels" || key == "duration"
                                        ? QVariant(0)
                                        : QVariant(QStringLiteral(""))));
    if (!q.exec())
        return 0;
    q.prepare("SELECT id FROM tracks WHERE path=?");
    q.addBindValue(t.value("path"));
    if (!q.exec() || !q.next())
        return 0;
    const auto id = q.value(0).toLongLong();
    emit changed();
    return id;
}
qint64 Library::addUrl(const QVariantMap &result, bool staged) {
    QUrl url(result.value("url", result.value("source")).toString());
    const auto match = QRegularExpression("(BV[0-9A-Za-z]{10})").match(url.path());
    if (url.scheme() != "https" ||
        !QStringList{"www.bilibili.com", "bilibili.com", "m.bilibili.com"}.contains(url.host()) ||
        !match.hasMatch())
        return 0;
    const int part = qMax(1, QUrlQuery(url).queryItemValue("p").toInt());
    const QString canonical =
        "https://www.bilibili.com/video/" + match.captured(1) + "?p=" + QString::number(part);
    Track track{{"path", canonical},
                {"source", canonical},
                {"storage", "url"},
                {"staged", staged ? 1 : 0},
                {"title", result.value("title")},
                {"artist", result.value("artist", QStringLiteral(""))},
                {"duration", result.value("duration", 0)},
                {"filesize", 0}};
    const auto id = addTrack(track);
    if (id && !staged)
        commitTrack(id);
    return id;
}
bool Library::commitTrack(qint64 id) {
    QSqlQuery q(m_db);
    q.prepare("UPDATE tracks SET staged=0 WHERE id=?");
    q.addBindValue(id);
    const bool ok = q.exec() && q.numRowsAffected() > 0;
    if (ok)
        emit changed();
    return ok;
}
QList<Track> Library::stagedTracks() const {
    QSqlQuery q(m_db);
    q.exec("SELECT * FROM tracks WHERE staged=1 ORDER BY id DESC");
    QList<Track> result;
    while (q.next())
        result << row(q);
    return result;
}
Track Library::track(qint64 id) const {
    QSqlQuery q(m_db);
    q.prepare("SELECT * FROM tracks WHERE id=?");
    q.addBindValue(id);
    return q.exec() && q.next() ? row(q) : Track{};
}
QList<Track> Library::tracks(qint64 playlist, const QString &search) const {
    QSqlQuery q(m_db);
    QString sql = "SELECT t.* FROM tracks t ";
    if (playlist)
        sql += "JOIN playlist_tracks p ON p.track_id=t.id WHERE p.playlist_id=? AND ";
    else
        sql += "WHERE ";
    sql += "t.staged=0 AND (instr(lower(t.title),lower(?))>0 OR instr(lower(t.artist),lower(?))>0) ";
    sql += playlist ? "ORDER BY p.position,t.id" : "ORDER BY t.id DESC";
    const QString needle = search.isNull() ? QStringLiteral("") : search;
    q.prepare(sql);
    if (playlist)
        q.addBindValue(playlist);
    q.addBindValue(needle);
    q.addBindValue(needle);
    QList<Track> out;
    if (q.exec())
        while (q.next())
            out << row(q);
    return out;
}
QList<QVariantMap> Library::playlists() const {
    QSqlQuery q(m_db);
    q.exec("SELECT p.id,p.name,count(pt.track_id) AS count FROM playlists p LEFT JOIN playlist_tracks pt ON "
           "pt.playlist_id=p.id GROUP BY p.id ORDER BY p.id");
    QList<QVariantMap> out;
    while (q.next())
        out << row(q);
    return out;
}
qint64 Library::createPlaylist(const QString &name) {
    if (name.trimmed().isEmpty())
        return 0;
    QSqlQuery q(m_db);
    q.prepare("INSERT INTO playlists(name) VALUES(?)");
    q.addBindValue(name.trimmed());
    if (!q.exec())
        return 0;
    auto id = q.lastInsertId().toLongLong();
    emit changed();
    return id;
}
bool Library::renamePlaylist(qint64 id, const QString &name) {
    if (name.trimmed().isEmpty())
        return false;
    QSqlQuery q(m_db);
    q.prepare("UPDATE playlists SET name=? WHERE id=?");
    q.addBindValue(name.trimmed());
    q.addBindValue(id);
    bool ok = q.exec();
    if (ok)
        emit changed();
    return ok;
}
bool Library::deletePlaylist(qint64 id) {
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM playlists WHERE id=?");
    q.addBindValue(id);
    bool ok = q.exec();
    if (ok)
        emit changed();
    return ok;
}
bool Library::addToPlaylist(qint64 playlist, const QList<qint64> &ids) {
    if (!m_db.transaction())
        return false;
    QSqlQuery q(m_db);
    q.prepare("SELECT COALESCE(MAX(position),-1)+1 FROM playlist_tracks WHERE playlist_id=?");
    q.addBindValue(playlist);
    q.exec();
    q.next();
    int pos = q.value(0).toInt();
    for (auto id : ids) {
        q.prepare("INSERT OR IGNORE INTO playlist_tracks VALUES(?,?,?)");
        q.addBindValue(playlist);
        q.addBindValue(id);
        q.addBindValue(pos++);
        if (!q.exec()) {
            m_db.rollback();
            return false;
        }
    }
    if (!m_db.commit())
        return false;
    emit changed();
    return true;
}
bool Library::removeFromPlaylist(qint64 playlist, const QList<qint64> &ids) {
    if (!m_db.transaction())
        return false;
    QSqlQuery q(m_db);
    for (auto id : ids) {
        q.prepare("DELETE FROM playlist_tracks WHERE playlist_id=? AND track_id=?");
        q.addBindValue(playlist);
        q.addBindValue(id);
        if (!q.exec()) {
            m_db.rollback();
            return false;
        }
    }
    if (!m_db.commit())
        return false;
    emit changed();
    return true;
}
bool Library::reorder(qint64 playlist, const QList<qint64> &ids) {
    if (!m_db.transaction())
        return false;
    QSqlQuery q(m_db);
    int pos = 0;
    for (auto id : ids) {
        q.prepare("UPDATE playlist_tracks SET position=? WHERE playlist_id=? AND track_id=?");
        q.addBindValue(pos++);
        q.addBindValue(playlist);
        q.addBindValue(id);
        if (!q.exec()) {
            m_db.rollback();
            return false;
        }
    }
    if (!m_db.commit())
        return false;
    emit changed();
    return true;
}
bool Library::deleteTrack(qint64 id) {
    QSqlQuery q(m_db);
    q.prepare("DELETE FROM tracks WHERE id=?");
    q.addBindValue(id);
    bool ok = q.exec();
    if (ok)
        emit changed();
    return ok;
}
bool Library::updateTrack(qint64 id, const QString &title, const QString &artist) {
    if (title.trimmed().isEmpty())
        return false;
    QSqlQuery q(m_db);
    q.prepare("UPDATE tracks SET title=?,artist=? WHERE id=?");
    q.addBindValue(title.trimmed());
    q.addBindValue(artist.trimmed());
    q.addBindValue(id);
    bool ok = q.exec();
    if (ok)
        emit changed();
    return ok;
}
bool Library::setLyrics(qint64 id, const QString &lrc, const QString &source) {
    QSqlQuery q(m_db);
    q.prepare("UPDATE tracks SET lrc=?,lyric_source=? WHERE id=?");
    q.addBindValue(lrc);
    q.addBindValue(source);
    q.addBindValue(id);
    bool ok = q.exec();
    if (ok)
        emit changed();
    return ok;
}
bool Library::setOffset(qint64 id, int ms) {
    QSqlQuery q(m_db);
    q.prepare("UPDATE tracks SET lyric_offset=? WHERE id=?");
    q.addBindValue(ms);
    q.addBindValue(id);
    bool ok = q.exec();
    if (ok)
        emit changed();
    return ok;
}
