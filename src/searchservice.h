#pragma once
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
  signals:
    void results(const QList<QVariantMap> &items, int page, bool hasNext, bool video);
    void message(const QString &text);
    void busyChanged(bool busy);

  private:
    void get(const QUrl &url, bool video, int page, quint64 token, bool shortLink = false);
    QSettings *m_settings;
    QNetworkAccessManager m_network;
    QNetworkReply *m_active = nullptr;
    quint64 m_token = 0;
    QString m_buvid;
};
