#include "services.h"
#include "features.h"
#include "lrc.h"
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QUrlQuery>
#include <QUuid>
#include <algorithm>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

QString resourceRoot() {
    QDir dir(QCoreApplication::applicationDirPath());
    for (int i = 0; i < 3; ++i) {
        if (dir.exists("tools"))
            return dir.absolutePath();
        dir.cdUp();
    }
    return QCoreApplication::applicationDirPath();
}
QString toolPath(QSettings *s, const QString &name) {
    auto configured = s->value("tools/" + name).toString();
    if (!configured.isEmpty())
        return configured;
    const auto local = QDir(resourceRoot()).filePath("tools/" + name + ".exe");
    if (QFileInfo::exists(local))
        return local;
    return QStandardPaths::findExecutable(name);
}
static QString cleanError(QByteArray bytes) {
    QString text = QString::fromUtf8(bytes.right(1800));
    text.replace(QRegularExpression("https?://[^\\s]+"), "[媒体地址]");
    return text.trimmed();
}
ImportService::ImportService(Library *l, QSettings *s, QObject *p) : QObject(p), m_library(l), m_settings(s) {
#ifdef Q_OS_WIN
    auto handle = CreateJobObjectW(nullptr, nullptr);
    if (handle) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (SetInformationJobObject(handle, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
            m_jobHandle = handle;
        else
            CloseHandle(handle);
    }
    connect(&m_process, &QProcess::started, this, [this] {
        auto child = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(m_process.processId()));
        if (child) {
            if (m_jobHandle)
                AssignProcessToJobObject(HANDLE(m_jobHandle), child);
            CloseHandle(child);
        }
        if (m_cancel)
            stopProcessTree();
    });
#endif
    m_timeout.setSingleShot(true);
    connect(&m_timeout, &QTimer::timeout, this, [this] {
        m_errors += "\n任务超时";
        stopProcessTree();
    });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] {
        auto b = m_process.readAllStandardOutput();
        m_output += b;
        if (m_stage == Download && !m_cancel) {
            auto str = QString::fromUtf8(b);
            auto lines = str.split('\n', Qt::SkipEmptyParts);
            if (!lines.isEmpty())
                emit status(m_task.id, m_title, lines.last().trimmed());
            if (m_output.size() > 65536)
                m_output = m_output.right(32768);
        }
    });
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
        m_errors += m_process.readAllStandardError();
        if (m_errors.size() > 8192)
            m_errors = m_errors.right(8192);
    });
    connect(&m_process, &QProcess::finished, this, &ImportService::finished);
    connect(&m_process, &QProcess::errorOccurred, this, [this](auto error) {
        if (error == QProcess::FailedToStart)
            fail(tr("无法启动工具：%1").arg(m_process.errorString()));
    });
}
ImportService::~ImportService() {
    m_process.disconnect(this);
    if (m_process.state() != QProcess::NotRunning) {
        m_cancel = true;
        stopProcessTree();
        m_process.waitForFinished(3000);
    }
    if (m_cancel)
        cleanupCancelledFiles();
#ifdef Q_OS_WIN
    if (m_jobHandle)
        CloseHandle(HANDLE(m_jobHandle));
#endif
}
void ImportService::enqueue(const QString &input, bool staged) {
    QString normalized = input.trimmed();
    if (normalized.isEmpty())
        return;
    if (normalized.startsWith("BV"))
        normalized = "https://www.bilibili.com/video/" + normalized;
    if (!QFileInfo::exists(normalized)) {
        const QUrl url(normalized);
        const auto host = url.host().toLower();
        if (url.scheme() != "https" ||
            !(host == "bilibili.com" || host.endsWith(".bilibili.com") || host == "b23.tv")) {
            emit status(QUuid::createUuid().toString(), input,
                        tr("失败：请输入本地文件或 HTTPS 哔哩哔哩链接"));
            return;
        }
    }
    Task task{QUuid::createUuid().toString(QUuid::WithoutBraces), normalized, staged};
    m_pending.enqueue(task);
    emit status(task.id, input, tr("等待中"));
    if (!m_busy)
        startNext();
}
void ImportService::startNext() {
    if (m_busy || m_pending.isEmpty())
        return;
    m_task = m_pending.dequeue();
    m_busy = true;
    m_cancel = false;
    m_track.clear();
    m_title = m_task.input;
    m_artist.clear();
    m_format.clear();
    m_raw.clear();
    m_playable.clear();
    m_outputDirectory.clear();
    if (QFileInfo::exists(m_task.input)) {
        m_raw = QFileInfo(m_task.input).absoluteFilePath();
        m_title = QFileInfo(m_raw).completeBaseName();
        emit status(m_task.id, m_title, tr("检查本地音频"));
        run(toolPath(m_settings, "ffprobe"),
            {"-v", "error", "-show_format", "-show_streams", "-of", "json", m_raw}, Probe);
        return;
    }
    emit status(m_task.id, m_title, tr("解析可访问音轨"));
    QStringList args = {"--ignore-config",    "--no-playlist",
                        "--dump-single-json", "--skip-download",
                        "--socket-timeout",   "25",
                        "--retries",          "2"};
    auto cookie = m_settings->value("download/cookies").toString();
    if (!cookie.isEmpty())
        args << "--cookies" << cookie;
    args << "--" << m_task.input;
    run(toolPath(m_settings, "yt-dlp"), args, Metadata);
}
void ImportService::run(const QString &program, const QStringList &args, Stage stage, int timeout) {
    m_stage = stage;
    m_output.clear();
    m_errors.clear();
    if (program.isEmpty() || !QFileInfo::exists(program)) {
        fail(tr("缺少工具，请在设置中配置：%1")
                 .arg(program.isEmpty() ? "yt-dlp / ffmpeg / ffprobe" : program));
        return;
    }
    m_process.setProgram(program);
    m_process.setArguments(args);
    m_process.start();
    m_timeout.start(timeout);
}
void ImportService::finished(int code, QProcess::ExitStatus exit) {
    m_timeout.stop();
    m_output += m_process.readAllStandardOutput();
    m_errors += m_process.readAllStandardError();
    if (m_cancel) {
        fail(tr("已取消"));
        return;
    }
    if (code || exit != QProcess::NormalExit) {
        fail(cleanError(m_errors).isEmpty() ? tr("媒体处理失败（%1）").arg(code) : cleanError(m_errors));
        return;
    }
    const auto obj = QJsonDocument::fromJson(m_output).object();
    if (m_stage == Metadata) {
        if (obj.isEmpty()) {
            fail(tr("解析器没有返回有效元数据"));
            return;
        }
        m_title = obj.value("title").toString();
        m_artist = obj.value("artist").toString();
        double best = -1;
        QJsonObject selected;
        for (const auto &v : obj.value("formats").toArray()) {
            const auto f = v.toObject();
            const auto ac = f.value("acodec").toString();
            if (f.value("vcodec").toString() != "none" || ac.isEmpty() || ac == "none")
                continue;
            double score = (ac.contains("flac", Qt::CaseInsensitive) || ac == "alac" ? 1000000000. : 0.) +
                           f.value("abr").toDouble(f.value("tbr").toDouble()) * 1000 +
                           f.value("asr").toDouble() / 100000;
            if (score > best) {
                best = score;
                selected = f;
            }
        }
        if (selected.isEmpty()) {
            fail(tr("没有可访问的独立音频流"));
            return;
        }
        m_format = selected.value("format_id").toString();
        auto root = m_settings->value("download/directory", QDir(m_library->directory()).filePath("media"))
                        .toString();
        QDir().mkpath(root);
        m_outputDirectory = QDir(root).absolutePath();
        const auto ext = selected.value("ext").toString();
        const bool mp4 = ext == "m4a" || ext == "mp4" || ext == "m4s";
        m_raw = QDir(root).filePath(m_task.id + ".source." + (mp4 ? "m4s" : ext));
        QStringList args = {
            "--ignore-config",
            "--no-playlist",
            "--no-overwrites",
            "--fixup",
            "never",
            "--newline",
            "--progress",
            "--progress-template",
            "download:下载 %(progress._percent_str)s · %(progress._speed_str)s · 剩余 %(progress._eta_str)s",
            "--socket-timeout",
            "25",
            "--retries",
            "3",
            "-f",
            m_format,
            "-o",
            m_raw};
        auto cookie = m_settings->value("download/cookies").toString();
        if (!cookie.isEmpty())
            args << "--cookies" << cookie;
        args << "--" << m_task.input;
        emit status(m_task.id, m_title, tr("下载原始音轨：%1").arg(selected.value("acodec").toString()));
        run(toolPath(m_settings, "yt-dlp"), args, Download, 1800000);
        return;
    }
    if (m_stage == Download) {
        run(toolPath(m_settings, "ffprobe"),
            {"-v", "error", "-show_format", "-show_streams", "-of", "json", m_raw}, Probe);
        return;
    }
    if (m_stage == Probe) {
        QJsonObject audio;
        for (const auto &v : obj.value("streams").toArray())
            if (v.toObject().value("codec_type") == "audio") {
                audio = v.toObject();
                break;
            }
        if (audio.isEmpty()) {
            fail(tr("文件没有可识别的音频轨道"));
            return;
        }
        const auto format = obj.value("format").toObject();
        const auto tags = format.value("tags").toObject();
        const auto codec = audio.value("codec_name").toString();
        if (QFileInfo::exists(m_task.input)) {
            if (!tags.value("title").toString().isEmpty())
                m_title = tags.value("title").toString();
            m_artist = tags.value("artist").toString();
        }
        m_track = {{"title", m_title},
                   {"artist", m_artist},
                   {"original", m_raw},
                   {"source", QFileInfo::exists(m_task.input) ? QStringLiteral("") : m_task.input},
                   {"codec", codec},
                   {"bitrate", audio.value("bit_rate").toVariant().toLongLong()},
                   {"sample_rate", audio.value("sample_rate").toVariant().toInt()},
                   {"channels", audio.value("channels").toInt()},
                   {"duration", qint64(format.value("duration").toString().toDouble() * 1000)}};
        if (QFileInfo::exists(m_task.input) && QFileInfo(m_raw).suffix().toLower() != "m4s") {
            m_playable = m_raw;
            complete();
            return;
        }
        const QString ext = codec == "flac"                     ? "flac"
                            : codec == "aac" || codec == "alac" ? "m4a"
                            : codec == "opus"                   ? "opus"
                                                                : "mka";
        m_playable = QFileInfo(m_raw).absolutePath() + "/" + m_task.id + "." + ext;
        m_outputDirectory = QFileInfo(m_playable).absolutePath();
        emit status(m_task.id, m_title, tr("保留原轨，直接封装为 %1").arg(ext.toUpper()));
        run(toolPath(m_settings, "ffmpeg"),
            {"-hide_banner", "-nostdin", "-v", "error", "-n", "-i", m_raw, "-map", "0:a:0", "-vn", "-c:a",
             "copy", m_playable},
            Remux, 300000);
        return;
    }
    if (m_stage == Remux)
        complete();
}
void ImportService::complete() {
    m_track["path"] = m_playable;
    m_track["staged"] = m_task.staged ? 1 : 0;
    const auto id = m_library->addTrack(m_track);
    if (!id) {
        fail(tr("写入歌曲库失败"));
        return;
    }
    if (!m_task.staged)
        m_library->commitTrack(id);
    // A local sidecar is used only when it contains a real time axis.
    const auto sidecar = QFileInfo(m_raw).absolutePath() + "/" + QFileInfo(m_raw).completeBaseName() + ".lrc";
    QFile lrc(sidecar);
    if (recognitionFeaturesEnabled() && lrc.open(QIODevice::ReadOnly)) {
        auto text = QString::fromUtf8(lrc.readAll());
        if (!Lrc::parse(text).isEmpty())
            m_library->setLyrics(id, text, "本地 LRC");
    }
    emit status(m_task.id, m_title,
                tr("完成 · %1 · 原轨已保留").arg(m_track.value("codec").toString().toUpper()));
    emit imported(id);
    emit completed(m_task.id, id);
    m_busy = false;
    QTimer::singleShot(0, this, &ImportService::startNext);
}
void ImportService::fail(const QString &message) {
    m_timeout.stop();
    if (!m_busy)
        return;
    if (m_cancel)
        cleanupCancelledFiles();
    emit status(m_task.id, m_title, tr("%1：%2").arg(m_cancel ? "取消" : "失败", message));
    if (!m_cancel)
        m_failed << m_task;
    m_busy = false;
    QTimer::singleShot(0, this, &ImportService::startNext);
}
void ImportService::cancel() {
    while (!m_pending.isEmpty()) {
        auto t = m_pending.dequeue();
        emit status(t.id, t.input, tr("已取消"));
    }
    if (m_busy) {
        m_cancel = true;
        stopProcessTree();
    }
}
void ImportService::cancelTask(const QString &id) {
    for (int i = m_pending.size() - 1; i >= 0; --i)
        if (m_pending[i].id == id)
            m_pending.removeAt(i);
    for (int i = m_failed.size() - 1; i >= 0; --i)
        if (m_failed[i].id == id)
            m_failed.removeAt(i);
    if (m_busy && m_task.id == id) {
        m_cancel = true;
        stopProcessTree();
    }
    emit status(id, {}, tr("已取消"));
}
void ImportService::stopProcessTree() {
#ifdef Q_OS_WIN
    if (m_jobHandle)
        TerminateJobObject(HANDLE(m_jobHandle), 1);
#endif
    m_process.kill();
}
void ImportService::cleanupCancelledFiles() {
    if (m_outputDirectory.isEmpty() || m_task.id.isEmpty())
        return;
    const QDir directory(m_outputDirectory);
    const auto root = QFileInfo(directory.absolutePath()).canonicalFilePath();
    if (root.isEmpty())
        return;
    const auto source = QFileInfo(m_task.input).canonicalFilePath();
    for (const auto &file : directory.entryInfoList(QDir::Files | QDir::Hidden | QDir::NoSymLinks)) {
        if (!file.fileName().startsWith(m_task.id + ".") ||
            QFileInfo(file.absolutePath()).canonicalFilePath() != root || file.canonicalFilePath() == source)
            continue;
        QFile::remove(file.absoluteFilePath());
    }
}
void ImportService::retryFailed() {
    auto failed = m_failed;
    m_failed.clear();
    for (const auto &task : failed)
        enqueue(task.input, task.staged);
}

LyricService::LyricService(Library *l, QSettings *s, QObject *p) : QObject(p), m_library(l), m_settings(s) {
    connect(&m_online, &OnlineLyricsSearch::progress, this, &LyricService::message);
    connect(&m_online, &OnlineLyricsSearch::finished, this,
            [this](const QList<QVariantMap> &options, const QString &error) {
                if (!m_onlinePending)
                    return;
                m_onlinePending = false;
                emit activityChanged(false);
                if (m_library->track(m_onlineTrack).isEmpty())
                    return;
                if (!options.isEmpty()) {
                    emit candidates(m_onlineTrack, options);
                    if (options.first().value("automatic").toBool())
                        useCandidate(m_onlineTrack, options.first());
                    else
                        emit message(
                            error.isEmpty()
                                ? tr("找到 %1 个版本，按匹配度排序，请确认时间轴。").arg(options.size())
                                : error);
                } else
                    emit message(error.isEmpty() ? tr("未找到歌词，可使用更简短的歌名或填写真实歌手重试。")
                                                 : error);
            });
}
LyricService::~LyricService() = default;
bool LyricService::importFile(qint64 id, const QString &path) {
    if (!recognitionFeaturesEnabled())
        return false;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        emit message(tr("无法打开歌词文件"));
        return false;
    }
    auto text = QString::fromUtf8(file.readAll());
    if (Lrc::parse(text).isEmpty()) {
        emit message(tr("没有找到 LRC 时间标签，请使用 UTF-8 时间轴歌词"));
        return false;
    }
    const bool ok = m_library->setLyrics(id, text, "本地 LRC");
    if (ok)
        emit message(tr("已载入本地时间轴歌词"));
    return ok;
}
void LyricService::fetch(qint64 id, bool force, const QString &keywords) {
    m_online.cancel();
    if (!lyricFeaturesEnabled())
        return;
    ++m_request;
    m_onlinePending = false;
    emit activityChanged(matching());
    const auto request = m_request;
    const auto t = m_library->track(id);
    if (t.isEmpty())
        return;
    if (!force && !Lrc::parse(t.value("lrc").toString()).isEmpty())
        return;
    const auto source = m_settings->value("lyrics/source", "auto").toString();
    if (source == "local") {
        emit message(tr("当前仅使用本地歌词，请导入 LRC"));
        return;
    }
    if (source == "offline") {
        emit message(tr("请选择同源视频字幕识别，或导入已有歌词。"));
        return;
    }
    Q_UNUSED(request)
    fetchOnline(id, keywords);
}
void LyricService::fetchOnline(qint64 id, const QString &keywords) {
    m_onlinePending = true;
    m_onlineTrack = id;
    emit activityChanged(true);
    m_online.search(m_library->track(id), keywords);
}
void LyricService::useCandidate(qint64 id, const QVariantMap &c) {
    if (!Lrc::parse(c.value("syncedLyrics").toString()).isEmpty()) {
        const auto source = c.value("sourceUrl").toString();
        m_library->setLyrics(id, c.value("syncedLyrics").toString(),
                             source.isEmpty() ? QString("LRCLIB") : "LRCLIB · " + source);
        emit message(tr("已保存 LRCLIB 时间轴歌词"));
    }
}
void LyricService::cancel() {
    m_online.cancel();
    ++m_request;
    m_onlinePending = false;
    emit activityChanged(false);
}

struct AudioExportService::State {
    explicit State(const QString &directory) : work(QDir(directory).filePath("export-XXXXXX")) {}
    QTemporaryDir work;
    std::unique_ptr<Library> library;
    std::unique_ptr<QSettings> settings;
    std::unique_ptr<ImportService> download;
    Track track;
    QString destination, audio, lrcPath, lrc, temporaryAudio;
};
AudioExportService::AudioExportService(Library *library, QSettings *settings, QObject *parent)
    : QObject(parent), m_library(library), m_settings(settings) {
    connect(&m_process, &QProcess::errorOccurred, this, [this](auto error) {
        if (error == QProcess::FailedToStart)
            finish(false, "无法启动导出工具");
    });
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        if (!m_state)
            return;
        if (code || status != QProcess::NormalExit) {
            finish(false, "导出失败：" + cleanError(m_process.readAllStandardError()));
            return;
        }
        if (QFile::exists(m_state->audio) || QFile::exists(m_state->lrcPath)) {
            finish(false, "目标歌曲或同名 LRC 已存在，请更换导出名称");
            return;
        }
        if (!QFile::copy(m_state->temporaryAudio, m_state->audio)) {
            finish(false, "无法写入导出歌曲");
            return;
        }
        QSaveFile lyrics(m_state->lrcPath);
        if (!lyrics.open(QIODevice::WriteOnly) || lyrics.write(m_state->lrc.toUtf8()) < 0 ||
            !lyrics.commit()) {
            QFile::remove(m_state->audio);
            finish(false, "无法写入同名歌词文件");
            return;
        }
        finish(true);
    });
}
AudioExportService::~AudioExportService() {
    m_process.disconnect(this);
    if (m_process.state() != QProcess::NotRunning) {
        m_process.kill();
        m_process.waitForFinished(3000);
    }
    m_state.reset();
}
void AudioExportService::exportTrack(qint64 id, const QString &destination) {
    if (busy()) {
        emit message("已有导出任务，请等待完成");
        return;
    }
    const auto track = m_library->track(id);
    auto lines = Lrc::parse(track.value("lrc").toString());
    if (track.isEmpty() || lines.isEmpty()) {
        emit finished(false, {}, {}, "这首歌还没有时间轴歌词，请先配置歌词再导出");
        return;
    }
    m_state = std::make_shared<State>(m_library->directory());
    if (!m_state->work.isValid()) {
        finish(false, "无法创建导出临时目录");
        return;
    }
    m_state->track = track;
    m_state->destination = destination;
    for (auto &line : lines)
        line.ms -= track.value("lyric_offset").toInt();
    m_state->lrc = Lrc::serialize(lines);
    if (track.value("storage").toString() != "url") {
        prepareAudio(track.value("path").toString());
        return;
    }
    // A private library keeps an exported URL download out of the user's library and tasks.
    m_state->library = std::make_unique<Library>(m_state->work.filePath("data"));
    m_state->settings =
        std::make_unique<QSettings>(m_state->work.filePath("settings.ini"), QSettings::IniFormat);
    for (const auto &key : m_settings->allKeys())
        m_state->settings->setValue(key, m_settings->value(key));
    m_state->settings->setValue("download/directory", m_state->work.filePath("media"));
    m_state->download = std::make_unique<ImportService>(m_state->library.get(), m_state->settings.get());
    connect(m_state->download.get(), &ImportService::imported, this, [this](qint64 trackId) {
        if (m_state)
            prepareAudio(m_state->library->track(trackId).value("path").toString());
    });
    connect(m_state->download.get(), &ImportService::status, this,
            [this](const QString &, const QString &, const QString &status) {
                emit message("导出：" + status);
                if (status.startsWith("失败") || status.startsWith("取消"))
                    finish(false, status);
            });
    m_state->download->enqueue(track.value("source").toString());
}
void AudioExportService::prepareAudio(const QString &path) {
    if (!m_state)
        return;
    const auto source = QFileInfo(path);
    if (!source.isFile()) {
        finish(false, "本地音频文件不存在");
        return;
    }
    const auto ext = source.suffix().toLower();
    auto audio = QFileInfo(m_state->destination).absoluteFilePath();
    if (QFileInfo(audio).suffix().toLower() != ext)
        audio += "." + ext;
    m_state->audio = audio;
    m_state->lrcPath = QFileInfo(audio).absolutePath() + "/" + QFileInfo(audio).completeBaseName() + ".lrc";
    if (!QFileInfo(QFileInfo(audio).absolutePath()).isDir() || QFile::exists(audio) ||
        QFile::exists(m_state->lrcPath)) {
        finish(false, "目标目录无效或歌曲、同名 LRC 已存在，请更换导出名称");
        return;
    }
    auto escape = [](QString value) {
        value.replace("\\", "\\\\");
        value.replace("=", "\\=");
        value.replace(";", "\\;");
        value.replace("#", "\\#");
        value.replace('\r', "");
        value.replace("\n", "\\\n");
        return value;
    };
    const auto metadataPath = m_state->work.filePath("lyrics.ffmeta");
    QFile metadata(metadataPath);
    if (!metadata.open(QIODevice::WriteOnly)) {
        finish(false, "无法准备歌词元数据：" + metadata.errorString());
        return;
    }
    metadata.write((";FFMETADATA1\ntitle=" + escape(m_state->track.value("title").toString()) +
                    "\nartist=" + escape(m_state->track.value("artist").toString()) +
                    "\nlyrics=" + escape(m_state->lrc) + "\n")
                       .toUtf8());
    metadata.close();
    m_state->temporaryAudio = m_state->work.filePath("export." + ext);
    emit message("正在导出歌曲及歌词（保留原音轨）");
    m_process.start(toolPath(m_settings, "ffmpeg"),
                    {"-hide_banner", "-nostdin", "-v", "error", "-n", "-i", path, "-f", "ffmetadata", "-i",
                     metadataPath, "-map", "0:a:0", "-map_metadata", "1", "-c:a", "copy",
                     m_state->temporaryAudio});
}
void AudioExportService::finish(bool ok, const QString &error) {
    if (!m_state)
        return;
    auto state = m_state;
    m_state.reset();
    emit finished(ok, ok ? state->audio : QString(), ok ? state->lrcPath : QString(), error);
    // ImportService can still be emitting a completion/status signal on this stack.
    QTimer::singleShot(0, this, [state] {});
}
