#include "player.h"
#include <QFileInfo>
#include <QRandomGenerator>
Player::Player(Library *l, QObject *p) : QObject(p), m_library(l) {
    m_media.setAudioOutput(&m_audio);
    m_audio.setVolume(.75);
    connect(&m_media, &QMediaPlayer::mediaStatusChanged, this, [this](auto status) {
        if (status == QMediaPlayer::EndOfMedia)
            next(true);
    });
    connect(&m_media, &QMediaPlayer::errorOccurred, this,
            [this](auto, const QString &s) { emit failure(s); });
    connect(l, &Library::changed, this, [this] {
        for (int i = m_queue.size() - 1; i >= 0; --i)
            if (m_library->track(m_queue[i].track).isEmpty())
                m_queue.removeAt(i);
        if (m_currentEntry && entryIndex(m_currentEntry) < 0) {
            cancelStream();
            m_media.stop();
            m_media.setSource({});
            m_current = 0;
            m_currentEntry = 0;
            emit currentChanged(0);
        }
        for (int i = m_shuffleBag.size() - 1; i >= 0; --i)
            if (entryIndex(m_shuffleBag[i]) < 0)
                m_shuffleBag.removeAt(i);
        prepareShuffle();
        emit queueChanged();
    });
}
void Player::setVolume(float volume) {
    volume = qBound(0.f, volume, 1.f);
    if (m_volume == volume)
        return;
    m_volume = volume;
    m_audio.setVolume(m_volume * m_duckingGain);
    emit volumeChanged(m_volume);
}
void Player::setDuckingGain(float gain) {
    m_duckingGain = qBound(0.f, gain, 1.f);
    m_audio.setVolume(m_volume * m_duckingGain);
}
int Player::entryIndex(quint64 key) const {
    for (int i = 0; i < m_queue.size(); ++i)
        if (m_queue[i].key == key)
            return i;
    return -1;
}
int Player::currentQueueIndex() const {
    return entryIndex(m_currentEntry);
}
void Player::prepareShuffle() {
    if (m_mode != Shuffle || !m_shuffleBag.isEmpty() || m_queue.isEmpty())
        return;
    for (const auto &entry : m_queue)
        if (entry.key != m_currentEntry)
            m_shuffleBag << entry.key;
    for (int i = m_shuffleBag.size() - 1; i > 0; --i)
        m_shuffleBag.swapItemsAt(i, QRandomGenerator::global()->bounded(i + 1));
}
void Player::setMode(Mode mode) {
    m_mode = mode;
    m_shuffleBag.clear();
    prepareShuffle();
    emit queueChanged();
}
QList<Player::QueueEntry> Player::orderedPending() const {
    if (m_queue.isEmpty())
        return {};
    const int current = currentQueueIndex();
    auto order = m_queue.mid(current + 1);
    if (current >= 0)
        order += m_queue.mid(0, current);
    return order;
}
QList<Player::QueueEntry> Player::pendingEntries() const {
    if (m_mode != Shuffle)
        return orderedPending();
    QList<QueueEntry> entries;
    for (auto key : m_shuffleBag) {
        const int index = entryIndex(key);
        if (index >= 0 && key != m_currentEntry)
            entries << m_queue[index];
    }
    return entries;
}
QList<qint64> Player::upcoming() const {
    if (m_queue.isEmpty())
        return {};
    if (m_mode == SingleLoop && m_currentEntry)
        return {m_current};
    QList<qint64> ids;
    for (const auto &entry : pendingEntries())
        ids << entry.track;
    return ids;
}
void Player::enqueue(const QList<qint64> &ids) {
    for (auto id : ids)
        if (!m_library->track(id).isEmpty()) {
            const auto key = ++m_nextEntry;
            m_queue << QueueEntry{key, id};
            if (m_mode == Shuffle)
                m_shuffleBag << key;
        }
    if (!m_currentEntry && !m_queue.isEmpty())
        activate(m_queue.first().key);
    prepareShuffle();
    emit queueChanged();
}
void Player::removeQueued(qint64 id) {
    // Legacy callers identify a song. Prefer one waiting occurrence.
    for (const auto &entry : pendingEntries())
        if (entry.track == id) {
            removeEntry(entry.key);
            return;
        }
    for (const auto &entry : m_queue)
        if (entry.track == id) {
            removeEntry(entry.key);
            return;
        }
}
void Player::removeEntry(quint64 key) {
    const int index = entryIndex(key);
    if (index < 0)
        return;
    auto pending = pendingEntries();
    const bool current = key == m_currentEntry;
    const bool playing = m_media.playbackState() == QMediaPlayer::PlayingState;
    m_queue.removeAt(index);
    m_shuffleBag.removeAll(key);
    m_history.removeAll(key);
    if (m_queue.isEmpty()) {
        setQueue({});
        return;
    }
    if (current) {
        m_media.stop();
        m_currentEntry = 0;
        m_current = 0;
        activate(!pending.isEmpty() ? pending.first().key : m_queue.first().key, playing);
        if (!m_currentEntry) {
            m_media.setSource({});
            emit currentChanged(0);
        }
    }
    prepareShuffle();
    emit queueChanged();
}
void Player::jumpTo(qint64 id) {
    for (const auto &entry : m_queue)
        if (entry.track == id) {
            jumpToEntry(entry.key);
            return;
        }
}
void Player::jumpToEntry(quint64 key) {
    if (entryIndex(key) < 0)
        return;
    if (m_currentEntry && m_currentEntry != key)
        m_history << m_currentEntry;
    activate(key);
}
void Player::moveEntry(quint64 key, QueueMove move) {
    auto pending = pendingEntries();
    int from = -1;
    for (int i = 0; i < pending.size(); ++i)
        if (pending[i].key == key)
            from = i;
    if (from < 0)
        return;
    // The visible queue runs upwards; its bottom waiting row plays first.
    const int to = move == Top      ? pending.size() - 1
                   : move == Bottom ? 0
                                    : qBound(0, from + (move == Up ? 1 : -1), int(pending.size()) - 1);
    if (to == from)
        return;
    pending.move(from, to);
    if (m_mode == Shuffle) {
        m_shuffleBag.clear();
        for (const auto &entry : pending)
            m_shuffleBag << entry.key;
    }
    const int current = currentQueueIndex();
    QList<QueueEntry> ordered;
    if (current >= 0)
        ordered << m_queue[current];
    ordered += pending;
    m_queue = ordered;
    emit queueChanged();
}
void Player::setQueue(const QList<qint64> &ids, qint64 start, bool play, int startIndex) {
    cancelStream();
    m_media.stop();
    m_queue.clear();
    m_currentEntry = 0;
    m_current = 0;
    for (auto id : ids)
        if (!m_library->track(id).isEmpty())
            m_queue << QueueEntry{++m_nextEntry, id};
    m_history.clear();
    m_shuffleBag.clear();
    if (m_queue.isEmpty()) {
        m_media.stop();
        m_media.setSource({});
        emit currentChanged(0);
        emit queueChanged();
        return;
    }
    auto key = m_queue.first().key;
    for (const auto &entry : m_queue)
        if (entry.track == start) {
            key = entry.key;
            break;
        }
    if (startIndex >= 0 && startIndex < m_queue.size())
        key = m_queue[startIndex].key;
    activate(key, play);
    if (!m_currentEntry) {
        m_media.setSource({});
        emit currentChanged(0);
    }
    emit queueChanged();
}
void Player::restoreQueue(const QList<qint64> &ids, int currentIndex) {
    setQueue(ids, 0, false, currentIndex);
}
void Player::playPlaylist(qint64 id) {
    QList<qint64> ids;
    for (const auto &t : m_library->tracks(id))
        ids << t.value("id").toLongLong();
    setQueue(ids);
}
void Player::activate(quint64 key, bool play) {
    const int index = entryIndex(key);
    if (index < 0)
        return;
    const auto id = m_queue[index].track;
    const auto t = m_library->track(id);
    const auto path = t.value("path").toString();
    const bool remote = t.value("storage").toString() == "url";
    if (!remote && !QFileInfo::exists(path)) {
        emit failure(tr("音频文件不存在：%1").arg(path));
        return;
    }
    m_current = id;
    m_currentEntry = key;
    cancelStream();
    m_shuffleBag.removeAll(key);
    prepareShuffle();
    if (remote) {
        m_media.stop();
        m_media.setSource({});
        m_loadingStream = true;
        m_streamPlay = play;
    } else {
        const auto source = QUrl::fromLocalFile(path);
        if (m_media.source() == source) {
            m_media.stop();
            m_media.setPosition(0);
        }
        m_media.setSource(source);
    }
    emit currentChanged(id);
    emit queueChanged();
    if (remote)
        emit streamRequested(m_streamToken, t.value("source").toString());
    else if (play)
        m_media.play();
}
void Player::cancelStream() {
    ++m_streamToken;
    m_loadingStream = false;
}
void Player::acceptStream(quint64 token, const QUrl &url, const QString &error) {
    if (token != m_streamToken || !m_loadingStream)
        return;
    m_loadingStream = false;
    if (!error.isEmpty() || !url.isValid()) {
        emit failure(error.isEmpty() ? tr("在线音轨不可用") : error);
        return;
    }
    m_media.setSource(url);
    if (m_streamPlay)
        m_media.play();
}
void Player::toggle() {
    if (m_loadingStream) {
        m_streamPlay = !m_streamPlay;
        return;
    }
    if (m_media.playbackState() == QMediaPlayer::PlayingState)
        m_media.pause();
    else if (m_current)
        if (m_library->track(m_current).value("storage").toString() == "url" &&
            m_media.error() != QMediaPlayer::NoError)
            activate(m_currentEntry);
        else
            m_media.play();
    else if (!m_queue.isEmpty())
        activate(m_queue.first().key);
}
void Player::next(bool automatic) {
    if (m_queue.isEmpty())
        return;
    if (automatic && m_mode == SingleLoop && m_currentEntry) {
        if (m_library->track(m_current).value("storage").toString() == "url") {
            activate(m_currentEntry);
            return;
        }
        m_media.setPosition(0);
        m_media.play();
        return;
    }
    const auto pending = pendingEntries();
    const int current = currentQueueIndex();
    const auto consumed = m_currentEntry;
    if (current >= 0)
        m_queue.removeAt(current);
    m_shuffleBag.removeAll(consumed);
    m_history.removeAll(consumed);
    m_currentEntry = 0;
    m_current = 0;
    if (pending.isEmpty()) {
        setQueue({});
        return;
    }
    activate(pending.first().key);
    if (!m_currentEntry) {
        m_media.stop();
        m_media.setSource({});
        emit currentChanged(0);
    }
    emit queueChanged();
}

void Player::previous() {
    while (!m_history.isEmpty() && entryIndex(m_history.last()) < 0)
        m_history.removeLast();
    if (!m_history.isEmpty()) {
        activate(m_history.takeLast());
        return;
    }
    if (m_queue.isEmpty())
        return;
    if (m_currentEntry)
        m_media.setPosition(0);
    else
        activate(m_queue.first().key);
}
