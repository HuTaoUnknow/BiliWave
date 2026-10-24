#pragma once
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QProcess>
#include <QSettings>
#include <QTcpServer>

// Resolves a fresh audio URL and forwards byte ranges in memory with required CDN headers.
class StreamService : public QObject {
    Q_OBJECT
  public:
    explicit StreamService(QSettings *settings, QObject *parent = nullptr);
    ~StreamService() override;
    void resolve(quint64 token, const QString &source);
    void cancel();
    static QJsonObject bestAudio(const QJsonObject &metadata);
  signals:
    void ready(quint64 token, const QUrl &url, const QString &error);
    void message(const QString &text);

  private:
    void acceptConnection();
    QSettings *m_settings;
    QTcpServer m_server;
    QNetworkAccessManager m_network;
    QProcess *m_process = nullptr;
    quint64 m_generation = 0;
    QString m_route;
    QUrl m_upstream;
    QMap<QByteArray, QByteArray> m_headers;
};
