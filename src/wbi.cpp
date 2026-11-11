#include "wbi.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QUrlQuery>
#include <algorithm>
#include <utility>

namespace {
constexpr int kMixin[64] = {46, 47, 18, 2,  53, 8,  23, 32, 15, 50, 10, 31, 58, 3,  45, 35,
                            27, 43, 5,  49, 33, 9,  42, 19, 29, 28, 14, 39, 12, 38, 41, 13,
                            37, 48, 7,  16, 24, 55, 40, 61, 26, 17, 0,  1,  60, 51, 30, 4,
                            22, 25, 54, 21, 56, 59, 6,  63, 57, 62, 11, 36, 20, 34, 44, 52};
constexpr auto kUserAgent =
    "Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 Chrome/130.0.0.0 Safari/537.36";

QString filterValue(QString value) {
    for (const auto mark : {'!', '\'', '(', ')', '*'})
        value.remove(QLatin1Char(mark));
    return value;
}
QString encodeComponent(const QString &value) {
    return QString::fromLatin1(QUrl::toPercentEncoding(value));
}
} // namespace

WbiSigner::WbiSigner(QNetworkAccessManager *network, QObject *parent)
    : QObject(parent), m_network(network), m_navUrl(QString("https://api.bilibili.com/x/web-interface/nav")) {
}
WbiSigner::~WbiSigner() {
    ++m_generation;
    m_waiters.clear();
    if (m_reply) {
        m_reply->disconnect(this);
        m_reply->abort();
        m_reply = nullptr;
    }
}
QString WbiSigner::mixinKey(const QString &imgKey, const QString &subKey) {
    const auto raw = imgKey + subKey;
    QString mixed;
    mixed.reserve(32);
    for (const int index : kMixin) {
        if (index >= 0 && index < raw.size())
            mixed += raw.at(index);
    }
    return mixed.left(32);
}
QString WbiSigner::keyFromUrl(const QString &url) {
    const auto name = QUrl(url).fileName();
    const auto dot = name.lastIndexOf(QLatin1Char('.'));
    const auto key = dot > 0 ? name.left(dot) : name;
    if (key.size() < 16 || key.size() > 128)
        return {};
    for (const auto ch : key)
        if (!ch.isLetterOrNumber())
            return {};
    return key;
}
QString WbiSigner::signQuery(const QList<QPair<QString, QString>> &params, const QString &mixinKey,
                             qint64 wts) {
    QList<QPair<QString, QString>> items;
    for (const auto &param : params) {
        if (param.first == "w_rid" || param.first == "wts")
            continue;
        items.append({param.first, filterValue(param.second)});
    }
    items.append({"wts", QString::number(wts)});
    std::sort(items.begin(), items.end(),
              [](const auto &left, const auto &right) { return left.first < right.first; });
    QStringList encoded;
    for (const auto &item : items)
        encoded << encodeComponent(item.first) + "=" + encodeComponent(item.second);
    const auto query = encoded.join(QLatin1Char('&'));
    const auto rid = QCryptographicHash::hash((query + mixinKey).toUtf8(), QCryptographicHash::Md5).toHex();
    return query + "&w_rid=" + QString::fromLatin1(rid);
}
QUrl WbiSigner::signUrl(const QUrl &url, const QString &mixinKey, qint64 wts) {
    QList<QPair<QString, QString>> params;
    for (const auto &item : QUrlQuery(url).queryItems(QUrl::FullyDecoded))
        params.append(item);
    const auto query = signQuery(params, mixinKey, wts);
    return QUrl::fromEncoded(url.toEncoded(QUrl::RemoveQuery | QUrl::RemoveFragment) + "?" + query.toUtf8(),
                             QUrl::StrictMode);
}
bool WbiSigner::keysFromNav(const QByteArray &body, QString *imgKey, QString *subKey, QString *error) {
    const auto root = QJsonDocument::fromJson(body).object();
    const auto wbi = root.value("data").toObject().value("wbi_img").toObject();
    const auto img = keyFromUrl(wbi.value("img_url").toString());
    const auto sub = keyFromUrl(wbi.value("sub_url").toString());
    if (img.isEmpty() || sub.isEmpty()) {
        if (error) {
            const auto message = root.value("message").toString();
            *error = message.isEmpty() ? QString("导航接口没有返回签名口令") : message;
        }
        return false;
    }
    if (imgKey)
        *imgKey = img;
    if (subKey)
        *subKey = sub;
    if (error)
        error->clear();
    return true;
}
qint64 WbiSigner::timestamp() const {
    if (m_clock)
        return m_clock();
    return QDateTime::currentSecsSinceEpoch();
}
qint64 WbiSigner::expiryAfter(qint64 fetchedAt) {
    // Keys rotate daily. Keep them until the next 00:00 Asia/Shanghai (UTC+8, no DST).
    constexpr qint64 kUtc8 = 8 * 3600;
    constexpr qint64 kDay = 24 * 3600;
    const qint64 local = fetchedAt + kUtc8;
    return (local / kDay + 1) * kDay - kUtc8;
}
bool WbiSigner::cacheFresh(qint64 at) const {
    return !m_mixin.isEmpty() && at < m_expiresAt;
}
void WbiSigner::setClockForTest(std::function<qint64()> clock) {
    m_clock = std::move(clock);
}
void WbiSigner::setNavUrlForTest(const QUrl &url) {
    m_navUrl = url;
}
void WbiSigner::setKeysForTest(const QString &imgKey, const QString &subKey, qint64 fetchedAt) {
    m_mixin = mixinKey(imgKey, subKey);
    m_expiresAt = expiryAfter(fetchedAt);
}
void WbiSigner::invalidate() {
    ++m_generation;
    m_mixin.clear();
    m_expiresAt = 0;
    const auto waiters = std::exchange(m_waiters, {});
    if (m_reply) {
        auto reply = m_reply;
        m_reply = nullptr;
        reply->disconnect(this);
        reply->abort();
        reply->deleteLater();
    }
    for (const auto &waiter : waiters)
        waiter({}, QString("签名口令已失效"));
}
void WbiSigner::withMixinKey(const Callback &done) {
    if (cacheFresh(timestamp())) {
        done(m_mixin, {});
        return;
    }
    m_waiters.append(done);
    if (!m_reply)
        fetch();
}
void WbiSigner::fetch() {
    const int generation = m_generation;
    QNetworkRequest request(m_navUrl);
    request.setTransferTimeout(25000);
    request.setRawHeader("User-Agent", kUserAgent);
    request.setRawHeader("Referer", "https://www.bilibili.com/");
    auto reply = m_network->get(request);
    m_reply = reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply, generation] {
        reply->deleteLater();
        if (m_reply == reply)
            m_reply = nullptr;
        if (generation != m_generation)
            return;
        if (reply->error() != QNetworkReply::NoError) {
            const auto error = QString("导航接口请求失败：%1").arg(reply->errorString());
            qWarning().noquote() << "哔哩哔哩签名口令获取失败：" << error;
            finish({}, error);
            return;
        }
        QString img, sub, error;
        if (!keysFromNav(reply->readAll(), &img, &sub, &error)) {
            qWarning().noquote() << "哔哩哔哩签名口令获取失败：" << error;
            finish({}, error);
            return;
        }
        m_mixin = mixinKey(img, sub);
        m_expiresAt = expiryAfter(timestamp());
        finish(m_mixin, {});
    });
}
void WbiSigner::finish(const QString &mixin, const QString &error) {
    const auto waiters = std::exchange(m_waiters, {});
    for (const auto &waiter : waiters)
        waiter(mixin, error);
}
