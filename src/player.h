#pragma once
#include "library.h"
#include <QAudioOutput>
#include <QAudioDevice>
#include <QMediaDevices>
#include <QMediaPlayer>
class Player : public QObject {
    Q_OBJECT
  public:
    struct QueueEntry {
        quint64 key;
        qint64 track;
    };
    enum QueueMove { Top, Bottom, Up, Down };
    enum Mode { Sequential, ListLoop, Shuffle, SingleLoop };
    Q_ENUM(Mode)
    explicit Player(Library *library, QObject *parent = nullptr);
    void acceptStream(quint64 token, const QUrl &url, const QString &error);
    void cancelStream();
    QMediaPlayer *media() {
        return &m_media;
    }
    QAudioOutput *audio() {
        return &m_audio;
    }
    float volume() const {
        return m_volume;
    }
    void setVolume(float volume);
    void setDuckingGain(float gain);
    QByteArray outputDeviceId() const { return m_outputDeviceId; }
    void setOutputDeviceId(const QByteArray &id);
    void refreshAudioOutputs(bool reconnect = false);
    static QByteArray chooseAudioOutput(const QByteArray &preferred, const QList<QByteArray> &available,
                                       const QByteArray &systemDefault);
    qint64 currentId() const {
        return m_current;
    }
    QList<qint64> queue() const {
        QList<qint64> ids;
        for (const auto &entry : m_queue)
            ids << entry.track;
        return ids;
    }
    QList<QueueEntry> entries() const {
        return m_queue;
    }
    quint64 currentEntry() const {
        return m_currentEntry;
    }
    int currentQueueIndex() const;
    QList<QueueEntry> pendingEntries() const;
    void removeEntry(quint64 key);
    void jumpToEntry(quint64 key);
    void moveEntry(quint64 key, QueueMove move);
    void restoreQueue(const QList<qint64> &ids, int currentIndex);
    Mode mode() const {
        return m_mode;
    }
    void setMode(Mode mode);
    QList<qint64> upcoming() const;
    void enqueue(const QList<qint64> &ids);
    void removeQueued(qint64 id);
    void jumpTo(qint64 id);
    void setQueue(const QList<qint64> &ids, qint64 start = 0, bool play = true, int startIndex = -1);
    void playPlaylist(qint64 id);
    void toggle();
    void next(bool automatic = false);
    void previous();
  signals:
    void currentChanged(qint64 id);
    void queueChanged();
    void failure(const QString &message);
    void streamRequested(quint64 token, const QString &url);
    void volumeChanged(float volume);
    void audioOutputsChanged();

  private:
    void activate(quint64 key, bool play = true);
    void prepareShuffle();
    int entryIndex(quint64 key) const;
    QList<QueueEntry> orderedPending() const;
    Library *m_library;
    QMediaPlayer m_media;
    QAudioOutput m_audio;
    QMediaDevices m_devices;
    QByteArray m_outputDeviceId;
    float m_volume = .75f, m_duckingGain = 1.f;
    QList<QueueEntry> m_queue;
    QList<quint64> m_history, m_shuffleBag;
    qint64 m_current = 0;
    quint64 m_currentEntry = 0, m_nextEntry = 0;
    Mode m_mode = ListLoop;
    quint64 m_streamToken = 0;
    bool m_loadingStream = false, m_streamPlay = false;
};
