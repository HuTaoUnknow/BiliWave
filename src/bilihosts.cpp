#include "bilihosts.h"
#include <QHostAddress>

namespace {
QString label(bool audio) {
    return audio ? QString("音轨地址") : QString("封面地址");
}
bool domainOf(const QString &host, const char *root) {
    const QString name = QString::fromLatin1(root);
    return host == name || host.endsWith("." + name);
}
bool mediaHost(const QString &host) {
    // bilivideo.* is the domestic VOD/PCDN family. acgvideo.com is the older video CDN.
    // akamaized.net is shared by many sites; only Bilibili's upos- mirrors belong here.
    if (domainOf(host, "bilivideo.com") || domainOf(host, "bilivideo.cn") ||
        domainOf(host, "bilivideo.net") || domainOf(host, "acgvideo.com"))
        return true;
    return host.startsWith("upos-") && host.endsWith(".akamaized.net");
}
bool imageHost(const QString &host) {
    return domainOf(host, "hdslb.com") || domainOf(host, "biliimg.com");
}
bool reject(QString *reason, const QString &text) {
    if (reason)
        *reason = text;
    return false;
}
bool acceptable(const QUrl &url, bool audio, QString *reason) {
    const auto what = label(audio);
    if (!url.isValid() || url.host().isEmpty() || url.scheme().isEmpty())
        return reject(reason, what + "无效");
    if (url.scheme() != "https")
        return reject(reason, what + "必须使用 HTTPS");
    if (!url.userInfo().isEmpty())
        return reject(reason, what + "不能携带登录信息");
    if (url.port() != -1 && url.port() != 443)
        return reject(reason, what + "的端口不被允许");
    const auto host = url.host().toLower();
    const bool ip = QHostAddress(host).protocol() != QAbstractSocket::UnknownNetworkLayerProtocol;
    if (ip || host == "localhost" || host.endsWith(".localhost"))
        return reject(reason, what + "不能指向 IP 或本机");
    const bool allowed = audio ? mediaHost(host) : (mediaHost(host) || imageHost(host));
    if (allowed) {
        if (reason)
            reason->clear();
        return true;
    }
    return reject(reason, QString("%1的主机不在哔哩哔哩 CDN 白名单内（%2）").arg(what, host));
}
} // namespace

QString BiliHosts::coverUrl(const QString &raw, QString *reason) {
    const auto value = raw.trimmed();
    if (value.isEmpty()) {
        if (reason)
            reason->clear();
        return {};
    }
    auto text = value;
    if (text.startsWith("//"))
        text = "https:" + text;
    else if (text.startsWith("http://", Qt::CaseInsensitive))
        text.replace(0, 7, "https://");
    const QUrl parsed(text, QUrl::StrictMode);
    if (!acceptable(parsed, false, reason))
        return {};
    QUrl cleaned = parsed;
    cleaned.setScheme("https");
    cleaned.setUserInfo(QString());
    if (cleaned.port() == 443)
        cleaned.setPort(-1);
    return cleaned.toString(QUrl::FullyEncoded);
}
bool BiliHosts::audioUrl(const QUrl &url, QString *reason) {
    return acceptable(url, true, reason);
}
