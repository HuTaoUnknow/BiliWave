#include "searchservice.h"
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QTextDocumentFragment>
#include <QUrlQuery>
#include <QUuid>
#include <cmath>
static QString imageUrl(QString value) {
    if (value.startsWith("//"))
        value = "https:" + value;
    if (value.startsWith("http:"))
        value.replace(0, 5, "https:");
    return value;
}
SearchService::SearchService(QSettings *s, QObject *p)
    : QObject(p), m_settings(s), m_buvid(QUuid::createUuid().toString(QUuid::WithoutBraces) + "infoc") {}
SearchService::~SearchService() {
    ++m_token;
    if (m_active) {
        m_active->disconnect(this);
        m_active->abort();
        m_active = nullptr;
    }
}
SearchPage SearchService::parseSearch(const QJsonObject &data, int page) {
    SearchPage result;
    result.page = page;
    for (const auto &v : data.value("result").toArray()) {
        auto row = v.toObject();
        const auto bvid = row.value("bvid").toString();
        if (bvid.isEmpty())
            continue;
        qint64 seconds = 0;
        for (const auto &part : row.value("duration").toString().split(':'))
            seconds = seconds * 60 + part.toLongLong();
        result.items << QVariantMap{
            {"bvid", bvid},
            {"url", "https://www.bilibili.com/video/" + bvid},
            {"title", QTextDocumentFragment::fromHtml(row.value("title").toString()).toPlainText()},
            {"duration", seconds * 1000},
            {"thumbnail", imageUrl(row.value("pic").toString())},
            {"uploader", row.value("author").toString()},
            {"part", 0}};
    }
    const int size = qMax(1, data.value("pagesize").toInt(20));
    result.hasNext =
        page < data.value("numPages").toInt(int(std::ceil(data.value("numResults").toDouble() / size)));
    return result;
}
SearchPage SearchService::parseVideo(const QJsonObject &data) {
    SearchPage result;
    result.video = true;
    const auto bvid = data.value("bvid").toString(), title = data.value("title").toString(),
               cover = imageUrl(data.value("pic").toString());
    const auto owner = data.value("owner").toObject().value("name").toString();
    const auto pages = data.value("pages").toArray();
    for (const auto &v : pages) {
        const auto p = v.toObject();
        const int index = p.value("page").toInt();
        if (index < 1 || bvid.isEmpty())
            continue;
        result.items << QVariantMap{
            {"bvid", bvid},
            {"url", QString("https://www.bilibili.com/video/%1?p=%2").arg(bvid).arg(index)},
            {"title", pages.size() > 1 ? title + " / " + p.value("part").toString() : title},
            {"duration", qint64(p.value("duration").toDouble() * 1000)},
            {"thumbnail", imageUrl(p.value("first_frame").toString(cover))},
            {"uploader", owner},
            {"part", index}};
    }
    if (result.items.isEmpty() && !bvid.isEmpty())
        result.items << QVariantMap{
            {"bvid", bvid},       {"url", "https://www.bilibili.com/video/" + bvid + "?p=1"},
            {"title", title},     {"duration", qint64(data.value("duration").toDouble() * 1000)},
            {"thumbnail", cover}, {"uploader", owner},
            {"part", 1}};
    return result;
}
void SearchService::cancel() {
    ++m_token;
    if (m_active) {
        m_active->abort();
        m_active = nullptr;
    }
    emit busyChanged(false);
}
void SearchService::search(const QString &input, int page) {
    const auto query = input.trimmed();
    if (query.isEmpty()) {
        emit message(tr("输入歌名、BV 号或视频链接"));
        return;
    }
    cancel();
    const auto token = m_token;
    emit busyChanged(true);
    auto match = QRegularExpression("BV[0-9A-Za-z]{10}").match(query);
    if (match.hasMatch()) {
        QUrl url("https://api.bilibili.com/x/web-interface/view");
        QUrlQuery params;
        params.addQueryItem("bvid", match.captured());
        url.setQuery(params);
        emit message(tr("正在获取视频及全部分 P…"));
        get(url, true, 1, token);
        return;
    }
    const QUrl link(query);
    if (link.host().compare("b23.tv", Qt::CaseInsensitive) == 0 && link.scheme() == "https") {
        emit message(tr("正在展开短链接…"));
        get(link, true, 1, token, true);
        return;
    }
    if (QRegularExpression("^av\\d+$", QRegularExpression::CaseInsensitiveOption).match(query).hasMatch()) {
        QUrl url("https://api.bilibili.com/x/web-interface/view");
        QUrlQuery params;
        params.addQueryItem("aid", query.mid(2));
        url.setQuery(params);
        get(url, true, 1, token);
        return;
    }
    if (query.startsWith("http", Qt::CaseInsensitive)) {
        emit busyChanged(false);
        emit message(tr("请输入哔哩哔哩视频链接或 BV 号"));
        return;
    }
    QUrl url("https://api.bilibili.com/x/web-interface/search/type");
    QUrlQuery params;
    params.addQueryItem("search_type", "video");
    params.addQueryItem("keyword", query);
    params.addQueryItem("page", QString::number(qMax(1, page)));
    params.addQueryItem("page_size", "20");
    params.addQueryItem("highlight", "0");
    url.setQuery(params);
    emit message(tr("正在搜索第 %1 页…").arg(page));
    get(url, false, page, token);
}
void SearchService::get(const QUrl &url, bool video, int page, quint64 token, bool shortLink) {
    QNetworkRequest request(url);
    request.setTransferTimeout(25000);
    request.setRawHeader(
        "User-Agent",
        "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/130.0.0.0 Safari/537.36");
    request.setRawHeader("Referer", "https://www.bilibili.com/");
    if (url.host() == "api.bilibili.com") {
        QStringList cookies;
        bool hasBuvid = false;
        QFile file(m_settings->value("download/cookies").toString());
        if (!file.fileName().isEmpty() && file.open(QIODevice::ReadOnly))
            for (auto line : QString::fromUtf8(file.readAll()).split('\n')) {
                if (line.startsWith("#HttpOnly_"))
                    line.remove(0, 10);
                else if (line.startsWith('#'))
                    continue;
                const auto f = line.trimmed().split('\t');
                if (f.size() < 7)
                    continue;
                auto domain = f[0];
                if (domain.startsWith('.'))
                    domain.remove(0, 1);
                if (domain != "bilibili.com" && domain != "api.bilibili.com")
                    continue;
                if (f[4] != "0" && f[4].toLongLong() < QDateTime::currentSecsSinceEpoch())
                    continue;
                if (f[5] == "buvid3")
                    hasBuvid = true;
                cookies << f[5] + "=" + f[6];
            }
        if (!hasBuvid)
            cookies << "buvid3=" + m_buvid;
        request.setRawHeader("Cookie", cookies.join("; ").toUtf8());
    }
    auto reply = m_network.get(request);
    m_active = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, video, page, token, shortLink] {
        reply->deleteLater();
        if (token != m_token)
            return;
        m_active = nullptr;
        if (shortLink && reply->error() == QNetworkReply::NoError) {
            const auto match = QRegularExpression("BV[0-9A-Za-z]{10}").match(reply->url().toString());
            if (match.hasMatch()) {
                search(match.captured());
                return;
            }
        }
        emit busyChanged(false);
        if (reply->error() != QNetworkReply::NoError) {
            emit message(tr("搜索失败：%1；可重试或在设置中配置本机 Cookie").arg(reply->errorString()));
            return;
        }
        const auto root = QJsonDocument::fromJson(reply->readAll()).object();
        if (!root.contains("code") || root.value("code").toInt() != 0) {
            emit message(tr("搜索服务暂不可用：%1（%2）")
                             .arg(root.value("message").toString(tr("响应不是有效数据")))
                             .arg(root.value("code").toInt()));
            return;
        }
        const auto result = video ? parseVideo(root.value("data").toObject())
                                  : parseSearch(root.value("data").toObject(), page);
        emit results(result.items, result.page, result.hasNext, result.video);
        emit message(result.items.isEmpty() ? tr("没有找到结果")
                                            : tr("已找到 %1 项%2")
                                                  .arg(result.items.size())
                                                  .arg(result.video ? tr("，分 P 可分别下载") : QString()));
    });
}
