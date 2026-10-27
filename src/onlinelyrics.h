#pragma once
#include <QNetworkAccessManager>
#include <QPointer>
#include <QQueue>
#include <QUrl>
#include <QVariantMap>
#include <QJsonArray>
#include <QMap>
#include <QSet>
#include <QTimer>
#include <functional>
class QNetworkReply;
class QSettings;

class OnlineLyricsSearch : public QObject {
    Q_OBJECT
  public:
    explicit OnlineLyricsSearch(QObject *parent = nullptr, QUrl api = QUrl("https://lrclib.net/api/"));
    void search(const QVariantMap &track, const QString &keywords = {});
    void cancel();
    static QString cleanTitle(QString title);
    static QString normalized(QString text);
    static QList<QVariantMap> ranked(const QVariantMap &track, QList<QVariantMap> candidates);
    static QList<QUrl> plan(const QUrl &api, const QVariantMap &track, const QString &keywords = {});
  signals:
    void progress(const QString &message);
    void finished(const QList<QVariantMap> &candidates, const QString &error);

  private:
    void next(quint64 token);
    void finish();
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QUrl m_api;
    QQueue<QUrl> m_requests;
    QVariantMap m_track;
    QList<QVariantMap> m_candidates;
    QString m_error;
    quint64 m_token = 0;
    qint64 m_retryAfter = 0;
    int m_total = 0;
    int m_retries = 0;
};

// One active request at a time; sources are fallbacks, never a fan-out search.
class LyricsSourcesSearch : public QObject {
    Q_OBJECT
  public:
    explicit LyricsSourcesSearch(QObject *parent = nullptr, QMap<QString, QUrl> endpoints = {});
    void search(const QVariantMap &track, const QString &keywords, const QStringList &sources);
    void cancel();
    static QList<QPair<QString, QString>> sources();
    static QStringList selectedSources(QSettings *settings);
    static QString sourceName(const QString &id);
    bool hasMore() const;
    QVariantMap statistics() const;
  signals:
    void progress(const QString &message);
    void finished(const QList<QVariantMap> &candidates, const QString &error);
  private:
    struct Source {
        QString id;
        QList<QVariantMap> pending;
        int page = 0;
        bool exhausted = false;
    };
    void advance(quint64 token);
    void loadPage(quint64 token);
    void loadLyrics(quint64 token, QVariantMap candidate);
    void selectSirenSongs(quint64 token);
    void request(quint64 token, const QUrl &url, std::function<void(const QByteArray &)> complete);
    void finish(const QString &error = {});
    QUrl url(const QString &source, const QString &path,
             const QList<QPair<QString, QString>> &parameters = {}) const;
    void enqueue(QVariantMap candidate);
    void accept(QVariantMap candidate);
    QNetworkAccessManager m_network;
    QPointer<QNetworkReply> m_reply;
    QMap<QString, QUrl> m_endpoints;
    QTimer m_deadline;
    QList<Source> m_sources;
    QList<QVariantMap> m_batch;
    QSet<QString> m_seen;
    QJsonArray m_sirenCatalog;
    QVariantMap m_track;
    QString m_keywords, m_key, m_lastError;
    QUrl m_direct;
    quint64 m_token = 0;
    int m_source = 0, m_requests = 0;
    qint64 m_bytes = 0, m_nextRequest = 0, m_cooldown = 0, m_catalogTime = 0;
    bool m_active = false;
};
