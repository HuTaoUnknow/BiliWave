#pragma once
#include <QDateTime>
#include <QHash>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QProcess>
#include <QSettings>
#include <QTcpServer>
#include <optional>

// In-memory map from a Bilibili page URL to the signed CDN audio URL.
// Nothing here is written to disk. Callers must treat entries as expired a little
// early, and drop them when the CDN answers 401, 403 or 410.
class ResolvedStreamCache {
  public:
    struct Entry {
        QUrl upstream;
        QMap<QByteArray, QByteArray> headers;
        QDateTime expiresAt;
    };
    static constexpr int reuseMarginSeconds = 30;
    static QDateTime expiryFromUpstream(const QUrl &upstream, const QDateTime &fetchedAt);
    static bool stillValid(const QDateTime &expiresAt, const QDateTime &now,
                           int marginSeconds = reuseMarginSeconds);
    static bool upstreamStatusExpired(int httpStatus);
    void store(const QString &source, const Entry &entry);
    void invalidate(const QString &source);
    std::optional<Entry> takeIfValid(const QString &source, const QDateTime &now);

  private:
    QHash<QString, Entry> m_entries;
};

// Resolves a fresh audio URL and forwards byte ranges in memory with required CDN headers.
class StreamService : public QObject {
    Q_OBJECT
  public:
    explicit StreamService(QSettings *settings, QObject *parent = nullptr);
    ~StreamService() override;
    void resolve(quint64 token, const QString &source);
    void cancel();
    // Remembers a resolved CDN URL without starting yt-dlp. The next resolve()
    // for this page reuses it until expiryFromUpstream / the given time.
    void rememberResolved(const QString &source, const QUrl &upstream,
                          const QMap<QByteArray, QByteArray> &headers, const QDateTime &expiresAt);
    QDateTime resolvedExpiry() const {
        return m_resolvedExpiry;
    }
    QUrl upstream() const {
        return m_upstream;
    }
    bool resolving() const {
        return m_process != nullptr;
    }
    static QJsonObject bestAudio(const QJsonObject &metadata);
  signals:
    void ready(quint64 token, const QUrl &url, const QString &error);
    void message(const QString &text);
    void upstreamRejected();

  private:
    void acceptConnection();
    void adoptResolved(const QString &source, const QUrl &upstream, QMap<QByteArray, QByteArray> headers,
                       const QDateTime &expiresAt);
    void noteUpstreamStatus(int status, const QString &source, const QUrl &upstream);
    QUrl localPlaybackUrl() const;
    QSettings *m_settings;
    QTcpServer m_server;
    QNetworkAccessManager m_network;
    QProcess *m_process = nullptr;
    quint64 m_generation = 0;
    QString m_route;
    QString m_activeSource;
    QUrl m_upstream;
    QMap<QByteArray, QByteArray> m_headers;
    QDateTime m_resolvedExpiry;
    bool m_upstreamRejected = false;
    ResolvedStreamCache m_cache;
};
