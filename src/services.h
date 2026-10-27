#pragma once
#include "library.h"
#include "onlinelyrics.h"
#include <QNetworkAccessManager>
#include <QProcess>
#include <QQueue>
#include <QSettings>
#include <QTimer>
#include <memory>

QString resourceRoot();
QString toolPath(QSettings *settings, const QString &name);
class ImportService : public QObject {
    Q_OBJECT
  public:
    ImportService(Library *library, QSettings *settings, QObject *parent = nullptr);
    ~ImportService() override;
    void enqueue(const QString &input, bool staged = false);
    void cancel();
    void cancelTask(const QString &id);
    void retryFailed();
    bool busy() const {
        return m_busy;
    }
  signals:
    void status(const QString &id, const QString &title, const QString &state);
    void imported(qint64 id);
    void completed(const QString &taskId, qint64 trackId);

  private:
    struct Task {
        QString id, input;
        bool staged = false;
    };
    enum Stage { Metadata, Download, Probe, Remux };
    void startNext();
    void run(const QString &program, const QStringList &args, Stage stage, int timeout = 120000);
    void finished(int code, QProcess::ExitStatus exit);
    void fail(const QString &message);
    void complete();
    void cleanupCancelledFiles();
    void stopProcessTree();
    Library *m_library;
    QSettings *m_settings;
    QProcess m_process;
    QTimer m_timeout;
    QQueue<Task> m_pending;
    QList<Task> m_failed;
    Task m_task;
    Stage m_stage = Metadata;
    bool m_busy = false, m_cancel = false;
    QByteArray m_output, m_errors;
    QString m_raw, m_playable, m_title, m_artist, m_format;
    QString m_outputDirectory;
    void *m_jobHandle = nullptr;
    QVariantMap m_track;
};

class LyricService : public QObject {
    Q_OBJECT
  public:
    LyricService(Library *library, QSettings *settings, QObject *parent = nullptr);
    ~LyricService() override;
    void fetch(qint64 id, bool force = false, const QString &keywords = {}, bool workbench = false);
    bool importFile(qint64 id, const QString &path, bool workbench = false);
    void useCandidate(qint64 id, const QVariantMap &candidate, bool workbench = false);
    bool busy() const {
        return false;
    }
    bool matching() const {
        return m_onlinePending;
    }
    void cancel();
  signals:
    void message(const QString &text);
    void candidates(qint64 id, const QList<QVariantMap> &items);
    void activityChanged(bool active);
    void resultApplied(qint64 track, const QString &lrc, const QString &source);

  private:
    void fetchOnline(qint64 id, const QString &keywords);
    Library *m_library;
    QSettings *m_settings;
    QNetworkAccessManager m_network;
    quint64 m_request = 0;
    bool m_onlinePending = false;
    LyricsSourcesSearch m_online;
    qint64 m_onlineTrack = 0;
    bool m_onlineWorkbench = false;
};

class AudioExportService : public QObject {
    Q_OBJECT
  public:
    AudioExportService(Library *library, QSettings *settings, QObject *parent = nullptr);
    ~AudioExportService() override;
    void exportTrack(qint64 track, const QString &destination);
    bool busy() const {
        return bool(m_state);
    }
  signals:
    void message(const QString &text);
    void finished(bool ok, const QString &audio, const QString &lrc, const QString &error);

  private:
    struct State;
    void prepareAudio(const QString &path);
    void finish(bool ok, const QString &error = {});
    Library *m_library;
    QSettings *m_settings;
    QProcess m_process;
    std::shared_ptr<State> m_state;
};
