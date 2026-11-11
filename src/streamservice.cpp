#include "streamservice.h"
#include "services.h"
#include <QDateTime>
#include <QTimeZone>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QPointer>
#include <QRegularExpression>
#include <QTcpSocket>
#include <QTimer>
#include <QUrlQuery>
#include <QUuid>
#include <memory>

QDateTime ResolvedStreamCache::expiryFromUpstream(const QUrl &upstream, const QDateTime &fetchedAt) {
    const auto fetched = fetchedAt.isValid() ? fetchedAt.toUTC() : QDateTime::currentDateTimeUtc();
    QDateTime earliest;
    for (const auto &item : QUrlQuery(upstream).queryItems()) {
        const auto key = item.first.toLower();
        if (key != QLatin1String("deadline") && key != QLatin1String("expires") &&
            key != QLatin1String("expire") && key != QLatin1String("expiry"))
            continue;
        bool ok = false;
        auto value = item.second.toLongLong(&ok);
        if (!ok || value <= 0)
            continue;
        if (value > 100000000000LL)
            value /= 1000;
        if (value < 1000000000LL)
            continue;
        const auto candidate = QDateTime::fromSecsSinceEpoch(value, QTimeZone(QTimeZone::UTC));
        if (!earliest.isValid() || candidate < earliest)
            earliest = candidate;
    }
    // Signed CDN URLs that do not advertise a timestamp are reused briefly, then resolved again.
    return earliest.isValid() ? earliest : fetched.addSecs(20 * 60);
}
bool ResolvedStreamCache::stillValid(const QDateTime &expiresAt, const QDateTime &now, int marginSeconds) {
    if (!expiresAt.isValid() || !now.isValid())
        return false;
    return expiresAt.toUTC() > now.toUTC().addSecs(qMax(0, marginSeconds));
}
bool ResolvedStreamCache::upstreamStatusExpired(int httpStatus) {
    return httpStatus == 401 || httpStatus == 403 || httpStatus == 410;
}
void ResolvedStreamCache::store(const QString &source, const Entry &entry) {
    if (!source.isEmpty() && entry.upstream.isValid() && entry.expiresAt.isValid())
        m_entries.insert(source, entry);
}
void ResolvedStreamCache::invalidate(const QString &source) {
    m_entries.remove(source);
}
std::optional<ResolvedStreamCache::Entry> ResolvedStreamCache::takeIfValid(const QString &source,
                                                                           const QDateTime &now) {
    auto it = m_entries.find(source);
    if (it == m_entries.end())
        return std::nullopt;
    if (!stillValid(it->expiresAt, now)) {
        m_entries.erase(it);
        return std::nullopt;
    }
    return *it;
}

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
void StreamService::rememberResolved(const QString &source, const QUrl &upstream,
                                     const QMap<QByteArray, QByteArray> &headers, const QDateTime &expiresAt) {
    ResolvedStreamCache::Entry entry;
    entry.upstream = upstream;
    entry.headers = headers;
    entry.expiresAt = expiresAt.isValid() ? expiresAt.toUTC()
                                          : ResolvedStreamCache::expiryFromUpstream(upstream, QDateTime::currentDateTimeUtc());
    m_cache.store(source, entry);
}
void StreamService::adoptResolved(const QString &source, const QUrl &upstream, QMap<QByteArray, QByteArray> headers,
                                  const QDateTime &expiresAt) {
    if (!headers.contains("Referer"))
        headers.insert("Referer", "https://www.bilibili.com/");
    m_upstream = upstream;
    m_headers = headers;
    m_activeSource = source;
    m_resolvedExpiry = expiresAt.toUTC();
    m_upstreamRejected = false;
    m_route = "/" + QUuid::createUuid().toString(QUuid::WithoutBraces) + "/audio.m4a";
    ResolvedStreamCache::Entry entry;
    entry.upstream = upstream;
    entry.headers = m_headers;
    entry.expiresAt = m_resolvedExpiry;
    m_cache.store(source, entry);
}
QUrl StreamService::localPlaybackUrl() const {
    if (!m_server.isListening() || m_route.isEmpty())
        return {};
    return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(m_server.serverPort()).arg(m_route));
}
void StreamService::noteUpstreamStatus(int status, const QString &source, const QUrl &upstream) {
    if (!ResolvedStreamCache::upstreamStatusExpired(status) || m_upstreamRejected)
        return;
    if (source != m_activeSource || upstream != m_upstream)
        return;
    m_upstreamRejected = true;
    m_cache.invalidate(source);
    m_resolvedExpiry = QDateTime::fromSecsSinceEpoch(0, QTimeZone(QTimeZone::UTC));
    emit upstreamRejected();
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
    if (const auto cached = m_cache.takeIfValid(source, QDateTime::currentDateTimeUtc())) {
        adoptResolved(source, cached->upstream, cached->headers, cached->expiresAt);
        emit ready(token, localPlaybackUrl(), {});
        emit message("在线音轨已就绪");
        return;
    }
    auto process = new QProcess(this);
    m_process = process;
    auto timer = new QTimer(process);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, process, &QProcess::kill);
    connect(process, &QProcess::finished, this, [this, process, generation, token, source](int code) {
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
        const QUrl upstream(format.value("url").toString());
        if (upstream.scheme() != "https" && upstream.scheme() != "http") {
            emit ready(token, {}, "音轨地址协议不支持");
            return;
        }
        QMap<QByteArray, QByteArray> headers;
        auto headerObject = metadata.value("http_headers").toObject();
        const auto custom = format.value("http_headers").toObject();
        for (auto it = custom.begin(); it != custom.end(); ++it)
            headerObject[it.key()] = it.value();
        for (auto it = headerObject.begin(); it != headerObject.end(); ++it)
            headers.insert(it.key().toUtf8(), it.value().toString().toUtf8());
        const auto fetchedAt = QDateTime::currentDateTimeUtc();
        adoptResolved(source, upstream, headers, ResolvedStreamCache::expiryFromUpstream(upstream, fetchedAt));
        emit ready(token, localPlaybackUrl(), {});
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
            const QString proxiedSource = m_activeSource;
            const QUrl proxiedUpstream = m_upstream;
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
            auto pump = [this, socket, reply, sent, proxiedSource, proxiedUpstream] {
                if (socket->state() != QAbstractSocket::ConnectedState)
                    return;
                if (!*sent && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).isValid()) {
                    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                    noteUpstreamStatus(status, proxiedSource, proxiedUpstream);
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
