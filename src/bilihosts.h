#pragma once
#include <QString>
#include <QUrl>

// Host checks for cover images and yt-dlp audio URLs.
class BiliHosts {
  public:
    // Empty input returns empty. Protocol-relative and http covers on an allowed host are
    // rewritten to https. Anything else returns empty and, when reason is set, a short message.
    static QString coverUrl(const QString &raw, QString *reason = nullptr);
    // Audio must already be https on a known Bilibili media CDN. reason is set when rejected.
    static bool audioUrl(const QUrl &url, QString *reason = nullptr);
};
