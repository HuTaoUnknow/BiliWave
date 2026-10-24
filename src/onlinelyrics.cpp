#include "onlinelyrics.h"
#include "lrc.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QUrlQuery>
#include <algorithm>
#include <cmath>

namespace {
double similarity(const QString &a, const QString &b) {
    if (a.isEmpty() || b.isEmpty())
        return 0;
    if (a == b)
        return 1;
    QVector<int> last(b.size() + 1), row(b.size() + 1);
    for (int j = 0; j <= b.size(); ++j)
        last[j] = j;
    for (int i = 1; i <= a.size(); ++i) {
        row[0] = i;
        for (int j = 1; j <= b.size(); ++j)
            row[j] = qMin(qMin(last[j] + 1, row[j - 1] + 1), last[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1));
        last.swap(row);
    }
    return 1. - double(last.last()) / qMax(a.size(), b.size());
}
} // namespace
OnlineLyricsSearch::OnlineLyricsSearch(QObject *parent, QUrl api) : QObject(parent), m_api(api) {}
QString OnlineLyricsSearch::normalized(QString text) {
    text = text.normalized(QString::NormalizationForm_KC).toCaseFolded().left(300);
    text.remove(QRegularExpression("[\\s\\p{P}\\p{S}]+"));
    return text;
}
QString OnlineLyricsSearch::cleanTitle(QString title) {
    title = title.normalized(QString::NormalizationForm_KC).trimmed().left(500);
    const auto song = QRegularExpression("《([^》]+)》").match(title);
    if (song.hasMatch())
        return song.captured(1).trimmed();
    title.remove(QRegularExpression("\\[(?:[^\\]]*(?:hi.res|无损|音质|歌词|字幕|1080|4k)[^\\]]*)\\]",
                                    QRegularExpression::CaseInsensitiveOption));
    title.remove(QRegularExpression("【[^】]*(?:无损|音质|歌词|字幕|高清|翻译)[^】]*】"));
    title.remove(QRegularExpression("\\bBV[0-9A-Za-z]{10}\\b"));
    return title.simplified();
}
QList<QUrl> OnlineLyricsSearch::plan(const QUrl &api, const QVariantMap &track, const QString &keywords) {
    QList<QUrl> urls;
    auto add = [&](const QString &path, const QList<QPair<QString, QString>> &parameters) {
        QUrl url = api.resolved(QUrl(path));
        QUrlQuery query;
        for (const auto &p : parameters)
            if (!p.second.isEmpty())
                query.addQueryItem(p.first, p.second);
        url.setQuery(query);
        if (!urls.contains(url))
            urls << url;
    };
    if (keywords.trimmed().startsWith("https://", Qt::CaseInsensitive)) {
        const QUrl link(keywords.trimmed());
        const auto match = QRegularExpression("^/(?:api/get|tracks)/(\\d+)/?$").match(link.path());
        if ((link.host() == "lrclib.net" || link.host() == "www.lrclib.net") && match.hasMatch() &&
            link.userInfo().isEmpty())
            add("get/" + match.captured(1), {});
        return urls;
    }
    const auto title = cleanTitle(track.value("title").toString());
    const auto artist = track.value("artist").toString().trimmed().left(200);
    const double seconds = track.value("duration").toLongLong() / 1000.;
    if (!keywords.trimmed().isEmpty())
        add("search", {{"q", keywords.trimmed().left(300)}});
    if (title.isEmpty())
        return urls;
    if (keywords.trimmed().isEmpty() && !artist.isEmpty() && seconds >= 1 && seconds <= 3600)
        add("get",
            {{"track_name", title}, {"artist_name", artist}, {"duration", QString::number(seconds, 'f', 3)}});
    if (!artist.isEmpty())
        add("search", {{"track_name", title}, {"artist_name", artist}});
    add("search", {{"track_name", title}});
    add("search", {{"q", title + (artist.isEmpty() ? QString() : " " + artist)}});
    const auto original = track.value("title").toString().simplified().left(300);
    if (original != title && urls.size() < 5)
        add("search", {{"q", original}});
    return urls.mid(0, 5);
}
QList<QVariantMap> OnlineLyricsSearch::ranked(const QVariantMap &track, QList<QVariantMap> candidates) {
    QList<QVariantMap> result;
    QSet<QString> seen;
    const auto title = normalized(cleanTitle(track.value("title").toString()));
    const auto artist = normalized(track.value("artist").toString());
    const double duration = track.value("duration").toLongLong() / 1000.;
    for (auto c : candidates) {
        const bool synced = !Lrc::parse(c.value("syncedLyrics").toString()).isEmpty();
        if (!synced && c.value("plainLyrics").toString().trimmed().isEmpty())
            continue;
        const auto text = c.value(synced ? "syncedLyrics" : "plainLyrics").toString().trimmed();
        const auto key =
            normalized(c.value("trackName").toString()) + "\n" +
            normalized(c.value("artistName").toString()) + "\n" +
            QString::fromLatin1(QCryptographicHash::hash(text.toUtf8(), QCryptographicHash::Sha256).toHex());
        if (seen.contains(key))
            continue;
        seen.insert(key);
        const double titleScore = similarity(title, normalized(cleanTitle(c.value("trackName").toString())));
        const double artistScore = similarity(artist, normalized(c.value("artistName").toString()));
        const bool durationKnown = duration > 0 && c.value("duration").toDouble() > 0;
        const double difference = durationKnown ? qAbs(duration - c.value("duration").toDouble()) : 0;
        const double durationScore =
            durationKnown ? qMax(0., 1. - difference / qMax(15., duration * .15)) : .5;
        const double score = (artist.isEmpty() ? .8 : .6) * titleScore +
                             (artist.isEmpty() ? 0 : .2) * artistScore + .15 * durationScore +
                             (synced ? .05 : 0);
        c["score"] = qRound(score * 100);
        c["difference"] = durationKnown ? QVariant(difference) : QVariant();
        c["provider"] = "LRCLIB";
        c["sourceUrl"] = QString("https://lrclib.net/api/get/%1").arg(c.value("id").toString());
        c["automatic"] = synced && titleScore == 1 && !artist.isEmpty() && artistScore == 1 &&
                         durationKnown && difference <= 3;
        result << c;
    }
    std::stable_sort(result.begin(), result.end(), [](const auto &a, const auto &b) {
        return a.value("score").toInt() > b.value("score").toInt();
    });
    return result.mid(0, 50);
}
void OnlineLyricsSearch::cancel() {
    ++m_token;
    m_requests.clear();
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
}
void OnlineLyricsSearch::search(const QVariantMap &track, const QString &keywords) {
    cancel();
    m_track = track;
    m_candidates.clear();
    m_error.clear();
    m_retries = 2;
    const auto token = m_token;
    const qint64 remaining = m_retryAfter - QDateTime::currentSecsSinceEpoch();
    if (remaining > 0) {
        QTimer::singleShot(0, this, [this, token, remaining] {
            if (token == m_token)
                emit finished({}, tr("歌词服务限流，请在 %1 秒后重试").arg(remaining));
        });
        return;
    }
    for (const auto &url : plan(m_api, track, keywords))
        m_requests.enqueue(url);
    m_total = m_requests.size();
    if (!m_total)
        m_error = tr("请输入歌曲名，或 https://lrclib.net/tracks/数字 的歌词链接");
    next(token);
}
void OnlineLyricsSearch::next(quint64 token) {
    if (token != m_token)
        return;
    if (m_requests.isEmpty()) {
        finish();
        return;
    }
    const auto url = m_requests.dequeue();
    emit progress(tr("在线搜索歌词 %1/%2…").arg(m_total - m_requests.size()).arg(m_total));
    QNetworkRequest request(url);
    request.setTransferTimeout(12000);
    request.setRawHeader("User-Agent", "BiliWave/1.10.0 (https://github.com/HuTaoUnknow/BiliWave)");
    request.setRawHeader("Accept", "application/json");
    auto reply = m_network.get(request);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, token] {
        reply->deleteLater();
        if (token != m_token)
            return;
        m_reply = nullptr;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 429 || status == 503) {
            bool valid = false;
            qint64 wait = reply->rawHeader("Retry-After").toLongLong(&valid);
            if (!valid)
                wait = QDateTime::fromString(QString::fromLatin1(reply->rawHeader("Retry-After")),
                                             Qt::RFC2822Date)
                           .toSecsSinceEpoch() -
                       QDateTime::currentSecsSinceEpoch();
            wait = qBound(qint64(1), wait > 0 ? wait : 30, qint64(86400));
            m_retryAfter = QDateTime::currentSecsSinceEpoch() + wait;
            if (status == 503 && wait <= 5 && m_retries > 0) {
                --m_retries;
                // Signature lookup may invoke LRCLIB's background fetcher. If it is
                // busy, use the queued catalog search after the requested delay.
                if (!reply->url().path().endsWith("/get") || m_requests.isEmpty())
                    m_requests.prepend(reply->url());
                emit progress(tr("歌词服务繁忙，%1 秒后重试…").arg(wait));
                QTimer::singleShot(int(wait * 1000), this, [this, token] { next(token); });
                return;
            }
            m_error = tr("歌词服务繁忙或限流，请在 %1 秒后重试").arg(wait);
            m_requests.clear();
        } else if (reply->error() == QNetworkReply::NoError) {
            QJsonParseError error;
            const auto document = QJsonDocument::fromJson(reply->readAll(), &error);
            if (error.error != QJsonParseError::NoError || (!document.isArray() && !document.isObject()))
                m_error = tr("歌词服务返回了无效数据");
            else {
                const auto array = document.isArray() ? document.array() : QJsonArray{document.object()};
                for (const auto &item : array)
                    if (item.isObject())
                        m_candidates << item.toObject().toVariantMap();
            }
        } else if (status != 404) {
            m_error = tr("在线歌词连接失败：%1").arg(reply->errorString());
            if (!status)
                m_requests.clear();
        }
        if (m_requests.isEmpty())
            finish();
        else
            QTimer::singleShot(300, this, [this, token] { next(token); });
    });
}
void OnlineLyricsSearch::finish() {
    emit finished(ranked(m_track, m_candidates), m_error);
}
