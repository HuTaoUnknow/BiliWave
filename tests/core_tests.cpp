#include "library.h"
#include "lrc.h"
#include "player.h"
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>
class CoreTests : public QObject {
    Q_OBJECT
  private slots:
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
        player.setMode(Player::ListLoop);
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
        for (auto mode : {Player::Sequential, Player::ListLoop, Player::Shuffle}) {
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
        player.setMode(Player::ListLoop);
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
};
QTEST_MAIN(CoreTests)
#include "core_tests.moc"
