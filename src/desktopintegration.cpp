#include "desktopintegration.h"
#include "features.h"
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QLocalSocket>
#include <QSettings>

namespace {
const auto runKey = "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Run";
QString quoted(QString value) {
    // Windows command-line parsing consumes backslashes before the closing quote.
    int trailing = 0;
    while (trailing < value.size() && value[value.size() - trailing - 1] == '\\')
        ++trailing;
    return '"' + value + QString(trailing, '\\') + '"';
}
} // namespace
namespace DesktopIntegration {
ApplicationInstance::ApplicationInstance(QObject *parent) : QObject(parent) {
    m_server.setParent(this);
    m_server.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&m_server, &QLocalServer::newConnection, this, [this] {
        while (auto socket = m_server.nextPendingConnection()) {
            socket->setParent(this);
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            auto read = [this, socket] {
                auto bytes = socket->property("requestBytes").toByteArray() + socket->readAll();
                if (bytes.size() > 65536) {
                    socket->abort();
                    return;
                }
                const int newline = bytes.indexOf('\n');
                if (newline < 0) {
                    socket->setProperty("requestBytes", bytes);
                    return;
                }
                const auto value = QJsonDocument::fromJson(bytes.left(newline));
                if (value.isObject())
                    emit commandRequested(value.object());
                socket->disconnectFromServer();
            };
            connect(socket, &QLocalSocket::readyRead, this, read);
            if (socket->bytesAvailable())
                read();
        }
    });
}
ApplicationInstance::Claim ApplicationInstance::claim(const QString &dataDirectory,
                                                      const QJsonObject &request, QString *error) {
    auto identity = QFileInfo(dataDirectory).canonicalFilePath();
    if (identity.isEmpty())
        identity = QDir(dataDirectory).absolutePath();
#ifdef Q_OS_WIN
    identity = identity.toCaseFolded();
#endif
    const auto digest =
        QCryptographicHash::hash((startupEntryName() + identity).toUtf8(), QCryptographicHash::Sha256)
            .toHex()
            .left(32);
    const auto name = "BiliWave-" + QString::fromLatin1(digest);
    QLocalSocket existing;
    existing.connectToServer(name);
    if (existing.waitForConnected(300)) {
        const auto message = QJsonDocument(request).toJson(QJsonDocument::Compact) + '\n';
        if (existing.write(message) != message.size() ||
            (existing.bytesToWrite() && !existing.waitForBytesWritten(1500))) {
            if (error)
                *error = "无法唤回正在运行的 BiliWave。";
            return Failed;
        }
        existing.disconnectFromServer();
        return Notified;
    }
    if (m_server.listen(name))
        return Primary;
    if (error)
        *error = "无法创建应用实例：" + m_server.errorString();
    return Failed;
}
QString startupEntryName() {
    return recognitionFeaturesEnabled() ? "BiliWave" : "BiliWave Lite";
}
QString startupCommand(const QString &executable, const QString &dataDirectory) {
    const auto exe = QDir::toNativeSeparators(QFileInfo(executable).absoluteFilePath());
    const auto data = QDir::toNativeSeparators(QDir(dataDirectory).absolutePath());
    return quoted(exe) + " --startup --data " + quoted(data);
}
bool writeStartupEntry(QSettings &store, bool enabled, const QString &executable,
                       const QString &dataDirectory, QString *error) {
    const auto key = startupEntryName();
    const auto command = startupCommand(executable, dataDirectory);
    if (enabled)
        store.setValue(key, command);
    else
        store.remove(key);
    store.sync();
    const bool ok = store.status() == QSettings::NoError &&
                    (enabled ? store.value(key).toString() == command : !store.contains(key));
    if (!ok && error)
        *error = "无法修改当前用户的开机启动项，请检查 Windows 权限。";
    return ok;
}
bool startupEnabled() {
#ifdef Q_OS_WIN
    QSettings store(runKey, QSettings::NativeFormat);
    return !store.value(startupEntryName()).toString().isEmpty();
#else
    return false;
#endif
}
bool setStartupEnabled(bool enabled, const QString &dataDirectory, QString *error) {
#ifdef Q_OS_WIN
    QSettings store(runKey, QSettings::NativeFormat);
    return writeStartupEntry(store, enabled, QCoreApplication::applicationFilePath(), dataDirectory, error);
#else
    Q_UNUSED(enabled)
    Q_UNUSED(dataDirectory)
    if (error)
        *error = "此平台暂不支持设置开机启动。";
    return false;
#endif
}
} // namespace DesktopIntegration
