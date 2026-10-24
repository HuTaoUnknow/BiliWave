#include "window.h"
#include <QDir>
#include <QDropEvent>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest>

class IntegrationTests : public QObject {
    Q_OBJECT
    QTemporaryDir dir{QDir::current().filePath("integration-XXXXXX")};
    QString flac, raw;
    QByteArray invoke(const QStringList &args) {
        QProcess p;
        p.start(QDir(resourceRoot()).filePath("tools/ffmpeg.exe"), args);
        if (!p.waitForFinished(20000) || p.exitCode() != 0)
            return {};
        return p.readAllStandardOutput();
    }
  private slots:
    void exportAudioWithEmbeddedAndSidecarLyrics() {
        QTemporaryDir taskDir(QDir::current().filePath("export-audio-XXXXXX"));
        Library library(taskDir.filePath("data"));
        QSettings settings(taskDir.filePath("settings.ini"), QSettings::IniFormat);
        AudioExportService exporter(&library, &settings);
        QSignalSpy done(&exporter, &AudioExportService::finished);
        const QString lyrics =
            "[00:01.000]Привет, мир!\n[00:01.000]你好，世界！\n[00:04.000]Love = music; #1\\2";
        const auto originalCount = library.tracks().size();
        auto hash = [&](const QString &path) {
            return invoke({"-v", "error", "-i", path, "-map", "0:a:0", "-c:a", "copy", "-f", "hash", "-hash",
                           "sha256", "-"});
        };
        const auto m4a = taskDir.filePath("original.m4a");
        QVERIFY(QFile::copy(raw, m4a));
        for (const auto &input : {flac, m4a}) {
            const auto id =
                library.addTrack({{"title", "导出测试"}, {"artist", "BiliWave"}, {"path", input}});
            library.setLyrics(id, lyrics, "测试");
            library.setOffset(id, 200);
            const QString ext = QFileInfo(input).suffix();
            exporter.exportTrack(id, taskDir.filePath("exported-" + ext + "." + ext));
            QTRY_VERIFY_WITH_TIMEOUT(done.size() > 0, 20000);
            const auto result = done.takeFirst();
            QVERIFY2(result[0].toBool(), qPrintable(result[3].toString()));
            QCOMPARE(hash(result[1].toString()), hash(input));
            QFile lrc(result[2].toString());
            QVERIFY(lrc.open(QIODevice::ReadOnly));
            const auto exported = QString::fromUtf8(lrc.readAll());
            QCOMPARE(Lrc::parse(exported).first().ms, 800);
            QVERIFY(exported.contains("你好，世界！"));
            QProcess probe;
            probe.start(QDir(resourceRoot()).filePath("tools/ffprobe.exe"),
                        {"-v", "error", "-show_format", "-of", "json", result[1].toString()});
            QVERIFY(probe.waitForFinished(10000));
            const auto tags = QJsonDocument::fromJson(probe.readAllStandardOutput())
                                  .object()["format"]
                                  .toObject()["tags"]
                                  .toObject();
            QString embedded;
            for (auto it = tags.begin(); it != tags.end(); ++it)
                if (it.key().startsWith("lyrics", Qt::CaseInsensitive))
                    embedded = it.value().toString();
            QCOMPARE(embedded, exported);
        }
        // URL export uses a private download library and must not add a local song to the user's library.
        settings.setValue("tools/yt-dlp", QCoreApplication::applicationDirPath() + "/import_fixture.exe");
        qputenv("BILI_TEST_DOWNLOAD_SOURCE", flac.toUtf8());
        const auto restoreEnv = qScopeGuard([] { qunsetenv("BILI_TEST_DOWNLOAD_SOURCE"); });
        const auto url =
            library.addUrl({{"title", "URL 导出"}, {"url", "https://www.bilibili.com/video/BV13x41117TL"}});
        library.setLyrics(url, lyrics, "测试");
        const auto count = library.tracks().size();
        exporter.exportTrack(url, taskDir.filePath("url-song"));
        QTRY_VERIFY_WITH_TIMEOUT(done.size() > 0, 20000);
        const auto result = done.takeFirst();
        QVERIFY2(result[0].toBool(), qPrintable(result[3].toString()));
        QCOMPARE(hash(result[1].toString()), hash(flac));
        QCOMPARE(library.tracks().size(), count);
        QCOMPARE(library.track(url).value("storage").toString(), QString("url"));
        QVERIFY(library.stagedTracks().isEmpty());
        QVERIFY(originalCount == 0);
    }
    void cancelledDownloadsRemoveOnlyTheirOwnFiles() {
        QTemporaryDir taskDir(QDir::current().filePath("cancel-download-XXXXXX"));
        Library library(taskDir.filePath("data"));
        QSettings settings(taskDir.filePath("settings.ini"), QSettings::IniFormat);
        const auto folder = taskDir.filePath("media");
        QDir().mkpath(folder);
        QFile unrelated(folder + "/keep.flac");
        QVERIFY(unrelated.open(QIODevice::WriteOnly));
        unrelated.write("keep this");
        unrelated.close();
        settings.setValue("download/directory", folder);
        settings.setValue("tools/yt-dlp", QCoreApplication::applicationDirPath() + "/import_fixture.exe");
        ImportService service(&library, &settings);
        QSignalSpy statuses(&service, &ImportService::status);
        service.enqueue("https://www.bilibili.com/video/BV13x41117TL", true);
        QTRY_VERIFY_WITH_TIMEOUT(QDir(folder).entryList({"*.part"}, QDir::Files).size() == 1, 15000);
        const auto activeId = statuses.first().at(0).toString();
        service.enqueue("https://www.bilibili.com/video/BV13x41117TL?p=2", true);
        const auto waitingId = statuses.last().at(0).toString();
        QVERIFY(waitingId != activeId);
        service.cancelTask(waitingId);
        QVERIFY(service.busy());
        QCOMPARE(QDir(folder).entryList({"*.part"}, QDir::Files).size(), 1);
        service.cancelTask(activeId);
        QTRY_VERIFY_WITH_TIMEOUT(!service.busy(), 5000);
        QCOMPARE(QDir(folder).entryList(QDir::Files), QStringList{"keep.flac"});
        QVERIFY(library.stagedTracks().isEmpty());
        QVERIFY(statuses.last().at(2).toString().startsWith("取消"));
        // Cancelling a generated remux must preserve an imported source file.
        const auto input = taskDir.filePath("original.m4s");
        QVERIFY(QFile::copy(raw, input));
        settings.setValue("tools/ffmpeg", QCoreApplication::applicationDirPath() + "/import_fixture.exe");
        service.enqueue(input, true);
        QTRY_VERIFY_WITH_TIMEOUT(QDir(taskDir.path()).entryList({"*.part"}, QDir::Files).size() == 1, 15000);
        service.cancel();
        QTRY_VERIFY_WITH_TIMEOUT(!service.busy(), 5000);
        QVERIFY(QFile::exists(input));
        QCOMPARE(QDir(taskDir.path()).entryList({"*.m4a", "*.part", "*.ytdl", "*Frag*"}, QDir::Files).size(),
                 0);
    }
    void initTestCase() {
        QVERIFY(dir.isValid());
        flac = dir.filePath("detail.flac");
        raw = dir.filePath("source.m4s");
        invoke({"-v", "error", "-n", "-f", "lavfi", "-i", "sine=frequency=440:duration=8:sample_rate=48000",
                "-metadata", "title=听见微光", "-metadata", "artist=BiliWave", "-c:a", "flac", flac});
        QVERIFY(QFileInfo::exists(flac));
        invoke({"-v", "error", "-n", "-i", flac, "-c:a", "aac", "-b:a", "192k", "-movflags",
                "frag_keyframe+empty_moov", "-f", "mp4", raw});
        QVERIFY(QFileInfo::exists(raw));
    }
    void importRemuxAndDecode() {
        Library library(dir.filePath("db"));
        QVERIFY(library.isOpen());
        QSettings settings(dir.filePath("test.ini"), QSettings::IniFormat);
        settings.setValue("lyrics/autoFetch", false);
        ImportService service(&library, &settings);
        QSignalSpy imported(&service, &ImportService::imported);
        service.enqueue(flac);
        QTRY_COMPARE_WITH_TIMEOUT(imported.size(), 1, 20000);
        service.enqueue(raw);
        QTRY_COMPARE_WITH_TIMEOUT(imported.size(), 2, 20000);
        QCOMPARE(library.tracks().size(), 2);
        const auto t = library.track(imported.last().first().toLongLong());
        QCOMPARE(t.value("codec").toString(), QString("aac"));
        QCOMPARE(t.value("original").toString(), raw);
        QVERIFY(t.value("path").toString().endsWith(".m4a"));
        QVERIFY(QFileInfo::exists(raw));
        auto hash = [&](const QString &path) {
            return invoke({"-v", "error", "-i", path, "-map", "0:a:0", "-c:a", "copy", "-f", "hash", "-hash",
                           "sha256", "-"});
        };
        const auto originalHash = hash(raw);
        QVERIFY(!originalHash.isEmpty());
        QCOMPARE(hash(t.value("path").toString()), originalHash);
        Player player(&library);
        player.setQueue({t.value("id").toLongLong()});
        QTRY_VERIFY_WITH_TIMEOUT(player.media()->duration() > 7000, 15000);
        QTRY_VERIFY_WITH_TIMEOUT(player.media()->position() > 100, 15000);
        player.media()->pause();
        player.media()->setPosition(4000);
        QTRY_VERIFY_WITH_TIMEOUT(qAbs(player.media()->position() - 4000) < 500, 3000);
        player.media()->stop();
    }
    void importWaitsForLibraryCommit() {
        Library library(dir.filePath("staged-db"));
        QSettings settings(dir.filePath("staged.ini"), QSettings::IniFormat);
        ImportService service(&library, &settings);
        QSignalSpy imported(&service, &ImportService::imported);
        service.enqueue(flac, true);
        QTRY_COMPARE_WITH_TIMEOUT(imported.size(), 1, 20000);
        QVERIFY(library.tracks().isEmpty());
        QCOMPARE(library.stagedTracks().size(), 1);
        const auto id = imported.first().first().toLongLong();
        QVERIFY(QFile::exists(library.track(id).value("path").toString()));
        QVERIFY(library.commitTrack(id));
        QCOMPARE(library.tracks().size(), 1);
        QVERIFY(library.stagedTracks().isEmpty());
    }
};
QTEST_MAIN(IntegrationTests)
#include "integration_tests.moc"
