#pragma once
#include <QByteArray>
#include <QList>
#include <QNetworkAccessManager>
#include <QObject>
#include <QPair>
#include <QString>
#include <QUrl>
#include <functional>
class QNetworkReply;

// Bilibili WBI signer. Keys come from the nav API and are cached until the next 00:00 CST.
class WbiSigner : public QObject {
    Q_OBJECT
  public:
    explicit WbiSigner(QNetworkAccessManager *network, QObject *parent = nullptr);
    ~WbiSigner() override;

    static QString mixinKey(const QString &imgKey, const QString &subKey);
    static QString keyFromUrl(const QString &url);
    static QString signQuery(const QList<QPair<QString, QString>> &params, const QString &mixinKey,
                             qint64 wts);
    static QUrl signUrl(const QUrl &url, const QString &mixinKey, qint64 wts);
    static bool keysFromNav(const QByteArray &body, QString *imgKey, QString *subKey, QString *error);

    using Callback = std::function<void(const QString &mixinKey, const QString &error)>;
    void withMixinKey(const Callback &done);
    void invalidate();
    bool cacheFresh(qint64 at) const;
    qint64 expiresAt() const {
        return m_expiresAt;
    }
    qint64 timestamp() const;

    void setClockForTest(std::function<qint64()> clock);
    void setNavUrlForTest(const QUrl &url);
    void setKeysForTest(const QString &imgKey, const QString &subKey, qint64 fetchedAt);

  private:
    void fetch();
    void finish(const QString &mixin, const QString &error);
    static qint64 expiryAfter(qint64 fetchedAt);

    QNetworkAccessManager *m_network;
    QNetworkReply *m_reply = nullptr;
    QList<Callback> m_waiters;
    QString m_mixin;
    qint64 m_expiresAt = 0;
    int m_generation = 0;
    std::function<qint64()> m_clock;
    QUrl m_navUrl;
};
