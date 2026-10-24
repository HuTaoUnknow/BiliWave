#pragma once
#include <QNetworkAccessManager>
#include <QPointer>
#include <QQueue>
#include <QUrl>
#include <QVariantMap>
class QNetworkReply;

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
