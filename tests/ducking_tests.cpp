#include "autoducking.h"
#include "library.h"
#include "player.h"
#include <QApplication>
#include <QAudioSource>
#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTextStream>
#include <QtTest>
#include <cmath>
#include <limits>

static bool writeWave(const QString &path, bool silent) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    QDataStream data(&file);
    data.setByteOrder(QDataStream::LittleEndian);
    constexpr int rate = 24000, samples = rate * 2, bytes = samples * 2;
    data.writeRawData("RIFF", 4);
    data << quint32(36 + bytes);
    data.writeRawData("WAVEfmt ", 8);
    data << quint32(16) << quint16(1) << quint16(1) << quint32(rate) << quint32(rate * 2) << quint16(2)
         << quint16(16);
    data.writeRawData("data", 4);
    data << quint32(bytes);
    for (int i = 0; i < samples; ++i)
        data << qint16(silent ? 0 : std::sin(6.283185307179586 * 220 * i / rate) * .08 * 32767);
    return data.status() == QDataStream::Ok;
}

class DuckingTests : public QObject {
    Q_OBJECT
  private slots:
    void speechGateRejectsImpulsesAndHoldsPauses() {
        SpeechLevelGate gate;
        QVERIFY(!gate.observe(.001, 0));
        QVERIFY(!gate.observe(.1, 100));
        QVERIFY(!gate.observe(.001, 120)); // A click must not count as speech.
        QVERIFY(!gate.observe(.02, 200));
        QVERIFY(!gate.observe(.02, 300));
        QVERIFY(gate.observe(.02, 320));
        QVERIFY(gate.observe(.001, 340));
        QVERIFY(gate.active(1519));
        QVERIFY(!gate.active(1520));
        gate.reset();
        QVERIFY(!gate.active(330));
        QVERIFY(!gate.observe(std::numeric_limits<double>::quiet_NaN(), 2000));
    }
    void triggerOptionsCombineWithOrAndRespectMasterSwitch() {
        for (int flags = 0; flags < 128; ++flags) {
            const bool enabled = flags & 1, voice = flags & 2, mic = flags & 4;
            const bool sound = flags & 8, speaking = flags & 16, open = flags & 32, playing = flags & 64;
            QCOMPARE(AutoDucking::shouldDuck(enabled, voice, mic, sound, speaking, open, playing),
                     enabled && ((voice && speaking) || (mic && open) || (sound && playing)));
        }
    }
    void gainNeverChangesUserVolumeOrAccumulates() {
        QTemporaryDir dir;
        Library library(dir.path());
        Player player(&library);
        player.setVolume(.8f);
        for (int i = 0; i < 5; ++i) {
            player.setDuckingGain(.3f);
            QVERIFY(qAbs(player.audio()->volume() - .24f) < .001f);
            QCOMPARE(player.volume(), .8f);
        }
        player.setVolume(.5f); // A user adjustment during ducking must survive restoration.
        QVERIFY(qAbs(player.audio()->volume() - .15f) < .001f);
        player.setDuckingGain(0);
        QCOMPARE(player.audio()->volume(), 0.f);
        player.setDuckingGain(1);
        QCOMPARE(player.audio()->volume(), .5f);
        player.setVolume(0);
        player.setDuckingGain(.3f);
        player.setDuckingGain(1);
        QCOMPARE(player.audio()->volume(), 0.f);
    }
    void microphoneObservationFadesAndDisableRestores() {
        QTemporaryDir dir;
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        settings.setValue("audioDucking/enabled", true);
        settings.setValue("audioDucking/onSpeech", false);
        settings.setValue("audioDucking/onMicrophone", true);
        settings.setValue("audioDucking/onOtherAudio", false);
        settings.setValue("audioDucking/reduction", 70);
        Player player(&library);
        player.setVolume(.8f);
        AutoDucking ducking(&player, &settings);
        QCoreApplication::processEvents();
        auto monitor = ducking.findChild<AudioSessionMonitor *>();
        QVERIFY(monitor);
        monitor->stop(); // Feed deterministic OS observations after stopping the polling fixture.
        QCoreApplication::processEvents();
        monitor->observed(true, {});
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(player.audio()->volume() - .24f) < .002f, 1500);
        QVERIFY(ducking.status().contains("70%"));
        QCOMPARE(player.volume(), .8f);
        player.setVolume(.5f);
        monitor->observed(false, {});
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(player.audio()->volume() - .5f) < .002f, 1500);
        monitor->observed(true, {});
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(player.audio()->volume() - .15f) < .002f, 1500);
        settings.setValue("audioDucking/enabled", false);
        ducking.reloadSettings();
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(player.audio()->volume() - .5f) < .002f, 1500);
        monitor->observed(true, {}); // Late callbacks must not re-duck after disabling.
        QTest::qWait(220);
        QCOMPARE(player.audio()->volume(), .5f);
        QCOMPARE(ducking.status(), QString("已关闭"));
        ducking.stop();
        QCOMPARE(player.volume(), .5f);
    }
    void otherAudioObservationHoldsBriefSilenceAndRespectsOption() {
        QTemporaryDir dir;
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        settings.setValue("audioDucking/enabled", true);
        settings.setValue("audioDucking/onSpeech", false);
        settings.setValue("audioDucking/onMicrophone", false);
        settings.setValue("audioDucking/onOtherAudio", true);
        Player player(&library);
        player.setVolume(.8f);
        AutoDucking ducking(&player, &settings);
        QCoreApplication::processEvents();
        auto monitor = ducking.findChild<AudioSessionMonitor *>("playbackSessionMonitor");
        QVERIFY(monitor);
        monitor->stop();
        QCoreApplication::processEvents();
        monitor->observed(true, {});
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(player.audio()->volume() - .24f) < .002f, 1500);
        QVERIFY(ducking.status().contains("其他应用正在发声"));
        monitor->observed(false, {});
        QTest::qWait(250);
        QVERIFY(qAbs(player.audio()->volume() - .24f) < .002f);
        player.setVolume(.5f);
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(player.audio()->volume() - .5f) < .002f, 2200);
        monitor->observed(true, {});
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(player.audio()->volume() - .15f) < .002f, 1500);
        settings.setValue("audioDucking/onOtherAudio", false);
        ducking.reloadSettings();
        monitor->observed(true, {});
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(player.audio()->volume() - .5f) < .002f, 1500);
        QVERIFY(ducking.status().contains("请选择触发条件"));
    }
    void nativeMonitorReportsAndStops() {
        AudioSessionMonitor monitor;
        QSignalSpy samples(&monitor, &AudioSessionMonitor::observed);
        monitor.start();
        QVERIFY(samples.wait(5000));
        monitor.stop();
        QVERIFY(!monitor.isRunning());
        // An unavailable API is visible to the UI; it must not pretend to detect an open mic.
        if (!samples.last()[1].toString().isEmpty())
            QVERIFY(!samples.last()[0].toBool());
        qInfo() << "Native microphone observation:" << samples.last();
    }
    void nativeMonitorDistinguishesOurDetectorFromAnotherApplication() {
        AudioSessionMonitor monitor;
        QSignalSpy samples(&monitor, &AudioSessionMonitor::observed);
        monitor.start();
        auto cleanupMonitor = qScopeGuard([&] { monitor.stop(); });
        QVERIFY(samples.wait(5000));
        if (!samples.last()[1].toString().isEmpty())
            QSKIP(qPrintable(samples.last()[1].toString()));
        if (samples.last()[0].toBool())
            QSKIP("Another application already has the microphone open");
        const auto device = QMediaDevices::defaultAudioInput();
        if (device.isNull())
            QSKIP("No microphone attached");
        QAudioSource ourCapture(device, device.preferredFormat());
        auto reader = ourCapture.start();
        if (!reader || ourCapture.error() != QtAudio::NoError)
            QSKIP("Microphone capture unavailable");
        connect(reader, &QIODevice::readyRead, &ourCapture, [reader] { reader->readAll(); });
        QTest::qWait(700);
        QVERIFY(!samples.last()[0].toBool()); // Opening our detector must not count as another app.
        QTemporaryDir fixtureDir;
        const auto stopFile = fixtureDir.filePath("stop");
        QProcess fixture;
        fixture.start(QCoreApplication::applicationFilePath(), {"--microphone-fixture", stopFile});
        auto stopFixture = [&] {
            QFile file(stopFile);
            if (!file.open(QIODevice::WriteOnly))
                return false;
            file.close();
            return fixture.state() == QProcess::NotRunning || fixture.waitForFinished(3000);
        };
        auto cleanupFixture = qScopeGuard([&] {
            // This is our own short-lived test helper, never a user process.
            if (!stopFixture() && fixture.state() != QProcess::NotRunning) {
                fixture.kill();
                fixture.waitForFinished(3000);
            }
        });
        QVERIFY(fixture.waitForStarted(5000));
        QTRY_VERIFY_WITH_TIMEOUT(fixture.bytesAvailable() > 0 || fixture.state() == QProcess::NotRunning,
                                 6000);
        const auto response = fixture.readAllStandardOutput();
        if (!response.contains("READY"))
            QSKIP("Second process could not open microphone");
        QTRY_VERIFY_WITH_TIMEOUT(samples.last()[0].toBool(), 4000);
        QVERIFY(stopFixture());
        QTRY_VERIFY_WITH_TIMEOUT(!samples.last()[0].toBool(), 4000);
        ourCapture.stop();
    }
    void nativeOtherAudioExcludesOwnPlaybackAndIgnoresSilence() {
        AudioSessionMonitor monitor(AudioSessionMonitor::OtherPlayback);
        QSignalSpy samples(&monitor, &AudioSessionMonitor::observed);
        monitor.start();
        auto cleanupMonitor = qScopeGuard([&] { monitor.stop(); });
        QVERIFY(samples.wait(5000));
        if (!samples.last()[1].toString().isEmpty())
            QSKIP(qPrintable(samples.last()[1].toString()));
        if (samples.last()[0].toBool())
            QSKIP("Another application already playing sound");
        if (QMediaDevices::defaultAudioOutput().isNull())
            QSKIP("No audio output attached");
        QTemporaryDir dir;
        QVERIFY(writeWave(dir.filePath("tone.wav"), false));
        QVERIFY(writeWave(dir.filePath("silence.wav"), true));
        QAudioOutput ownOutput;
        ownOutput.setVolume(.15f);
        QMediaPlayer ownPlayer;
        ownPlayer.setAudioOutput(&ownOutput);
        ownPlayer.setLoops(QMediaPlayer::Infinite);
        ownPlayer.setSource(QUrl::fromLocalFile(dir.filePath("tone.wav")));
        ownPlayer.play();
        QTRY_VERIFY_WITH_TIMEOUT(ownPlayer.position() > 100, 5000);
        QTest::qWait(400);
        QVERIFY(!samples.last()[0].toBool());
        QProcess fixture;
        fixture.start(QCoreApplication::applicationFilePath(), {"--playback-fixture", dir.path()});
        auto command = [&](const QByteArray &text) {
            QFile file(dir.filePath("mode"));
            return file.open(QIODevice::WriteOnly) && file.write(text) == text.size();
        };
        auto stopFixture = [&] {
            QFile file(dir.filePath("stop"));
            if (!file.open(QIODevice::WriteOnly))
                return false;
            file.close();
            return fixture.state() == QProcess::NotRunning || fixture.waitForFinished(3000);
        };
        auto cleanupFixture = qScopeGuard([&] {
            if (!stopFixture() && fixture.state() != QProcess::NotRunning) {
                fixture.kill();
                fixture.waitForFinished(3000);
            }
        });
        QVERIFY(fixture.waitForStarted(5000));
        QTRY_VERIFY_WITH_TIMEOUT(fixture.bytesAvailable() > 0 || fixture.state() == QProcess::NotRunning,
                                 6000);
        QVERIFY(fixture.readAllStandardOutput().contains("READY"));
        QTRY_VERIFY_WITH_TIMEOUT(samples.last()[0].toBool(), 5000);
        QVERIFY(command("silent"));
        QTRY_VERIFY_WITH_TIMEOUT(!samples.last()[0].toBool(), 3000);
        QVERIFY(command("sound"));
        QTRY_VERIFY_WITH_TIMEOUT(samples.last()[0].toBool(), 3000);
        QVERIFY(command("muted"));
        QTRY_VERIFY_WITH_TIMEOUT(!samples.last()[0].toBool(), 3000);
        QVERIFY(stopFixture());
        ownPlayer.stop();
    }
};
int main(int argc, char **argv) {
    QApplication app(argc, argv);
    if (app.arguments().contains("--playback-fixture")) {
        const QDir dir(app.arguments().value(2));
        QAudioOutput output;
        output.setVolume(.15f);
        QMediaPlayer player;
        player.setAudioOutput(&output);
        player.setLoops(QMediaPlayer::Infinite);
        QString lastMode;
        QTimer timer;
        QObject::connect(&timer, &QTimer::timeout, &app, [&] {
            if (QFileInfo::exists(dir.filePath("stop"))) {
                app.quit();
                return;
            }
            QFile modeFile(dir.filePath("mode"));
            const auto mode =
                modeFile.open(QIODevice::ReadOnly) ? QString::fromUtf8(modeFile.readAll()) : "sound";
            if (mode == lastMode)
                return;
            lastMode = mode;
            const auto source =
                QUrl::fromLocalFile(dir.filePath(mode == "silent" ? "silence.wav" : "tone.wav"));
            output.setMuted(mode == "muted");
            if (player.source() != source) {
                player.setSource(source);
                player.play();
            }
        });
        timer.start(50);
        QTimer::singleShot(500, &app, [&] {
            if (player.playbackState() == QMediaPlayer::PlayingState)
                QTextStream(stdout) << "READY\n" << Qt::flush;
            else
                app.exit(4);
        });
        QTimer::singleShot(20000, &app, &QCoreApplication::quit);
        return app.exec();
    }
    if (app.arguments().contains("--microphone-fixture")) {
        const auto device = QMediaDevices::defaultAudioInput();
        if (device.isNull())
            return 3;
        QAudioSource capture(device, device.preferredFormat());
        const auto reader = capture.start();
        if (!reader || capture.error() != QtAudio::NoError)
            return 4;
        QObject::connect(reader, &QIODevice::readyRead, &app, [reader] { reader->readAll(); });
        QTextStream(stdout) << "READY\n" << Qt::flush;
        QTimer stopTimer;
        QObject::connect(&stopTimer, &QTimer::timeout, &app, [&] {
            if (QFileInfo::exists(app.arguments().value(2)))
                app.quit();
        });
        stopTimer.start(100);
        QTimer::singleShot(10000, &app, &QCoreApplication::quit);
        return app.exec();
    }
    DuckingTests tests;
    return QTest::qExec(&tests, argc, argv);
}
#include "ducking_tests.moc"
