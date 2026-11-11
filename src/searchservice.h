#pragma once
#include "wbi.h"
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QObject>
#include <QSettings>
struct SearchPage {
    QList<QVariantMap> items;
    int page = 1;
    bool hasNext = false;
    bool video = false;
};
class SearchService : public QObject {
    Q_OBJECT
  public:
    explicit SearchService(QSettings *settings, QObject *parent = nullptr);
    ~SearchService() override;
    void search(const QString &query, int page = 1);
    void cancel();
    static SearchPage parseSearch(const QJsonObject &data, int page);
    static SearchPage parseVideo(const QJsonObject &data);
    void setApiRootForTest(const QUrl &root);
    WbiSigner &wbi() {
        return m_wbi;
    }
  signals:
    void results(const QList<QVariantMap> &items, int page, bool hasNext, bool video);
    void message(const QString &text);
    void busyChanged(bool busy);

  private:
    void request(const QUrl &url, bool video, int page, quint64 token, bool retried = false);
    void get(const QUrl &url, bool video, int page, quint64 token, bool shortLink = false,
             const QUrl &unsignedUrl = {}, bool retried = false);
    QUrl api(const QString &path) const;
    QSettings *m_settings;
    QNetworkAccessManager m_network;
    WbiSigner m_wbi;
    QNetworkReply *m_active = nullptr;
    quint64 m_token = 0;
    QString m_buvid;
    QUrl m_apiRoot;
};
