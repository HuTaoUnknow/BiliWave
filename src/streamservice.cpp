#include "streamservice.h"
#include "services.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QPointer>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QTimer>
#include <QUuid>
#include <memory>

StreamService::StreamService(QSettings *settings, QObject *parent) : QObject(parent), m_settings(settings) {
    m_server.listen(QHostAddress::LocalHost, 0);
    connect(&m_server, &QTcpServer::newConnection, this, &StreamService::acceptConnection);
}
StreamService::~StreamService() {
    cancel();
    for (auto process : findChildren<QProcess *>()) {
        process->disconnect(this);
        process->kill();
        process->waitForFinished(2000);
    }
    for (auto reply : m_network.findChildren<QNetworkReply *>()) {
        reply->disconnect();
        reply->abort();
    }
}
QJsonObject StreamService::bestAudio(const QJsonObject &metadata) {
    QJsonObject best;
    double score = -1;
    for (const auto &value : metadata.value("formats").toArray()) {
        const auto format = value.toObject();
        const auto codec = format.value("acodec").toString();
        if (format.value("vcodec").toString() != "none" || codec.isEmpty() || codec == "none" ||
            format.value("url").toString().isEmpty())
            continue;
        const double rank = (codec.contains("flac", Qt::CaseInsensitive) || codec == "alac" ? 1e9 : 0) +
                            format.value("abr").toDouble(format.value("tbr").toDouble()) * 1000;
        if (rank > score) {
            score = rank;
            best = format;
        }
    }
    return best;
}
void StreamService::cancel() {
    ++m_generation;
    if (m_process) {
        m_process->kill();
        m_process = nullptr;
    }
}
void StreamService::resolve(quint64 token, const QString &source) {
    cancel();
    const auto generation = m_generation;
    const QUrl input(source);
    if (input.scheme() != "https" ||
        !QStringList{"bilibili.com", "www.bilibili.com", "m.bilibili.com", "b23.tv"}.contains(input.host())) {
        emit ready(token, {}, "无效的哔哩哔哩链接");
        return;
    }
    if (!m_server.isListening()) {
        emit ready(token, {}, "无法启动本机音频转发");
        return;
    }
    auto process = new QProcess(this);
    m_process = process;
    auto timer = new QTimer(process);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, process, &QProcess::kill);
    connect(process, &QProcess::finished, this, [this, process, generation, token](int code) {
        process->deleteLater();
        if (generation != m_generation)
            return;
        m_process = nullptr;
        const auto metadata = QJsonDocument::fromJson(process->readAllStandardOutput()).object();
        const auto format = bestAudio(metadata);
        if (code || format.isEmpty()) {
            emit ready(token, {}, "无法获取在线音轨，请检查网络、登录权限或重试");
            return;
        }
        m_upstream = QUrl(format.value("url").toString());
        if (m_upstream.scheme() != "https" && m_upstream.scheme() != "http") {
            emit ready(token, {}, "音轨地址协议不支持");
            return;
        }
        m_headers.clear();
        auto headers = metadata.value("http_headers").toObject();
        const auto custom = format.value("http_headers").toObject();
        for (auto it = custom.begin(); it != custom.end(); ++it)
            headers[it.key()] = it.value();
        for (auto it = headers.begin(); it != headers.end(); ++it)
            m_headers[it.key().toUtf8()] = it.value().toString().toUtf8();
        if (!m_headers.contains("Referer"))
            m_headers["Referer"] = "https://www.bilibili.com/";
        m_route = "/" + QUuid::createUuid().toString(QUuid::WithoutBraces) + "/audio.m4a";
        emit ready(token, QUrl(QString("http://127.0.0.1:%1%2").arg(m_server.serverPort()).arg(m_route)), {});
        emit message("在线音轨已就绪 · " + format.value("acodec").toString());
    });
    connect(process, &QProcess::errorOccurred, this, [this, process, token, generation](auto error) {
        if (error == QProcess::FailedToStart) {
            if (generation == m_generation) {
                m_process = nullptr;
                emit ready(token, {}, "无法启动在线音轨解析工具，请检查 yt-dlp 设置");
            }
            process->deleteLater();
        }
    });
    QStringList args{"--ignore-config",    "--no-playlist",
                     "--dump-single-json", "--skip-download",
                     "--socket-timeout",   "20",
                     "--retries",          "1"};
    const auto cookies = m_settings->value("download/cookies").toString();
    if (!cookies.isEmpty())
        args << "--cookies" << cookies;
    args << "--" << source;
    emit message("正在获取在线音轨…");
    process->start(toolPath(m_settings, "yt-dlp"), args);
    timer->start(60000);
}
void StreamService::acceptConnection() {
    while (m_server.hasPendingConnections()) {
        auto socket = m_server.nextPendingConnection();
        auto requestData = std::make_shared<QByteArray>();
        auto handled = std::make_shared<bool>(false);
        auto idle = new QTimer(socket);
        idle->setSingleShot(true);
        idle->start(30000);
        connect(idle, &QTimer::timeout, socket, &QTcpSocket::abort);
        connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
        connect(socket, &QTcpSocket::readyRead, this, [this, socket, requestData, handled, idle] {
            if (*handled)
                return;
            *requestData += socket->readAll();
            if (requestData->size() > 16384) {
                socket->abort();
                return;
            }
            if (!requestData->contains("\r\n\r\n"))
                return;
            *handled = true;
            idle->stop();
            const auto lines = requestData->split('\n');
            const auto first = lines.first().trimmed().split(' ');
            if (first.size() != 3 || (first[0] != "GET" && first[0] != "HEAD") ||
                first[1] != m_route.toUtf8()) {
                socket->write("HTTP/1.1 404 Not Found\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                socket->disconnectFromHost();
                return;
            }
            QNetworkRequest request(m_upstream);
            for (auto it = m_headers.begin(); it != m_headers.end(); ++it)
                request.setRawHeader(it.key(), it.value());
            for (const auto &line : lines)
                if (line.toLower().startsWith("range:")) {
                    const auto range = line.mid(6).trimmed();
                    if (QRegularExpression("^bytes=\\d*-\\d*$").match(QString::fromLatin1(range)).hasMatch())
                        request.setRawHeader("Range", range);
                }
            request.setTransferTimeout(30000);
            auto reply = first[0] == "HEAD" ? m_network.head(request) : m_network.get(request);
            reply->setReadBufferSize(262144);
            auto sent = std::make_shared<bool>(false);
            auto pump = [socket, reply, sent] {
                if (socket->state() != QAbstractSocket::ConnectedState)
                    return;
                if (!*sent && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).isValid()) {
                    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                    QByteArray headers =
                        "HTTP/1.1 " + QByteArray::number(status) + " OK\r\nConnection: close\r\n";
                    for (const auto &name : {QByteArray("Content-Type"), QByteArray("Content-Length"),
                                             QByteArray("Content-Range"), QByteArray("Accept-Ranges")}) {
                        const auto value = reply->rawHeader(name);
                        if (!value.isEmpty())
                            headers += name + ": " + value + "\r\n";
                    }
                    socket->write(headers + "\r\n");
                    *sent = true;
                }
                while (*sent && reply->bytesAvailable() && socket->bytesToWrite() < 262144)
                    socket->write(reply->read(32768));
                if (reply->isFinished() && !reply->bytesAvailable()) {
                    if (!*sent)
                        socket->write(
                            "HTTP/1.1 502 Bad Gateway\r\nContent-Length: 0\r\nConnection: close\r\n\r\n");
                    socket->disconnectFromHost();
                }
            };
            connect(reply, &QNetworkReply::metaDataChanged, socket, pump);
            connect(reply, &QIODevice::readyRead, socket, pump);
            connect(reply, &QNetworkReply::finished, socket, pump);
            connect(socket, &QTcpSocket::bytesWritten, socket, pump);
            connect(socket, &QObject::destroyed, reply, [reply] {
                reply->abort();
                reply->deleteLater();
            });
        });
    }
}
