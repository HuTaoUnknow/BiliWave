#pragma once
#include <QAudioFormat>
#include <QElapsedTimer>
#include <QMediaDevices>
#include <QMutex>
#include <QSettings>
#include <QThread>
#include <QTimer>
#include <QVariantAnimation>
#include <QWaitCondition>

class Player;
class QAudioSource;

// A level gate, not a transcription model. Time is supplied for deterministic tests.
class SpeechLevelGate {
  public:
    bool observe(double rms, qint64 now);
    bool active(qint64 now) const;
    void reset();

  private:
    qint64 m_candidate = -1, m_lastSpeech = -1;
};

class AudioSessionMonitor : public QThread {
    Q_OBJECT
  public:
    enum Source { Microphone, OtherPlayback };
    explicit AudioSessionMonitor(Source source = Microphone, QObject *parent = nullptr)
        : QThread(parent), m_source(source) {}
    ~AudioSessionMonitor() override;
    void stop();
  signals:
    void observed(bool active, const QString &error);

  protected:
    void run() override;

  private:
    QMutex m_mutex;
    QWaitCondition m_wake;
    Source m_source;
};

class AutoDucking : public QObject {
    Q_OBJECT
  public:
    AutoDucking(Player *player, QSettings *settings, QObject *parent = nullptr);
    ~AutoDucking() override;
    void reloadSettings();
    void stop();
    QString status() const {
        return m_status;
    }
    static bool shouldDuck(bool enabled, bool voiceEnabled, bool micEnabled, bool soundEnabled, bool speaking,
                           bool micOpen, bool otherSound);
  signals:
    void statusChanged(const QString &status);

  private:
    void startInput();
    void stopInput();
    void readInput();
    void update();
    Player *m_player;
    QSettings *m_settings;
    QMediaDevices m_devices;
    QAudioSource *m_input = nullptr;
    QIODevice *m_reader = nullptr;
    QAudioFormat m_format;
    QByteArray m_pending;
    QElapsedTimer m_clock;
    QTimer m_tick;
    QVariantAnimation m_fade;
    AudioSessionMonitor m_sessions, m_outputSessions{AudioSessionMonitor::OtherPlayback};
    SpeechLevelGate m_gate;
    bool m_enabled = false, m_voice = true, m_mic = true, m_open = false;
    bool m_otherSound = true;
    qint64 m_lastOtherSound = -1;
    int m_reduction = 70;
    float m_gain = 1.f, m_target = 1.f;
    QString m_inputError, m_sessionError, m_outputError, m_status;
};
