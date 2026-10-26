#pragma once
#include "window.h"
#include <QDir>
#include <QProcess>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

inline void checkAudioOutputSettingsAndPlayback() {
    QTemporaryDir dir(QDir::current().filePath("audio-output-test-XXXXXX"));
    Library library(dir.filePath("data"));
    QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
    settings.setValue("lyrics/autoFetch", false);
    settings.setValue("windows/showFloat", false);
    settings.setValue("windows/showLyrics", false);
    const QByteArray missing("test-disconnected-headset");
    settings.setValue("audio/outputDevice", missing.toBase64());
    settings.setValue("audio/outputDeviceName", "测试耳机");
    QByteArray selected;
    {
        MainWindow window(&library, &settings);
        window.show();
        window.navigate(4);
        auto outputs = window.findChild<QComboBox *>("settingsAudioOutput");
        auto detect = window.findChild<QPushButton *>("detectAudioOutputs");
        auto status = window.findChild<QLabel *>("audioOutputStatus");
        QVERIFY(outputs && detect && status);
        QCOMPARE(window.player()->outputDeviceId(), missing);
        QCOMPARE(outputs->currentData().toByteArray(), missing);
        QVERIFY(outputs->currentText().contains("未连接"));
        const auto actualDefault = QMediaDevices::defaultAudioOutput();
        if (!actualDefault.isNull())
            QCOMPARE(window.player()->audio()->device().id(), actualDefault.id());
        const auto deviceCount = QMediaDevices::audioOutputs().size();
        QCOMPARE(outputs->count(), deviceCount + 2); // System default + missing pin.
        outputs->setCurrentIndex(0);
        outputs->activated(0);
        QVERIFY(window.player()->outputDeviceId().isEmpty());
        QCOMPARE(outputs->count(), deviceCount + 1);
        QVERIFY(settings.value("audio/outputDevice").toByteArray().isEmpty());
        if (deviceCount > 0) {
            selected = outputs->itemData(1).toByteArray();
            outputs->setCurrentIndex(1);
            outputs->activated(1);
            QCOMPARE(window.player()->audio()->device().id(), selected);
            QCOMPARE(QByteArray::fromBase64(settings.value("audio/outputDevice").toByteArray()), selected);
        }
        const auto audio = dir.filePath("tone.flac");
        QProcess ffmpeg;
        ffmpeg.start(toolPath(&settings, "ffmpeg"),
                     {"-v", "error", "-n", "-f", "lavfi", "-i", "sine=frequency=440:duration=15", audio});
        QVERIFY(ffmpeg.waitForFinished(15000));
        QCOMPARE(ffmpeg.exitCode(), 0);
        const auto id = library.addTrack({{"title", "输出切换验证"}, {"path", audio}});
        auto player = window.player();
        player->setVolume(.6f);
        player->setDuckingGain(.4f);
        player->audio()->setMuted(true);
        player->setQueue({id, id}, id, true);
        QTRY_VERIFY_WITH_TIMEOUT(player->media()->position() > 500, 15000);
        player->media()->pause();
        player->media()->setPosition(4500);
        QTRY_COMPARE_WITH_TIMEOUT(player->media()->position(), 4500, 5000);
        const auto queue = player->entries();
        const auto source = player->media()->source();
        QSignalSpy changed(player, &Player::audioOutputsChanged);
        detect->click();
        QCOMPARE(changed.count(), 1);
        QCOMPARE(player->media()->playbackState(), QMediaPlayer::PausedState);
        QCOMPARE(player->media()->position(), 4500);
        QCOMPARE(player->media()->source(), source);
        QCOMPARE(player->entries().size(), queue.size());
        QCOMPARE(player->entries().first().key, queue.first().key);
        QCOMPARE(player->volume(), .6f);
        QVERIFY(qAbs(player->audio()->volume() - .24f) < .001f);
        QVERIFY(player->audio()->isMuted());
        player->media()->play();
        QTRY_VERIFY_WITH_TIMEOUT(player->media()->position() > 4700, 5000);
        const auto before = player->media()->position();
        detect->click();
        QTRY_COMPARE_WITH_TIMEOUT(player->media()->playbackState(), QMediaPlayer::PlayingState, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(player->media()->position() >= before, 5000);
        QCOMPARE(player->queue().size(), 2);
        player->media()->stop();
    }
    {
        MainWindow window(&library, &settings);
        QCOMPARE(window.player()->outputDeviceId(), selected);
        QCOMPARE(window.findChild<QComboBox *>("settingsAudioOutput")->currentData().toByteArray(), selected);
    }
}
