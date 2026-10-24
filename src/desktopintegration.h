#pragma once
#include <QJsonObject>
#include <QLocalServer>
#include <QString>
class QSettings;

namespace DesktopIntegration {
class ApplicationInstance : public QObject {
    Q_OBJECT
  public:
    enum Claim { Primary, Notified, Failed };
    explicit ApplicationInstance(QObject *parent = nullptr);
    Claim claim(const QString &dataDirectory, const QJsonObject &request, QString *error = nullptr);
  signals:
    void commandRequested(const QJsonObject &request);

  private:
    QLocalServer m_server;
};
QString startupEntryName();
QString startupCommand(const QString &executable, const QString &dataDirectory);
bool startupEnabled();
bool setStartupEnabled(bool enabled, const QString &dataDirectory, QString *error = nullptr);
// A separate settings store lets tests verify registration without changing Windows startup.
bool writeStartupEntry(QSettings &store, bool enabled, const QString &executable,
                       const QString &dataDirectory, QString *error = nullptr);
} // namespace DesktopIntegration
