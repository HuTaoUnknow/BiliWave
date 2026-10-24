#include "autoducking.h"
#include "player.h"
#include <QAudioSource>
#include <QCoreApplication>
#include <cmath>
#include <cstring>
#ifdef Q_OS_WIN
#include <audiopolicy.h>
#include <endpointvolume.h>
#include <mmdeviceapi.h>
#include <windows.h>

// MinGW 13.1 only forward-declares this standard COM interface.
// ABI: Microsoft WinSDK endpointvolume.h (IAudioMeterInformation).
#ifndef __IAudioMeterInformation_INTERFACE_DEFINED__
#define __IAudioMeterInformation_INTERFACE_DEFINED__
MIDL_INTERFACE("C02216F6-8C67-4B5B-9D00-D008E73E0064")
IAudioMeterInformation : public IUnknown {
  public:
    virtual HRESULT STDMETHODCALLTYPE GetPeakValue(float *) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetMeteringChannelCount(UINT *) = 0;
    virtual HRESULT STDMETHODCALLTYPE GetChannelsPeakValues(UINT32, float *) = 0;
    virtual HRESULT STDMETHODCALLTYPE QueryHardwareSupport(DWORD *) = 0;
};
#ifdef __CRT_UUID_DECL
__CRT_UUID_DECL(IAudioMeterInformation, 0xc02216f6, 0x8c67, 0x4b5b, 0x9d, 0x00, 0xd0, 0x08, 0xe7, 0x3e, 0x00,
                0x64)
#endif
#endif

namespace {
template <typename T> struct Com {
    T *p = nullptr;
    ~Com() {
        if (p)
            p->Release();
    }
    T **out() {
        return &p;
    }
    T *operator->() const {
        return p;
    }
};
QString comError(HRESULT hr) {
    return QString("Windows 音频状态检测失败（0x%1）").arg(quint32(hr), 8, 16, QChar('0'));
}
bool sessionActivity(IMMDeviceEnumerator *enumerator, EDataFlow flow, QString *error) {
    Com<IMMDeviceCollection> devices;
    auto hr = enumerator->EnumAudioEndpoints(flow, DEVICE_STATE_ACTIVE, devices.out());
    if (FAILED(hr)) {
        *error = comError(hr);
        return false;
    }
    UINT count = 0;
    hr = devices->GetCount(&count);
    if (FAILED(hr)) {
        *error = comError(hr);
        return false;
    }
    for (UINT i = 0; i < count; ++i) {
        Com<IMMDevice> device;
        Com<IAudioSessionManager2> manager;
        Com<IAudioSessionEnumerator> sessions;
        hr = devices->Item(i, device.out());
        if (SUCCEEDED(hr) && flow == eRender) {
            Com<IAudioEndpointVolume> endpoint;
            hr = device->Activate(__uuidof(IAudioEndpointVolume), CLSCTX_ALL, nullptr,
                                  reinterpret_cast<void **>(endpoint.out()));
            BOOL muted = FALSE;
            float volume = 0;
            if (SUCCEEDED(hr))
                hr = endpoint->GetMute(&muted);
            if (SUCCEEDED(hr))
                hr = endpoint->GetMasterVolumeLevelScalar(&volume);
            if (SUCCEEDED(hr) && (muted || volume <= 0))
                continue;
        }
        if (SUCCEEDED(hr))
            hr = device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
                                  reinterpret_cast<void **>(manager.out()));
        if (SUCCEEDED(hr))
            hr = manager->GetSessionEnumerator(sessions.out());
        int size = 0;
        if (SUCCEEDED(hr))
            hr = sessions->GetCount(&size);
        if (FAILED(hr)) {
            *error = comError(hr);
            continue;
        }
        for (int j = 0; j < size; ++j) {
            Com<IAudioSessionControl> session;
            Com<IAudioSessionControl2> process;
            AudioSessionState state = AudioSessionStateInactive;
            DWORD pid = 0;
            hr = sessions->GetSession(j, session.out());
            if (SUCCEEDED(hr))
                hr = session->GetState(&state);
            if (FAILED(hr)) {
                *error = comError(hr);
                continue;
            }
            if (state != AudioSessionStateActive)
                continue;
            hr = session->QueryInterface(__uuidof(IAudioSessionControl2),
                                         reinterpret_cast<void **>(process.out()));
            if (SUCCEEDED(hr))
                hr = process->GetProcessId(&pid);
            if (FAILED(hr)) {
                *error = comError(hr);
                continue;
            }
            // Exclude both our microphone detector and our music / preview playback.
            if (pid == GetCurrentProcessId())
                continue;
            if (flow == eRender) {
                Com<ISimpleAudioVolume> volume;
                Com<IAudioMeterInformation> meter;
                BOOL muted = FALSE;
                float scalar = 0, peak = 0;
                hr = session->QueryInterface(__uuidof(ISimpleAudioVolume),
                                             reinterpret_cast<void **>(volume.out()));
                if (SUCCEEDED(hr))
                    hr = volume->GetMute(&muted);
                if (SUCCEEDED(hr))
                    hr = volume->GetMasterVolume(&scalar);
                if (FAILED(hr)) {
                    *error = comError(hr);
                    continue;
                }
                if (muted || scalar <= 0)
                    continue;
                hr = session->QueryInterface(__uuidof(IAudioMeterInformation),
                                             reinterpret_cast<void **>(meter.out()));
                if (SUCCEEDED(hr))
                    hr = meter->GetPeakValue(&peak);
                if (FAILED(hr)) {
                    *error = comError(hr);
                    continue;
                }
                // An active but silent stream must not suppress music indefinitely.
                if (!std::isfinite(peak) || peak <= .001f)
                    continue;
            }
            error->clear();
            return true;
        }
    }
    return false;
}
} // namespace
#endif

bool SpeechLevelGate::observe(double rms, qint64 now) {
    // About -38 dBFS RMS for >= 120 ms, then hold through short speech pauses.
    if (std::isfinite(rms) && rms >= .0126) {
        if (m_candidate < 0)
            m_candidate = now;
        if (now - m_candidate >= 120)
            m_lastSpeech = now;
    } else
        m_candidate = -1;
    return active(now);
}
bool SpeechLevelGate::active(qint64 now) const {
    return m_lastSpeech >= 0 && now - m_lastSpeech < 1200;
}
void SpeechLevelGate::reset() {
    m_candidate = m_lastSpeech = -1;
}

AudioSessionMonitor::~AudioSessionMonitor() {
    stop();
}
void AudioSessionMonitor::stop() {
    QMutexLocker lock(&m_mutex);
    requestInterruption();
    m_wake.wakeAll();
    lock.unlock();
    wait();
}
void AudioSessionMonitor::run() {
#ifdef Q_OS_WIN
    const auto initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    if (FAILED(initialized)) {
        emit observed(false, comError(initialized));
        return;
    }
    {
        Com<IMMDeviceEnumerator> enumerator;
        const auto hr =
            CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL, __uuidof(IMMDeviceEnumerator),
                             reinterpret_cast<void **>(enumerator.out()));
        if (FAILED(hr))
            emit observed(false, comError(hr));
        else
            while (!isInterruptionRequested()) {
                QString error;
                const bool active =
                    sessionActivity(enumerator.p, m_source == Microphone ? eCapture : eRender, &error);
                emit observed(active, error);
                QMutexLocker lock(&m_mutex);
                if (!isInterruptionRequested())
                    m_wake.wait(&m_mutex, m_source == Microphone ? 500 : 100);
            }
    }
    CoUninitialize();
#else
    emit observed(false, "音频会话检测仅支持 Windows");
#endif
}

AutoDucking::AutoDucking(Player *player, QSettings *settings, QObject *parent)
    : QObject(parent), m_player(player), m_settings(settings) {
    setObjectName("autoDucking");
    m_sessions.setParent(this);
    m_sessions.setObjectName("microphoneSessionMonitor");
    m_outputSessions.setParent(this);
    m_outputSessions.setObjectName("playbackSessionMonitor");
    m_clock.start();
    m_tick.setInterval(50);
    connect(&m_tick, &QTimer::timeout, this, [this] {
        readInput();
        update();
    });
    connect(&m_devices, &QMediaDevices::audioInputsChanged, this, [this] {
        stopInput();
        if (m_enabled && m_voice)
            startInput();
        update();
    });
    connect(&m_sessions, &AudioSessionMonitor::observed, this, [this](bool active, const QString &error) {
        if (!m_enabled || !m_mic)
            return;
        m_open = active;
        m_sessionError = error;
        update();
    });
    connect(&m_outputSessions, &AudioSessionMonitor::observed, this,
            [this](bool active, const QString &error) {
                if (!m_enabled || !m_otherSound)
                    return;
                m_outputError = error;
                if (active)
                    m_lastOtherSound = m_clock.elapsed();
                if (!error.isEmpty())
                    m_lastOtherSound = -1;
                update();
            });
    connect(&m_fade, &QVariantAnimation::valueChanged, this, [this](const QVariant &value) {
        m_gain = value.toFloat();
        m_player->setDuckingGain(m_gain);
    });
    QTimer::singleShot(0, this, &AutoDucking::reloadSettings);
}
AutoDucking::~AutoDucking() {
    stop();
}
bool AutoDucking::shouldDuck(bool enabled, bool voice, bool mic, bool sound, bool speaking, bool open,
                             bool otherSound) {
    return enabled && ((voice && speaking) || (mic && open) || (sound && otherSound));
}
void AutoDucking::reloadSettings() {
    m_enabled = m_settings->value("audioDucking/enabled", false).toBool();
    m_voice = m_settings->value("audioDucking/onSpeech", true).toBool();
    m_mic = m_settings->value("audioDucking/onMicrophone", true).toBool();
    m_otherSound = m_settings->value("audioDucking/onOtherAudio", true).toBool();
    m_reduction = qBound(0, m_settings->value("audioDucking/reduction", 70).toInt(), 100);
    if (!m_enabled || !m_voice)
        stopInput();
    else if (!m_input)
        startInput();
    if (m_enabled && m_mic) {
        if (!m_sessions.isRunning())
            m_sessions.start();
    } else {
        m_sessions.stop();
        m_open = false;
        m_sessionError.clear();
    }
    if (m_enabled && m_otherSound) {
        if (!m_outputSessions.isRunning())
            m_outputSessions.start();
    } else {
        m_outputSessions.stop();
        m_lastOtherSound = -1;
        m_outputError.clear();
    }
    if (m_enabled)
        m_tick.start();
    else
        m_tick.stop();
    update();
}
void AutoDucking::startInput() {
    m_inputError.clear();
    const auto device = QMediaDevices::defaultAudioInput();
    if (device.isNull()) {
        m_inputError = "没有可用的麦克风";
        return;
    }
    m_format = device.preferredFormat();
    if (!m_format.isValid()) {
        m_inputError = "麦克风格式不可用";
        return;
    }
    m_input = new QAudioSource(device, m_format, this);
    connect(m_input, &QAudioSource::stateChanged, this, [this](QtAudio::State state) {
        if (m_input && state == QtAudio::StoppedState && m_input->error() != QtAudio::NoError) {
            m_inputError = "麦克风不可用，请检查系统权限和独占占用；可关闭再开启重试";
            m_gate.reset();
            update();
        }
    });
    m_reader = m_input->start();
    if (!m_reader)
        m_inputError = "无法读取麦克风，请检查系统权限和独占占用";
}
void AutoDucking::stopInput() {
    if (m_input) {
        m_input->disconnect(this);
        m_input->stop();
        delete m_input;
        m_input = nullptr;
    }
    m_reader = nullptr;
    m_pending.clear();
    m_gate.reset();
    m_inputError.clear();
}
void AutoDucking::readInput() {
    if (!m_reader || !m_inputError.isEmpty())
        return;
    m_pending += m_reader->readAll();
    // Fixed 20 ms windows make detection independent of device buffer size / channel count.
    const int bytes = m_format.bytesForDuration(20000);
    const int stride = m_format.bytesPerSample();
    if (bytes <= 0 || stride <= 0)
        return;
    int offset = 0;
    while (m_pending.size() - offset >= bytes) {
        double sum = 0;
        const auto *data = m_pending.constData() + offset;
        for (int i = 0; i + stride <= bytes; i += stride) {
            // memcpy avoids unaligned typed reads in normalizedSampleValue.
            alignas(8) char sample[8]{};
            memcpy(sample, data + i, size_t(stride));
            const double value = m_format.normalizedSampleValue(sample);
            sum += value * value;
        }
        const qint64 stamp =
            m_clock.elapsed() - m_format.durationForBytes(m_pending.size() - offset - bytes) / 1000;
        m_gate.observe(std::sqrt(sum / (bytes / stride)), stamp);
        offset += bytes;
    }
    m_pending.remove(0, offset);
}
void AutoDucking::update() {
    const bool speaking = m_gate.active(m_clock.elapsed());
    const bool otherSound = m_lastOtherSound >= 0 && m_clock.elapsed() - m_lastOtherSound < 800;
    const bool duck = shouldDuck(m_enabled, m_voice, m_mic, m_otherSound, speaking, m_open, otherSound);
    const float target = duck ? 1.f - m_reduction / 100.f : 1.f;
    if (target != m_target) {
        m_target = target;
        m_fade.stop();
        m_fade.setStartValue(m_gain);
        m_fade.setEndValue(target);
        m_fade.setDuration(target < m_gain ? 180 : 650);
        m_fade.start();
    }
    QString text = !m_enabled                            ? "已关闭"
                   : !m_voice && !m_mic && !m_otherSound ? "请选择触发条件"
                   : duck ? QString("正在降低 %1% 音量 · %2")
                                .arg(m_reduction)
                                .arg(m_mic && m_open              ? "其他应用正在使用麦克风"
                                     : m_otherSound && otherSound ? "其他应用正在发声"
                                                                  : "检测到麦克风声音")
                          : "等待触发";
    if (m_enabled && !m_inputError.isEmpty())
        text += " · " + m_inputError;
    if (m_enabled && !m_sessionError.isEmpty())
        text += " · " + m_sessionError;
    if (m_enabled && !m_outputError.isEmpty())
        text += " · " + m_outputError;
    if (text != m_status) {
        m_status = text;
        emit statusChanged(text);
    }
}
void AutoDucking::stop() {
    m_tick.stop();
    m_sessions.stop();
    m_outputSessions.stop();
    m_lastOtherSound = -1;
    stopInput();
    m_enabled = false;
    m_fade.stop();
    m_gain = m_target = 1.f;
    m_player->setDuckingGain(1.f);
}
