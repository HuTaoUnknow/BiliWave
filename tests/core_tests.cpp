#include "library.h"
#include "lrc.h"
#include "player.h"
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlQuery>
#include <QTemporaryDir>
#include <QThreadPool>
#include <QtTest>
class CoreTests : public QObject {
    Q_OBJECT
  private slots:
    void mergedModesRetainPersistedNumbers() {
        QCOMPARE(Player::modeFromStored(0), Player::Sequential);
        QCOMPARE(Player::modeFromStored(1), Player::Sequential);
        QCOMPARE(Player::modeFromStored(2), Player::Shuffle);
        QCOMPARE(Player::modeFromStored(3), Player::SingleLoop);
        QCOMPARE(Player::modeFromStored(-1), Player::Sequential);
        QCOMPARE(Player::modeFromStored(99), Player::Sequential);
    }
    void playlistSummariesMatchVisibleMembers() {
        QTemporaryDir dir(QDir::current().filePath("summary-test-XXXXXX"));
        Library library(dir.path());
        const auto a = library.addTrack({{"title", "A"}, {"artist", "Artist"}, {"path", "a.wav"}, {"duration", 1000}});
        const auto b = library.addTrack({{"title", "B"}, {"path", "b.wav"}, {"duration", 2500}});
        const auto staged = library.addTrack({{"title", "Staged"}, {"path", "c.wav"}, {"duration", 9000}, {"staged", 1}});
        const auto first = library.createPlaylist("First"), second = library.createPlaylist("Empty");
        QVERIFY(library.addToPlaylist(first, {a, b, staged}));
        QVERIFY(library.setLyrics(a, "[00:01]門後", "Saved"));
        auto playlists = library.playlists();
        QCOMPARE(playlists.first().value("count").toInt(), 2);
        QCOMPARE(playlists.first().value("duration").toLongLong(), 3500);
        QCOMPARE(playlists.last().value("id").toLongLong(), second);
        QCOMPARE(playlists.last().value("duration").toLongLong(), 0);
        QCOMPARE(library.trackIds(), QSet<qint64>({a,b,staged}));
        QCOMPARE(library.assignedTrackIds(), QSet<qint64>({a,b}));
        auto summary = library.trackSummaries(first);
        QCOMPARE(summary.size(), 2);
        QCOMPARE(summary.first().value("id").toLongLong(), a);
        QVERIFY(!summary.first().contains("lrc"));
        QVERIFY(!summary.first().contains("raw_lrc"));
        QCOMPARE(library.track(a).value("raw_lrc").toString(), QString("[00:01]門後"));
        QCOMPARE(library.trackSummaries(0, "Artist").size(), 1);
        QVERIFY(library.reorder(first, {b,a,staged}));
        QCOMPARE(library.trackSummaries(first).first().value("id").toLongLong(), b);
        QVERIFY(library.deleteTrack(b));
        QCOMPARE(library.playlists().first().value("duration").toLongLong(), 1000);
    }
    void cachedBilingualTimelineSupportsClearAndSeeking() {
        const QList<LyricLine> lines{{1000,"Hello"}, {1200,"你好"}, {2000,"Again\n再次"},
                                     {3000,""}, {4000,"Привет"}, {4200,"世界"},
                                     {5000,"光の中"}, {5000,"光之中"}};
        const auto states = Lrc::displayStates(lines);
        QCOMPARE(states.size(), lines.size());
        QCOMPARE(states[0], qMakePair(QString("Hello"), QString()));
        QCOMPARE(states[1], qMakePair(QString("Hello"), QString("你好")));
        QCOMPARE(states[2], qMakePair(QString("Again"), QString("再次")));
        QCOMPARE(states[3], qMakePair(QString(), QString()));
        QCOMPARE(states[4], qMakePair(QString("Привет"), QString()));
        QCOMPARE(states[5], qMakePair(QString("Привет"), QString("世界")));
        QCOMPARE(states[6], qMakePair(QString("光の中"), QString("光之中")));
        QCOMPARE(states[7], states[6]);
        for (qint64 position : {0, 999, 1000, 1300, 2200, 3000, 4500, 5500, 1100}) {
            const int index = Lrc::indexAt(lines, position);
            const auto cached = index >= 0 ? states[index] : QPair<QString,QString>();
            QCOMPARE(cached, Lrc::displayAt(lines, position, true));
        }
        QVERIFY(Lrc::displayStates({}).isEmpty());
    }
    void audioOutputPolicyTracksHotplugAndSystemDefault() {
        const QByteArray speakers("speakers"), headset("headset"), hdmi("hdmi");
        QCOMPARE(Player::chooseAudioOutput({}, {speakers, headset}, speakers), speakers);
        QCOMPARE(Player::chooseAudioOutput({}, {speakers, headset}, headset), headset);
        // Pinning survives a different system default and temporary removal.
        QCOMPARE(Player::chooseAudioOutput(headset, {speakers, headset, hdmi}, hdmi), headset);
        QCOMPARE(Player::chooseAudioOutput(headset, {speakers, hdmi}, speakers), speakers);
        QCOMPARE(Player::chooseAudioOutput(headset, {}, {}), QByteArray());
        QCOMPARE(Player::chooseAudioOutput(headset, {speakers, headset}, speakers), headset);
        QCOMPARE(Player::chooseAudioOutput({}, {speakers}, {}), speakers);
    }
    void traditionalConversionCoversHouAndPhraseAmbiguity() {
        QCOMPARE(Lrc::simplifiedChinese("我在門後 假裝你人還沒走"), QString("我在门后 假装你人还没走"));
        QCOMPARE(Lrc::simplifiedChinese("後來 皇后 之後 後面"), QString("后来 皇后 之后 后面"));
        QCOMPARE(Lrc::simplifiedChinese("乾淨 乾杯 乾燥 乾隆 乾坤"), QString("干净 干杯 干燥 乾隆 乾坤"));
        QCOMPARE(Lrc::simplifiedChinese("頭髮 發展 裡面 裏面 臺灣"), QString("头发 发展 里面 里面 台湾"));
        QCOMPARE(Lrc::simplifiedChinese("㑮"), QString("𫝈"));
        const QString original = "[00:01.230]我在門後 假裝你人還沒走\n"
                                 "[00:04.000]Мой мир / Hello world 😀";
        QCOMPARE(Lrc::simplifiedChinese(original), QString("[00:01.230]我在门后 假装你人还没走\n"
                                                           "[00:04.000]Мой мир / Hello world 😀"));
        QCOMPARE(Lrc::simplifiedChinese(original, false), original);
    }
    void traditionalLyricsPreserveOriginalAndTiming() {
        const QString original = "[ti:繁體歌詞]\n[00:01.230]這個世界，愛與夢想\n"
                                 "[00:02.500]Мой мармеладный, я не права\n"
                                 "[00:04.000]Hello world 😀\n[00:04.000]讓音樂繼續";
        const auto converted = Lrc::simplifiedChinese(original);
        QVERIFY(converted.contains("这个世界，爱与梦想"));
        QVERIFY(converted.contains("让音乐继续"));
        QCOMPARE(Lrc::simplifiedChinese(original, false), original);
        const auto before = Lrc::parse(original), after = Lrc::parse(converted);
        QCOMPARE(before.size(), after.size());
        for (int i = 0; i < before.size(); ++i)
            QCOMPARE(before[i].ms, after[i].ms);
        QVERIFY(converted.contains("Мой мармеладный, я не права"));
        QVERIFY(converted.contains("Hello world 😀"));
        QTemporaryDir dir(QDir::current().filePath("lyric-conversion-test-XXXXXX"));
        Library library(dir.path());
        const auto id = library.addTrack({{"title", "测试"}, {"path", "test.wav"}});
        QVERIFY(library.setLyrics(id, original, "LRCLIB"));
        QCOMPARE(library.track(id).value("lrc").toString(), converted);
        QCOMPARE(library.track(id).value("raw_lrc").toString(), original);
        library.setAutoSimplifyLyrics(false);
        QCOMPARE(library.track(id).value("lrc").toString(), original);
        QCOMPARE(library.tracks().first().value("lrc").toString(), original);
        library.setAutoSimplifyLyrics(true);
        QCOMPARE(library.tracks().first().value("lrc").toString(), converted);
    }
    void duplicateQueueEntriesAndReordering() {
        QTemporaryDir dir(QDir::current().filePath("duplicate-queue-XXXXXX"));
        Library library(dir.path());
        QList<qint64> ids;
        for (int i = 0; i < 3; ++i) {
            QFile file(dir.filePath(QString::number(i) + ".wav"));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("RIFF");
            file.close();
            ids << library.addTrack({{"title", QString::number(i)}, {"path", file.fileName()}});
        }
        Player player(&library);
        player.setMode(Player::Sequential);
        player.setQueue({ids[0], ids[1], ids[1], ids[2]}, 0, false);
        const auto original = player.entries();
        QCOMPARE(original.size(), 4);
        QVERIFY(original[1].key != original[2].key);
        player.moveEntry(original[2].key, Player::Bottom);
        QCOMPARE(player.pendingEntries().first().key, original[2].key);
        player.next();
        QCOMPARE(player.currentEntry(), original[2].key);
        player.next();
        QCOMPARE(player.currentEntry(), original[1].key);
        QCOMPARE(player.currentId(), ids[1]);
        QCOMPARE(player.queue().size(), 2);
        player.previous();
        QCOMPARE(player.currentEntry(), original[1].key);
        player.enqueue({ids[1]});
        player.removeEntry(original[1].key);
        QCOMPARE(player.queue().count(ids[1]), 1);
        player.enqueue({ids[0]});
        player.setMode(Player::Sequential);
        auto pending = player.pendingEntries();
        player.moveEntry(pending.first().key, Player::Top);
        QCOMPARE(player.pendingEntries().last().key, pending.first().key);
        player.moveEntry(pending.first().key, Player::Down);
        QCOMPARE(player.pendingEntries().first().key, pending.first().key);
        player.setMode(Player::Shuffle);
        pending = player.pendingEntries();
        player.moveEntry(pending.last().key, Player::Bottom);
        const auto next = player.pendingEntries().first().key;
        player.next();
        QCOMPARE(player.currentEntry(), next);
        player.setMode(Player::SingleLoop);
        QVERIFY(!player.pendingEntries().isEmpty());
        player.restoreQueue({ids[0], ids[1], ids[1]}, 2);
        QCOMPARE(player.queue().size(), 3);
        QCOMPARE(player.currentQueueIndex(), 2);
        QCOMPARE(player.currentId(), ids[1]);
        player.enqueue({ids[1], ids[1]});
        QCOMPARE(player.queue().count(ids[1]), 4);
        library.deleteTrack(ids[1]);
        QCOMPARE(player.queue().count(ids[1]), 0);
        QCOMPARE(player.currentId(), 0);
        QCOMPARE(library.tracks().size(), 2);
    }
    void bilingualLyrics() {
        const auto lines = Lrc::parse("[00:01.000]你好，世界！\n[00:01.000]Привет, мир!\n"
                                      "[00:03.000]Hello again\n[00:03.000]再次你好\n[00:05.000]");
        QCOMPARE(lines.size(), 3);
        QCOMPARE(lines[0].text, QString("Привет, мир!\n你好，世界！"));
        QCOMPARE(Lrc::bilingualLines(lines[0].text),
                 qMakePair(QString("Привет, мир!"), QString("你好，世界！")));
        const auto roundtrip = Lrc::parse(Lrc::serialize(lines));
        QCOMPARE(roundtrip.size(), lines.size());
        for (int i = 0; i < lines.size(); ++i) {
            QCOMPARE(roundtrip[i].ms, lines[i].ms);
            QCOMPARE(roundtrip[i].text, lines[i].text);
        }
        QVERIFY(Lrc::bilingualLines("单语歌词").first.isEmpty());
        QCOMPARE(Lrc::parse("[00:01.00]Same\n[00:01.00]Same").size(), 2);
        QCOMPARE(Lrc::indexAt(lines, 5000), 2);
        QVERIFY(lines[2].text.isEmpty());
    }
    void bilingualUsesOneSortedPlaybackClockAndOriginalTimestamps() {
        const auto lines = Lrc::parse("[00:05.000]有形無形\n[00:03.000]光の中\n[00:01.000]夢の続き\n"
                                      "[00:01.000]梦想仍在继续\n[00:03.200]光之中\n[00:05.000]有形无形");
        QVERIFY(Lrc::isBilingual(lines));
        QCOMPARE(Lrc::displayAt(lines, 1000, true), qMakePair(QString("夢の続き"), QString("梦想仍在继续")));
        QCOMPARE(Lrc::displayAt(lines, 3000, true), qMakePair(QString("光の中"), QString("梦想仍在继续")));
        QCOMPARE(Lrc::displayAt(lines, 3200, true), qMakePair(QString("光の中"), QString("光之中")));
        QCOMPARE(Lrc::displayAt(lines, 5000, true), qMakePair(QString("有形無形"), QString("有形无形")));
        // Seeking backward reconstructs the same pair, without a second clock.
        QCOMPARE(Lrc::displayAt(lines, 1100, true), qMakePair(QString("夢の続き"), QString("梦想仍在继续")));
        QCOMPARE(Lrc::displayAt(lines, 0, true), qMakePair(QString(), QString()));
        const auto shifted = Lrc::parse("[00:01.000]Hello\n[00:03.000]Again\n"
                                       "[00:01.200]你好\n[00:03.100]再次");
        QVERIFY(Lrc::isBilingual(shifted));
        QCOMPARE(Lrc::displayAt(shifted, 3100, true), qMakePair(QString("Again"), QString("再次")));
        QCOMPARE(Lrc::displayAt(shifted, 1000, true), qMakePair(QString("Hello"), QString()));
        const auto single = Lrc::parse("[00:01.000]第一句\n[00:03.000]第二句");
        QVERIFY(!Lrc::isBilingual(single));
        QCOMPARE(Lrc::displayAt(single, 1000, false), qMakePair(QString("第一句"), QString("第二句")));
        // No minority of all-Kanji lines may turn monolingual Japanese into translations.
        QVERIFY(!Lrc::isBilingual(Lrc::parse("[00:01]あの空\n[00:02]光\n[00:03]その先\n[00:04]夢の中\n[00:05]歩いて")));
    }
    void lyrics() {
        auto lines = Lrc::parse("[ar:Test]\n[offset:-150]\n[00:02.30][00:04.005]重复句\n[00:01.2]首句\n[00:"
                                "03.000]\n[00:89.00]无效\n");
        QCOMPARE(lines.size(), 4);
        QCOMPARE(lines[0].ms, 1050);
        QCOMPARE(lines[1].ms, 2150);
        QCOMPARE(lines[3].ms, 3855);
        QCOMPARE(Lrc::indexAt(lines, 1049), -1);
        QCOMPARE(Lrc::indexAt(lines, 1050), 0);
        QCOMPARE(Lrc::indexAt(lines, 2849), 1);
        QCOMPARE(Lrc::indexAt(lines, 2850), 2);
        QCOMPARE(lines[2].text, QString());
        QCOMPARE(Lrc::indexAt({}, 100), -1);
    }
    void playlistsAndPersistence() {
        QTemporaryDir dir(QDir::current().filePath("test-XXXXXX"));
        qint64 song = 0, second = 0, a = 0, b = 0;
        {
            Library library(dir.path());
            QVERIFY2(library.isOpen(), qPrintable(library.error()));
            song = library.addTrack({{"title", "歌曲一"}, {"path", "C:/music/a.m4a"}});
            second = library.addTrack({{"title", "歌曲二"}, {"path", "C:/music/b.flac"}});
            QVERIFY(song && second);
            QCOMPARE(library.addTrack({{"title", "重复"}, {"path", "C:/music/a.m4a"}}), song);
            a = library.createPlaylist("晨间");
            b = library.createPlaylist("夜间");
            QVERIFY(library.addToPlaylist(a, {song, second}));
            QVERIFY(library.addToPlaylist(b, {song}));
            QVERIFY(library.addToPlaylist(a, {song}));
            QCOMPARE(library.tracks(a).size(), 2);
            QCOMPARE(library.tracks(b).size(), 1);
            QCOMPARE(library.tracks().size(), 2);
            QVERIFY(library.reorder(a, {second, song}));
            QCOMPARE(library.tracks(a).first().value("id").toLongLong(), second);
            QVERIFY(library.removeFromPlaylist(b, {song}));
            QCOMPARE(library.tracks(b).size(), 0);
            QCOMPARE(library.tracks(a).size(), 2);
            QVERIFY(library.setLyrics(song, "[00:01.00]歌词", "test"));
            QVERIFY(library.setOffset(song, 250));
        }
        {
            Library library(dir.path());
            QCOMPARE(library.tracks(a).first().value("id").toLongLong(), second);
            QCOMPARE(library.track(song).value("lyric_offset").toInt(), 250);
            QVERIFY(library.deletePlaylist(a));
            QCOMPARE(library.tracks().size(), 2);
            QVERIFY(library.addToPlaylist(b, {song}));
            QVERIFY(library.deleteTrack(song));
            QCOMPARE(library.tracks(b).size(), 0);
            QCOMPARE(library.tracks().size(), 1);
            QCOMPARE(library.tracks(0, "二").size(), 1);
        }
    }
    void queueModes() {
        QTemporaryDir dir(QDir::current().filePath("test-XXXXXX"));
        Library library(dir.path());
        QList<qint64> ids;
        for (int i = 0; i < 3; ++i) {
            QString path = dir.filePath(QString::number(i) + ".wav");
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("RIFF");
            file.close();
            ids << library.addTrack({{"title", QString::number(i)}, {"path", path}});
        }
        Player player(&library);
        player.setQueue(ids, ids[0], false);
        QCOMPARE(player.currentId(), ids[0]);
        player.setMode(Player::Shuffle);
        QSet<qint64> cycle{ids[0]};
        player.next();
        cycle << player.currentId();
        player.next();
        cycle << player.currentId();
        QCOMPARE(cycle.size(), 3);
        auto current = player.currentId();
        player.setMode(Player::SingleLoop);
        player.next(true);
        QCOMPARE(player.currentId(), current);
        for (auto mode : {Player::Sequential, Player::Shuffle}) {
            player.setMode(mode);
            player.setQueue(ids, ids.first(), false);
            for (int remaining = 2; remaining >= 0; --remaining) {
                player.next(true);
                QCOMPARE(player.queue().size(), remaining);
            }
            QCOMPARE(player.currentId(), 0);
            player.next(true);
            QVERIFY(player.queue().isEmpty());
        }
        player.setQueue({ids.last()}, ids.last(), false);
        library.deleteTrack(ids.last());
        QVERIFY(!player.queue().contains(ids.last()));
        QCOMPARE(player.currentId(), 0);
    }
    void upcomingOrderAndQueueEdits() {
        QTemporaryDir dir(QDir::current().filePath("queue-test-XXXXXX"));
        Library library(dir.path());
        QList<qint64> ids;
        for (int i = 0; i < 4; ++i) {
            QFile file(dir.filePath(QString::number(i) + ".wav"));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("RIFF");
            file.close();
            ids << library.addTrack({{"title", QString::number(i)}, {"path", file.fileName()}});
        }
        Player player(&library);
        player.setMode(Player::Sequential);
        player.setQueue(ids.mid(0, 3), ids[0], false);
        QCOMPARE(player.upcoming(), QList<qint64>({ids[1], ids[2]}));
        player.enqueue({ids[3], ids[3]});
        QCOMPARE(player.currentId(), ids[0]);
        QCOMPARE(player.upcoming(), QList<qint64>({ids[1], ids[2], ids[3], ids[3]}));
        player.removeQueued(ids[1]);
        QCOMPARE(player.upcoming(), QList<qint64>({ids[2], ids[3], ids[3]}));
        QCOMPARE(library.tracks().size(), 4);
        player.jumpTo(ids[2]);
        QCOMPARE(player.currentId(), ids[2]);
        QCOMPARE(player.upcoming(), QList<qint64>({ids[3], ids[3], ids[0]}));
        player.setQueue(ids, ids[3], false);
        QCOMPARE(player.upcoming(), ids.mid(0, 3));
        player.setMode(Player::Sequential);
        QCOMPARE(player.upcoming(), ids.mid(0, 3));
        player.setMode(Player::SingleLoop);
        QCOMPARE(player.upcoming(), QList<qint64>({ids[3]}));
        player.setQueue(ids, ids[0], false);
        player.setMode(Player::Shuffle);
        const auto announced = player.upcoming();
        QCOMPARE(announced.size(), 3);
        QCOMPARE(player.upcoming(), announced);
        for (auto expected : announced) {
            player.next();
            QCOMPARE(player.currentId(), expected);
        }
        QVERIFY(player.upcoming().isEmpty());
        player.next(true);
        QVERIFY(player.queue().isEmpty());
        QCOMPARE(library.tracks().size(), 4);
        player.setQueue({});
        QVERIFY(player.upcoming().isEmpty());
        QCOMPARE(player.currentId(), 0);
    }
    void urlStorageAndDrafts() {
        QTemporaryDir dir(QDir::current().filePath("url-test-XXXXXX"));
        Library library(dir.path());
        const QVariantMap result{{"title", "在线歌曲"},
                                 {"url", "https://www.bilibili.com/video/BV1bK411W797?p=2"},
                                 {"duration", 98000}};
        const auto id = library.addUrl(result, true);
        QVERIFY(id);
        QVERIFY(library.tracks().isEmpty());
        QCOMPARE(library.stagedTracks().size(), 1);
        QCOMPARE(library.track(id).value("storage").toString(), QString("url"));
        QVERIFY(library.track(id).value("original").toString().isEmpty());
        QVERIFY(library.commitTrack(id));
        QCOMPARE(library.tracks().size(), 1);
        QCOMPARE(library.addUrl(result), id);
        QCOMPARE(library.tracks().size(), 1);
        QVERIFY(!library.addUrl({{"url", "https://example.org/video/BV1bK411W797"}}));
        const auto playlist = library.createPlaylist("URL 歌单");
        QVERIFY(library.addToPlaylist(playlist, {id}));
        Player player(&library);
        QSignalSpy requests(&player, &Player::streamRequested);
        player.setQueue({id}, 0, false);
        QCOMPARE(requests.size(), 1);
        const auto first = requests.last().at(0).toULongLong();
        QCOMPARE(requests.last().at(1).toString(), result.value("url").toString());
        player.jumpTo(id);
        QCOMPARE(requests.size(), 2);
        QVERIFY(requests.last().at(0).toULongLong() > first);
        player.acceptStream(first, QUrl("http://127.0.0.1/stale"), {});
        QVERIFY(player.media()->source().isEmpty());
        player.setQueue({});
        QVERIFY(library.deletePlaylist(playlist));
        QCOMPARE(library.tracks().size(), 1);
    }
    void queueMetadataSkipsLyricConversion() {
        QTemporaryDir dir(QDir::current().filePath("queue-lyrics-XXXXXX"));
        Library library(dir.path());
        QString lyric;
        const QString line = "[00:01.00]我在門後 假裝你人還沒走 後來這個世界愛與夢想\n";
        for (int i = 0; i < 400; ++i)
            lyric += line;
        QList<qint64> ids;
        for (int i = 0; i < 80; ++i) {
            QFile file(dir.filePath(QString::number(i) + ".wav"));
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("RIFF");
            file.close();
            const auto id = library.addTrack({{"title", QString("歌曲%1").arg(i)}, {"path", file.fileName()}});
            QVERIFY(library.setLyrics(id, lyric, "bench"));
            ids << id;
        }
        const auto header = library.trackHeader(ids.first());
        QVERIFY(!header.contains("lrc"));
        QVERIFY(!header.contains("raw_lrc"));
        QCOMPARE(header.value("title").toString(), QString("歌曲0"));
        QCOMPARE(library.trackHeaders({}).size(), 0);
        QCOMPARE(library.trackHeaders(ids).size(), ids.size());
        QVERIFY(!library.trackHeaders(ids).value(ids.first()).contains("lrc"));

        Player player(&library);
        QElapsedTimer timer;
        timer.start();
        player.enqueue(ids);
        const auto enqueueNs = timer.nsecsElapsed();
        QCOMPARE(player.queue().size(), ids.size());
        player.enqueue({ids.first(), -1});
        QCOMPARE(player.queue().size(), ids.size() + 1);
        timer.restart();
        player.setQueue(ids, ids.first(), false);
        const auto setQueueNs = timer.nsecsElapsed();
        QCOMPARE(player.queue().size(), ids.size());
        QCOMPARE(player.currentId(), ids.first());
        timer.restart();
        qint64 converted = 0;
        for (auto id : ids)
            converted += library.track(id).value("lrc").toString().size();
        const auto convertNs = timer.nsecsElapsed();
        QVERIFY(converted > lyric.size());
        QVERIFY(library.track(ids.first()).value("lrc").toString().contains("我在门后"));
        QVERIFY(library.track(ids.first()).value("raw_lrc").toString().contains("我在門後"));
        qInfo().noquote() << QString("PERF enqueue %1 tracks %2 ms; setQueue %3 ms; read+convert lyrics %4 ms")
                                 .arg(ids.size())
                                 .arg(enqueueNs / 1e6, 0, 'f', 3)
                                 .arg(setQueueNs / 1e6, 0, 'f', 3)
                                 .arg(convertNs / 1e6, 0, 'f', 3);
        QVERIFY2(enqueueNs * 3 < convertNs,
                 qPrintable(QString("enqueue %1 ns, convert %2 ns").arg(enqueueNs).arg(convertNs)));
    }
    void fileSizeScanStaysOffTheOpeningThread() {
        QTemporaryDir dir(QDir::current().filePath("filesize-scan-XXXXXX"));
        QStringList paths;
        QList<qint64> ids;
        {
            Library writer(dir.path());
            QVERIFY2(writer.isOpen(), qPrintable(writer.error()));
            for (int i = 0; i < 800; ++i) {
                const auto path = dir.filePath(QString("song-%1.bin").arg(i));
                QFile file(path);
                QVERIFY(file.open(QIODevice::WriteOnly));
                QCOMPARE(file.write(QByteArray(128, 'a')), 128);
                file.close();
                paths << path;
                const auto id = writer.addTrack({{"title", QString::number(i)}, {"path", path}, {"filesize", 0}});
                QVERIFY(id);
                ids << id;
            }
            QCOMPARE(writer.trackHeader(ids.first()).value("filesize").toLongLong(), 0);
        }
        QElapsedTimer statTimer;
        statTimer.start();
        qint64 statBytes = 0;
        for (const auto &path : paths)
            statBytes += QFileInfo(path).size();
        const auto statNs = statTimer.nsecsElapsed();
        QCOMPARE(statBytes, paths.size() * 128LL);

        QElapsedTimer openTimer;
        openTimer.start();
        Library library(dir.path());
        const auto openNs = openTimer.nsecsElapsed();
        QVERIFY2(library.isOpen(), qPrintable(library.error()));
        QCOMPARE(library.trackHeader(ids.first()).value("filesize").toLongLong(), 0);
        QCOMPARE(library.trackHeader(ids.last()).value("filesize").toLongLong(), 0);

        QElapsedTimer fillTimer;
        fillTimer.start();
        qint64 fillNs = -1;
        QObject::connect(&library, &Library::fileSizesUpdated, &library,
                         [&] { fillNs = fillTimer.nsecsElapsed(); });
        QTRY_VERIFY_WITH_TIMEOUT(fillNs >= 0, 10000);
        QCOMPARE(library.trackHeader(ids.first()).value("filesize").toLongLong(), 128);
        QCOMPARE(library.trackHeader(ids.last()).value("filesize").toLongLong(), 128);
        qint64 stored = 0;
        for (auto id : ids)
            stored += library.trackHeader(id).value("filesize").toLongLong();
        QCOMPARE(stored, statBytes);
        const QString connection = "filesize-baseline";
        qint64 oldNs = 0;
        {
            auto baseline = QSqlDatabase::addDatabase("QSQLITE", connection);
            baseline.setDatabaseName(dir.filePath("baseline.sqlite"));
            QVERIFY(baseline.open());
            QSqlQuery setup(baseline);
            QVERIFY(setup.exec("CREATE TABLE tracks (id INTEGER PRIMARY KEY, path TEXT, storage TEXT, filesize INTEGER)"));
            QVERIFY(setup.exec("BEGIN"));
            for (const auto &path : paths) {
                setup.prepare("INSERT INTO tracks(path,storage,filesize) VALUES(?,'local',0)");
                setup.addBindValue(path);
                QVERIFY(setup.exec());
            }
            QVERIFY(setup.exec("COMMIT"));
            QElapsedTimer oldTimer;
            oldTimer.start();
            QSqlQuery sizes(baseline);
            QVERIFY(sizes.exec("SELECT id,path FROM tracks WHERE storage='local' AND filesize=0"));
            while (sizes.next()) {
                QSqlQuery update(baseline);
                update.prepare("UPDATE tracks SET filesize=? WHERE id=?");
                update.addBindValue(QFileInfo(sizes.value(1).toString()).size());
                update.addBindValue(sizes.value(0));
                QVERIFY(update.exec());
            }
            oldNs = oldTimer.nsecsElapsed();
            baseline.close();
        }
        QSqlDatabase::removeDatabase(connection);
        qInfo().noquote()
            << QString("PERF file-size open %1 ms (still 0); background fill %2 ms; old ui-thread stat+update %3 ms; stat only %4 ms; files %5")
                   .arg(openNs / 1e6, 0, 'f', 3)
                   .arg(fillNs / 1e6, 0, 'f', 3)
                   .arg(oldNs / 1e6, 0, 'f', 3)
                   .arg(statNs / 1e6, 0, 'f', 3)
                   .arg(paths.size());
    }
    void fileSizeScanIgnoresMissingFilesAndSurvivesDestruction() {
        QTemporaryDir dir(QDir::current().filePath("filesize-missing-XXXXXX"));
        {
            Library library(dir.path());
            const auto id = library.addTrack(
                {{"title", "gone"}, {"path", dir.filePath("missing.wav")}, {"filesize", 0}});
            QVERIFY(id);
            QSignalSpy updated(&library, &Library::fileSizesUpdated);
            QTest::qWait(250);
            QCOMPARE(updated.count(), 0);
            QCOMPARE(library.trackHeader(id).value("filesize").toLongLong(), 0);
            QCoreApplication::processEvents();
        }
        QThreadPool::globalInstance()->waitForDone(2000);
        QCoreApplication::processEvents();

        const auto alive = dir.filePath("alive");
        {
            Library writer(alive);
            for (int i = 0; i < 40; ++i) {
                const auto path = dir.filePath(QString("alive/%1.bin").arg(i));
                QFile file(path);
                QVERIFY(file.open(QIODevice::WriteOnly));
                file.write(QByteArray(32, 'b'));
                file.close();
                QVERIFY(writer.addTrack({{"title", QString::number(i)}, {"path", path}, {"filesize", 0}}));
            }
        }
        {
            Library transient(alive);
            QCoreApplication::processEvents();
        }
        QThreadPool::globalInstance()->waitForDone(2000);
        QCoreApplication::processEvents();
    }
    void urlResumeReResolvesOnlyAfterExpiry() {
        QTemporaryDir dir(QDir::current().filePath("url-expiry-XXXXXX"));
        Library library(dir.path());
        const auto id = library.addUrl({{"title", "在线"}, {"url", "https://www.bilibili.com/video/BV1bK411W797"}});
        QVERIFY(id);
        const auto future = QDateTime::currentDateTimeUtc().addSecs(600);
        const auto past = QDateTime::currentDateTimeUtc().addSecs(-5);
        const auto requestsAfter = [&](const QDateTime &expiry, bool expire, int *count) {
            Player player(&library);
            QSignalSpy requests(&player, &Player::streamRequested);
            player.setQueue({id}, 0, false);
            if (requests.size() != 1)
                return false;
            const auto token = requests.first().at(0).toULongLong();
            player.acceptStream(token, QUrl("http://127.0.0.1/audio.m4a"), {});
            player.noteStreamExpiry(token + 1, past);
            player.noteStreamExpiry(token, future);
            if (expire)
                player.expireResolvedStream();
            else if (expiry.isValid())
                player.noteStreamExpiry(token, expiry);
            player.toggle();
            *count = int(requests.size());
            return true;
        };
        int count = 0;
        QVERIFY(requestsAfter(future, false, &count));
        QCOMPARE(count, 1);
        QVERIFY(requestsAfter(past, false, &count));
        QCOMPARE(count, 2);
        QVERIFY(requestsAfter({}, true, &count));
        QCOMPARE(count, 2);

        Player player(&library);
        QSignalSpy requests(&player, &Player::streamRequested);
        player.setQueue({id}, 0, false);
        player.setMode(Player::SingleLoop);
        player.next(true);
        QCOMPARE(requests.size(), 2);
        QCOMPARE(player.currentId(), id);
        QCOMPARE(player.queue().size(), 1);
    }
};
QTEST_MAIN(CoreTests)
#include "core_tests.moc"
