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
#include <QSettings>
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
    // Video/game headings are not the song title (e.g. 《明日方舟》EP - 无名策).
    const auto ep = QRegularExpression("(?:EP|OST)\\s*[-—–:：]\\s*(.+)$",
                                      QRegularExpression::CaseInsensitiveOption).match(title);
    if (ep.hasMatch())
        return ep.captured(1).trimmed();
    if (title.startsWith("明日方舟_"))
        return title.section('_', -1).trimmed();
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
        const auto key = c.value("provider", "LRCLIB").toString() + "\n" +
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
        if (!c.contains("provider"))
            c["provider"] = "LRCLIB";
        if (!c.contains("sourceUrl"))
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

namespace {
QString artists(const QJsonArray &values) {
    QStringList names;
    for (const auto &v : values) {
        const auto name = v.isString() ? v.toString() : v.toObject().value("name").toString();
        if (!name.isEmpty()) names << name;
    }
    return names.join(" / ");
}
QString unescapeLyrics(QString text) {
    text.replace("&#39;", "'").replace("&apos;", "'").replace("&quot;", "\"");
    text.replace("&lt;", "<").replace("&gt;", ">").replace("&amp;", "&");
    return text;
}
bool relevantSong(const QVariantMap &track, const QVariantMap &candidate, const QString &keywords) {
    auto title = OnlineLyricsSearch::normalized(OnlineLyricsSearch::cleanTitle(track.value("title").toString()));
    const auto name = OnlineLyricsSearch::normalized(candidate.value("trackName").toString());
    if (!keywords.isEmpty() && !keywords.startsWith("https://"))
        title = OnlineLyricsSearch::normalized(OnlineLyricsSearch::cleanTitle(keywords));
    if (title.isEmpty() || name.isEmpty()) return false;
    if (name.contains("instrumental") && !title.contains("instrumental")) return false;
    return similarity(title, name) >= .5 || (name.size() >= 3 && title.contains(name)) ||
           (title.size() >= 3 && name.contains(title));
}
}
QList<QPair<QString, QString>> LyricsSourcesSearch::sources() {
    return {{"msr", "塞壬官方"}, {"netease", "网易云音乐"}, {"qq", "QQ 音乐"},
            {"lrclib", "LRCLIB"}, {"kugou", "酷狗歌词"}, {"ovh", "Lyrics.ovh（纯文本）"}};
}
QString LyricsSourcesSearch::sourceName(const QString &id) {
    for (const auto &s : sources()) if (s.first == id) return s.second;
    return id;
}
QStringList LyricsSourcesSearch::selectedSources(QSettings *settings) {
    const QStringList defaults{"msr", "netease", "qq", "lrclib"};
    const auto selected = settings->value("lyrics/onlineSources", defaults).toStringList();
    QStringList enabled;
    for (const auto &entry : sources()) if (selected.contains(entry.first)) enabled << entry.first;
    const auto first = settings->value("lyrics/primaryOnlineSource", "msr").toString();
    if (enabled.removeAll(first)) enabled.prepend(first);
    return enabled;
}
LyricsSourcesSearch::LyricsSourcesSearch(QObject *parent, QMap<QString, QUrl> endpoints) : QObject(parent) {
    m_endpoints = {{"msr", QUrl("https://monster-siren.hypergryph.com/api/")},
                   {"netease", QUrl("https://music.163.com/api/")},
                   {"qq", QUrl("https://c.y.qq.com/")},
                   {"lrclib", QUrl("https://lrclib.net/api/")},
                   {"kugou", QUrl("https://lyrics.kugou.com/")},
                   {"ovh", QUrl("https://api.lyrics.ovh/v1/")}};
    for (auto it = endpoints.cbegin(); it != endpoints.cend(); ++it) m_endpoints[it.key()] = it.value();
    m_deadline.setSingleShot(true);
    connect(&m_deadline, &QTimer::timeout, this, [this] {
        finish(tr("本次查询已到 30 秒上限；已取得的结果保留，可稍后继续搜索。"));
    });
}
QUrl LyricsSourcesSearch::url(const QString &source, const QString &path,
                             const QList<QPair<QString, QString>> &parameters) const {
    auto result = m_endpoints.value(source).resolved(QUrl(path));
    QUrlQuery query;
    for (const auto &p : parameters) query.addQueryItem(p.first, p.second);
    result.setQuery(query);
    return result;
}
bool LyricsSourcesSearch::hasMore() const {
    for (int i = m_source; i < m_sources.size(); ++i)
        if (!m_sources[i].pending.isEmpty() || !m_sources[i].exhausted) return true;
    return false;
}
QVariantMap LyricsSourcesSearch::statistics() const {
    return {{"requests", m_requests}, {"bytes", m_bytes}, {"results", m_batch.size()},
            {"hasMore", hasMore()}, {"source", m_source < m_sources.size() ? m_sources[m_source].id : QString()}};
}
void LyricsSourcesSearch::cancel() {
    ++m_token;
    m_active = false;
    m_deadline.stop();
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply->deleteLater();
        m_reply = nullptr;
    }
}
void LyricsSourcesSearch::search(const QVariantMap &track, const QString &keywords, const QStringList &sources) {
    cancel();
    m_active = true;
    m_batch.clear();
    m_lastError.clear();
    m_requests = 0;
    m_bytes = 0;
    m_track = track;
    m_keywords = keywords.trimmed().left(300);
    QStringList valid;
    for (const auto &s : sources)
        if (m_endpoints.contains(s) && !valid.contains(s)) valid << s;
    const auto key = QString::fromUtf8(QJsonDocument(QJsonObject::fromVariantMap(
        {{"id", track.value("id")}, {"title", track.value("title")}, {"artist", track.value("artist")},
         {"duration", track.value("duration")}, {"keywords", m_keywords}, {"sources", valid}})).toJson(QJsonDocument::Compact));
    if (key != m_key) {
        m_key = key;
        m_sources.clear();
        m_seen.clear();
        m_source = 0;
        m_direct = QUrl();
        if (m_keywords.startsWith("https://", Qt::CaseInsensitive)) {
            const QUrl link(m_keywords);
            QString source;
            if (!link.userInfo().isEmpty()) valid.clear();
            else if (link.host() == "monster-siren.hypergryph.com" &&
                     QRegularExpression("^/music/\\d+/?$").match(link.path()).hasMatch()) source = "msr";
            else if ((link.host() == "lrclib.net" || link.host() == "www.lrclib.net") &&
                     QRegularExpression("^/(?:tracks|api/get)/\\d+/?$").match(link.path()).hasMatch()) source = "lrclib";
            if (valid.contains(source)) { valid = {source}; m_direct = link; }
            else valid.clear();
        }
        for (const auto &s : valid) m_sources << Source{s, {}, 0, false};
    }
    const auto token = m_token;
    m_deadline.start(30000);
    QTimer::singleShot(0, this, [this, token] {
        if (!m_active || token != m_token) return;
        const auto remaining = m_cooldown - QDateTime::currentMSecsSinceEpoch();
        if (remaining > 0) {
            finish(tr("歌词源暂时限流，请在 %1 秒后重试。").arg((remaining + 999) / 1000));
        } else if (m_sources.isEmpty()) {
            finish(tr("请选择至少一个歌词源；链接目前支持已启用的塞壬官网或 LRCLIB。"));
        } else advance(token);
    });
}
void LyricsSourcesSearch::request(quint64 token, const QUrl &address,
                                  std::function<void(const QByteArray &)> complete) {
    if (!m_active || token != m_token) return;
    // No retries, no overlapping requests, at most eight requests per explicit click.
    if (m_requests >= 8) {
        finish(tr("本次已达到 8 次请求上限；点击继续搜索可从当前位置继续。"));
        return;
    }
    const auto delay = m_nextRequest - QDateTime::currentMSecsSinceEpoch();
    if (delay > 0) {
        QTimer::singleShot(int(delay), this, [this, token, address, complete] { request(token, address, complete); });
        return;
    }
    ++m_requests;
    m_nextRequest = QDateTime::currentMSecsSinceEpoch() + 1000;
    emit progress(tr("%1 · 查询中（本次 %2/8 次请求）").arg(sourceName(m_sources[m_source].id)).arg(m_requests));
    QNetworkRequest req(address);
    req.setTransferTimeout(6000);
    req.setRawHeader("User-Agent", "BiliWave/" BILI_VERSION " (https://github.com/HuTaoUnknow/BiliWave)");
    const auto source = m_sources[m_source].id;
    if (source == "netease") req.setRawHeader("Referer", "https://music.163.com/");
    if (source == "qq") req.setRawHeader("Referer", "https://y.qq.com/");
    auto reply = m_network.get(req);
    m_reply = reply;
    connect(reply, &QIODevice::readyRead, this, [reply] {
        auto body = reply->property("body").toByteArray() + reply->readAll();
        if (body.size() > 2 * 1024 * 1024) {
            reply->setProperty("oversize", true);
            reply->abort();
        } else reply->setProperty("body", body);
    });
    connect(reply, &QNetworkReply::finished, this, [this, token, reply, complete] {
        reply->deleteLater();
        if (!m_active || token != m_token) return;
        m_reply = nullptr;
        const auto body = reply->property("body").toByteArray() + reply->readAll();
        m_bytes += body.size();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 429 || status == 503) {
            bool ok = false;
            auto after = reply->rawHeader("Retry-After").toLongLong(&ok);
            if (!ok) after = QDateTime::fromString(QString::fromLatin1(reply->rawHeader("Retry-After")), Qt::RFC2822Date).toSecsSinceEpoch() - QDateTime::currentSecsSinceEpoch();
            const auto seconds = qBound(qint64(60), after, qint64(86400));
            m_cooldown = QDateTime::currentMSecsSinceEpoch() + seconds * 1000;
            finish(tr("%1 返回限流/繁忙，已停止所有请求，%2 秒后再试。").arg(sourceName(m_sources[m_source].id)).arg(seconds));
        } else if (reply->error() != QNetworkReply::NoError || body.size() > 2 * 1024 * 1024 ||
                   reply->property("oversize").toBool()) {
            m_lastError = tr("%1 暂不可用").arg(sourceName(m_sources[m_source].id));
            m_sources[m_source].pending.clear();
            m_sources[m_source].exhausted = true;
            advance(token);
        } else complete(body);
    });
}
void LyricsSourcesSearch::enqueue(QVariantMap c) {
    if (!m_direct.isEmpty() || relevantSong(m_track, c, m_keywords))
        m_sources[m_source].pending << c;
}
void LyricsSourcesSearch::accept(QVariantMap c) {
    const auto key = c.value("provider").toString() + ":" + c.value("id").toString();
    c.remove("_accesskey");
    c.remove("_lyricUrl");
    if (!m_seen.contains(key) && (!c.value("syncedLyrics").toString().isEmpty() || !c.value("plainLyrics").toString().isEmpty())) {
        m_seen.insert(key);
        m_batch << c;
    }
}
void LyricsSourcesSearch::advance(quint64 token) {
    if (!m_active || token != m_token) return;
    if (m_batch.size() >= 5) { finish(); return; }
    while (m_source < m_sources.size()) {
        auto &s = m_sources[m_source];
        if (!s.pending.isEmpty()) {
            const auto c = s.pending.first();
            const auto key = c.value("provider").toString() + ":" + c.value("id").toString();
            if (m_seen.contains(key)) { s.pending.removeFirst(); continue; }
            if (c.contains("syncedLyrics") || c.contains("plainLyrics")) {
                s.pending.removeFirst();
                accept(c);
                if (m_batch.size() == 5) { finish(); return; }
            } else { loadLyrics(token, c); return; }
        } else {
            // A successful source ends this click; others are strictly fallbacks.
            if (!m_batch.isEmpty()) { finish(); return; }
            if (s.exhausted) { ++m_source; continue; }
            loadPage(token);
            return;
        }
    }
    finish(m_lastError);
}
void LyricsSourcesSearch::selectSirenSongs(quint64 token) {
    for (const auto &v : m_sirenCatalog) {
        const auto o = v.toObject();
        enqueue({{"id", o.value("cid").toString()}, {"trackName", o.value("name").toString()},
                 {"artistName", artists(o.value("artists").toArray())}, {"provider", sourceName("msr")},
                 {"sourceUrl", "https://monster-siren.hypergryph.com/music/" + o.value("cid").toString()}});
    }
    auto &pending = m_sources[m_source].pending;
    std::stable_sort(pending.begin(), pending.end(), [this](const auto &a, const auto &b) {
        const auto title = OnlineLyricsSearch::normalized(OnlineLyricsSearch::cleanTitle(m_track.value("title").toString()));
        return similarity(title, OnlineLyricsSearch::normalized(a.value("trackName").toString())) >
               similarity(title, OnlineLyricsSearch::normalized(b.value("trackName").toString()));
    });
    pending = pending.mid(0, 20);
    m_sources[m_source].exhausted = true;
    advance(token);
}
void LyricsSourcesSearch::loadPage(quint64 token) {
    auto &s = m_sources[m_source];
    const auto title = OnlineLyricsSearch::cleanTitle(m_track.value("title").toString());
    const auto artist = m_track.value("artist").toString().trimmed();
    const auto query = m_keywords.isEmpty() ? title : m_keywords;
    if (title.isEmpty()) { s.exhausted = true; advance(token); return; }
    if (s.id == "msr") {
        if (!m_direct.isEmpty()) {
            s.pending << QVariantMap{{"id", m_direct.path().split('/', Qt::SkipEmptyParts).last()}, {"provider", sourceName("msr")}};
            s.exhausted = true;
            advance(token);
        } else if (!m_sirenCatalog.isEmpty() && QDateTime::currentSecsSinceEpoch() - m_catalogTime < 86400) {
            selectSirenSongs(token);
        } else request(token, url("msr", "songs"), [this, token](const QByteArray &body) {
            const auto d = QJsonDocument::fromJson(body).object();
            if (d.value("code").toInt(-1) != 0) { m_sources[m_source].exhausted = true; advance(token); return; }
            m_sirenCatalog = d.value("data").toObject().value("list").toArray();
            m_catalogTime = QDateTime::currentSecsSinceEpoch();
            selectSirenSongs(token);
        });
        return;
    }
    if (s.id == "ovh") {
        s.exhausted = true;
        if (artist.isEmpty()) { advance(token); return; }
        request(token, url("ovh", QString::fromLatin1(QUrl::toPercentEncoding(artist)) + "/" + QString::fromLatin1(QUrl::toPercentEncoding(title))),
                [this, token, title, artist](const QByteArray &body) {
            const auto d = QJsonDocument::fromJson(body).object();
            accept({{"id", title + "/" + artist}, {"provider", sourceName("ovh")}, {"trackName", title},
                    {"artistName", artist}, {"plainLyrics", d.value("lyrics").toString()}, {"sourceUrl", "https://lyrics.ovh/"}});
            advance(token);
        });
        return;
    }
    QUrl address;
    if (s.id == "netease") address = url(s.id, "search/get", {{"s", query}, {"type", "1"}, {"limit", "5"}, {"offset", QString::number(s.page * 5)}});
    else if (s.id == "qq") address = url(s.id, "soso/fcgi-bin/client_search_cp", {{"w", query}, {"p", QString::number(s.page + 1)}, {"n", "5"}, {"format", "json"}});
    else if (s.id == "kugou") address = url(s.id, "search", {{"ver", "1"}, {"man", "yes"}, {"client", "pc"}, {"keyword", query + (artist.isEmpty() ? QString() : " " + artist)}, {"duration", m_track.value("duration").toString()}});
    else if (!m_direct.isEmpty()) address = url(s.id, "get/" + m_direct.path().split('/', Qt::SkipEmptyParts).last());
    else address = url(s.id, "search", {{m_keywords.isEmpty() ? "track_name" : "q", query}});
    request(token, address, [this, token](const QByteArray &body) {
        auto &s = m_sources[m_source];
        QJsonParseError error;
        const auto doc = QJsonDocument::fromJson(body, &error);
        const auto d = doc.object();
        if (error.error != QJsonParseError::NoError) { s.exhausted = true; advance(token); return; }
        if (d.value("code").toInt() == 429 || d.value("code").toInt() == 503) {
            m_cooldown = QDateTime::currentMSecsSinceEpoch() + 60000;
            finish(tr("%1 返回限流/繁忙，已停止请求，60 秒后再试。").arg(sourceName(s.id)));
            return;
        }
        QJsonArray rows;
        if (s.id == "netease") rows = d.value("result").toObject().value("songs").toArray();
        else if (s.id == "qq") rows = d.value("data").toObject().value("song").toObject().value("list").toArray();
        else if (s.id == "kugou") rows = d.value("candidates").toArray();
        else rows = doc.isArray() ? doc.array() : QJsonArray{d};
        ++s.page;
        s.exhausted = (s.id != "netease" && s.id != "qq") || rows.size() < 5 || s.page >= 10;
        for (const auto &v : rows) {
            const auto o = v.toObject();
            QVariantMap c;
            if (s.id == "netease") c = {{"id", o.value("id").toVariant().toString()}, {"trackName", o.value("name").toString()}, {"artistName", artists(o.value("artists").toArray())}, {"duration", o.value("duration").toDouble() / 1000.}};
            else if (s.id == "qq") c = {{"id", o.value("songmid").toString()}, {"trackName", o.value("songname").toString()}, {"artistName", artists(o.value("singer").toArray())}, {"duration", o.value("interval").toDouble()}};
            else if (s.id == "kugou") c = {{"id", o.value("id").toString()}, {"trackName", o.value("song").toString()}, {"artistName", o.value("singer").toString()}, {"duration", o.value("duration").toDouble() / 1000.}, {"_accesskey", o.value("accesskey").toString()}};
            else c = o.toVariantMap();
            if (c.value("id").toString().isEmpty()) continue;
            c["provider"] = sourceName(s.id);
            const auto id = c.value("id").toString();
            c["sourceUrl"] = s.id == "netease" ? "https://music.163.com/#/song?id=" + id
                              : s.id == "qq" ? "https://y.qq.com/n/ryqq/songDetail/" + id
                              : s.id == "kugou" ? "https://www.kugou.com/"
                                                : "https://lrclib.net/api/get/" + id;
            enqueue(c);
            if (s.pending.size() >= 50) break;
        }
        // Do not keep paging through unrelated results automatically.
        if (s.pending.isEmpty()) s.exhausted = true;
        advance(token);
    });
}
void LyricsSourcesSearch::loadLyrics(quint64 token, QVariantMap c) {
    const auto source = m_sources[m_source].id;
    const auto id = c.value("id").toString();
    if (source == "msr" && c.contains("_lyricUrl")) {
        request(token, QUrl(c.value("_lyricUrl").toString()), [this, token, c](const QByteArray &raw) mutable {
            c["syncedLyrics"] = QString::fromUtf8(raw).remove(QChar(0xfeff));
            m_sources[m_source].pending.removeFirst(); accept(c); advance(token);
        });
        return;
    }
    QUrl address;
    if (source == "msr") address = url(source, "song/" + id);
    else if (source == "netease") address = url(source, "song/lyric", {{"id", id}, {"lv", "-1"}, {"kv", "-1"}, {"tv", "-1"}});
    else if (source == "qq") address = url(source, "lyric/fcgi-bin/fcg_query_lyric_new.fcg", {{"songmid", id}, {"format", "json"}, {"nobase64", "1"}});
    else address = url(source, "download", {{"ver", "1"}, {"client", "pc"}, {"id", id}, {"accesskey", c.value("_accesskey").toString()}, {"fmt", "lrc"}, {"charset", "utf8"}});
    request(token, address, [this, token, c, source](const QByteArray &body) mutable {
        const auto d = QJsonDocument::fromJson(body).object();
        if (source == "msr") {
            const auto data = d.value("data").toObject();
            const QUrl lyric(data.value("lyricUrl").toString());
            // Only follow the publisher's HTTPS lyric CDN, never an arbitrary returned URL.
            const auto base = m_endpoints.value("msr");
            const bool fixture = base.host() == "127.0.0.1" && lyric.host() == base.host() &&
                                 lyric.port() == base.port() && lyric.scheme() == base.scheme();
            const bool official = lyric.scheme() == "https" && lyric.port(443) == 443 &&
                                  (lyric.host() == "web.hycdn.cn" || lyric.host().endsWith(".hycdn.cn")) &&
                                  lyric.path().startsWith("/siren/lyric/");
            if (!lyric.userInfo().isEmpty() || !(fixture || official)) {
                m_sources[m_source].pending.removeFirst(); advance(token); return;
            }
            c["trackName"] = data.value("name").toString();
            c["artistName"] = artists(data.value("artists").toArray());
            c["sourceUrl"] = "https://monster-siren.hypergryph.com/music/" + c.value("id").toString();
            // Keep the CDN URL with the pending item so a request-budget stop can resume it.
            c["_lyricUrl"] = lyric.toString();
            m_sources[m_source].pending[0] = c;
            request(token, lyric, [this, token, c](const QByteArray &raw) mutable {
                c["syncedLyrics"] = QString::fromUtf8(raw).remove(QChar(0xfeff));
                m_sources[m_source].pending.removeFirst(); accept(c); advance(token);
            });
            return;
        }
        QString lrc, translation;
        if (source == "netease") { lrc = d.value("lrc").toObject().value("lyric").toString(); translation = d.value("tlyric").toObject().value("lyric").toString(); }
        else if (source == "qq") { lrc = unescapeLyrics(d.value("lyric").toString()); translation = unescapeLyrics(d.value("trans").toString()); }
        else lrc = QString::fromUtf8(QByteArray::fromBase64(d.value("content").toString().toLatin1()));
        if (!Lrc::parse(lrc).isEmpty()) {
            if (!Lrc::parse(translation).isEmpty()) lrc += '\n' + translation;
            c["syncedLyrics"] = lrc;
        } else c["plainLyrics"] = lrc;
        m_sources[m_source].pending.removeFirst();
        accept(c);
        advance(token);
    });
}
void LyricsSourcesSearch::finish(const QString &error) {
    if (!m_active) return;
    const auto batch = OnlineLyricsSearch::ranked(m_track, m_batch).mid(0, 5);
    cancel();
    emit finished(batch, error);
}
