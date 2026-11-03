#include "lyricworkflow.h"
#include "theme.h"
#include "window.h"
#include <QBoxLayout>
#include <QHeaderView>
#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSplitter>
#include <algorithm>
#include <memory>

#include "uihelpers.h"
using namespace Ui;

QWidget *MainWindow::createSearchPage() {
    auto page = new QWidget;
    page->setObjectName("searchPage");
    auto layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    auto searchRow = new QHBoxLayout;
    m_input = new QLineEdit;
    m_input->setObjectName("onlineSearchInput");
    m_input->setPlaceholderText("输入歌曲名称、BV 号或视频链接");
    m_searchButton = action("搜索", "primary");
    m_searchButton->setObjectName("onlineSearchButton");
    searchRow->addWidget(m_input, 1);
    searchRow->addWidget(m_searchButton);
    auto searchHeader = new QWidget;
    searchHeader->setLayout(searchRow);
    searchRow->setContentsMargins(18, 0, 0, 0);
    m_headerTools->addWidget(searchHeader);
    auto catalog = new QWidget;
    auto left = new QVBoxLayout(catalog);
    left->setContentsMargins(0, 0, 8, 0);
    auto resultHeader = new QHBoxLayout;
    resultHeader->addWidget(label("歌曲搜索", "section"));
    resultHeader->addStretch();
    auto urlCollect = action("URL 收录", "collectSearchUrl");
    auto selected = action("下载", "primary");
    selected->setObjectName("downloadSelected");
    resultHeader->addWidget(urlCollect);
    resultHeader->addWidget(selected);
    if (!lyricFeaturesEnabled()) {
        auto trial = action("试听", "trialSearchResult");
        resultHeader->addWidget(trial);
        connect(trial, &QPushButton::clicked, this, [this] {
            const auto item = m_results->currentItem();
            if (!item)
                return;
            const auto id = m_library->addUrl(item->data(0, Qt::UserRole).toMap(), true);
            if (id)
                m_player.setQueue({id});
        });
    }
    left->addLayout(resultHeader);
    m_resultStatus = label("", "muted");
    m_resultStatus->setWordWrap(true);
    left->addWidget(m_resultStatus);
    m_results = new QTreeWidget;
    m_results->setObjectName("searchResults");
    m_results->setColumnCount(4);
    m_results->setHeaderLabels({"封面", "视频标题", "时长", "UP主"});
    m_results->setRootIsDecorated(false);
    m_results->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_results->setIconSize(QSize(96, 54));
    m_results->header()->setSectionResizeMode(0, QHeaderView::Fixed);
    m_results->header()->setStretchLastSection(false);
    m_results->header()->resizeSection(0, 112);
    m_results->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_results->header()->resizeSection(2, 70);
    m_results->header()->resizeSection(3, 100);
    left->addWidget(m_results, 1);
    auto pages = new QHBoxLayout;
    m_previousPage = action("← 上一页");
    m_nextPage = action("下一页 →");
    m_previousPage->setObjectName("previousSearchPage");
    m_nextPage->setObjectName("nextSearchPage");
    m_pageNumber = label("第 1 页", "muted");
    pages->addStretch();
    pages->addWidget(m_previousPage);
    pages->addWidget(m_pageNumber);
    pages->addWidget(m_nextPage);
    pages->addStretch();
    m_previousPage->setEnabled(false);
    m_nextPage->setEnabled(false);
    left->addLayout(pages);
    m_matchAfterDownload = new QCheckBox(page);
    m_matchAfterDownload->hide();
    m_settings->setValue("lyrics/matchAfterDownload", false);
    auto downloadPanel = new QWidget;
    downloadPanel->setObjectName("downloadPanel");
    downloadPanel->setMinimumWidth(300);
    auto downloadLayout = new QVBoxLayout(downloadPanel);
    downloadLayout->setContentsMargins(16, 0, 0, 0);
    auto jobs = new QHBoxLayout;
    jobs->addWidget(label("收录队列", "section"));
    jobs->addStretch();
    auto retry = action("重试下载"), cancel = action("取消下载");
    jobs->addWidget(retry);
    jobs->addWidget(cancel);
    downloadLayout->addLayout(jobs);
    m_jobs = new QListWidget;
    m_jobs->setObjectName("downloadJobs");
    m_jobs->setStyleSheet("QListWidget#downloadJobs::item{padding:0;}");
    m_jobs->setMinimumHeight(55);
    downloadLayout->addWidget(m_jobs, 1);
    connect(m_jobs, &QListWidget::itemDoubleClicked, this, &MainWindow::openDownload);
    if (lyricFeaturesEnabled()) {
        for (const auto &track : m_library->stagedTracks())
            completeDownload((track.value("storage").toString() == "url" ? "url-" : "saved-") +
                                 track.value("id").toString(), track.value("id").toLongLong());
    }
    auto searchSplit = new QSplitter;
    searchSplit->setObjectName("searchDownloadsSplit");
    searchSplit->setChildrenCollapsible(false);
    searchSplit->addWidget(catalog);
    searchSplit->addWidget(downloadPanel);
    searchSplit->setStretchFactor(0, 3);
    searchSplit->setStretchFactor(1, 1);
    searchSplit->setSizes({680, 360});
    layout->addWidget(searchSplit, 1);
    connect(m_searchButton, &QPushButton::clicked, this, [this] { beginSearch(); });
    connect(m_input, &QLineEdit::returnPressed, this, [this] {
        m_focusResultsAfterSearch = true;
        m_searchButton->click();
    });
    connect(m_previousPage, &QPushButton::clicked, this, [this] { beginSearch(m_resultPage - 1); });
    connect(m_nextPage, &QPushButton::clicked, this, [this] { beginSearch(m_resultPage + 1); });
    connect(m_results, &QTreeWidget::itemDoubleClicked, this, &MainWindow::chooseSearchResult);
    connect(urlCollect, &QPushButton::clicked, this, &MainWindow::collectUrls);
    connect(selected, &QPushButton::clicked, this, [this] { downloadResults(); });
    connect(m_matchAfterDownload, &QCheckBox::toggled, this,
            [this](bool on) { m_settings->setValue("lyrics/matchAfterDownload", on); });
    connect(retry, &QPushButton::clicked, &m_import, &ImportService::retryFailed);
    connect(cancel, &QPushButton::clicked, &m_import, &ImportService::cancel);
    return page;
}
void MainWindow::beginSearch(int page) {
    if (page <= 1)
        m_searchQuery = m_input->text().trimmed();
    if (m_searchQuery.isEmpty())
        return;
    m_lookup.search(m_searchQuery, qMax(1, page));
}
void MainWindow::showSearchResults(const QList<QVariantMap> &items, int page, bool hasNext, bool video) {
    ++m_thumbnailGeneration;
    const auto generation = m_thumbnailGeneration;
    m_results->clear();
    m_resultPage = page;
    m_resultHasNext = hasNext;
    m_resultIsVideo = video;
    m_pageNumber->setText(video ? QString("%1 个分 P").arg(items.size()) : QString("第 %1 页").arg(page));
    m_previousPage->setEnabled(!video && page > 1);
    m_nextPage->setEnabled(!video && hasNext);
    const auto placeholder = emptyCover(UiTheme::forWidget(this));
    QSet<QString> covers;
    for (const auto &row : items) {
        auto item = new QTreeWidgetItem(m_results, {QString(), row.value("title").toString(),
                                                    displayTime(row.value("duration").toLongLong()),
                                                    row.value("uploader").toString()});
        item->setData(0, Qt::UserRole, row);
        item->setIcon(0, placeholder);
        item->setSizeHint(0, QSize(110, 72));
        item->setToolTip(1, row.value("title").toString() + "\n" + row.value("bvid").toString() + "\n" +
                                row.value("uploader").toString());
        if (!row.value("thumbnail").toString().isEmpty())
            covers.insert(row.value("thumbnail").toString());
    }
    if (m_results->topLevelItemCount())
        m_results->setCurrentItem(m_results->topLevelItem(0));
    if (m_focusResultsAfterSearch && m_pages->currentIndex() == 0 &&
        (QApplication::focusWidget() == m_input || QApplication::focusWidget() == m_results))
        m_results->setFocus(Qt::OtherFocusReason);
    m_focusResultsAfterSearch = false;
    for (const auto &url : covers) {
        QNetworkRequest request{QUrl(url)};
        request.setTransferTimeout(15000);
        request.setRawHeader("Referer", "https://www.bilibili.com/");
        auto reply = m_thumbnails.get(request);
        connect(reply, &QNetworkReply::finished, this, [this, reply, url, generation] {
            reply->deleteLater();
            if (generation != m_thumbnailGeneration || reply->error() != QNetworkReply::NoError)
                return;
            QPixmap pixmap;
            if (!pixmap.loadFromData(reply->readAll()))
                return;
            auto icon = QIcon(pixmap.scaled(96, 54, Qt::KeepAspectRatio, Qt::SmoothTransformation));
            for (int i = 0; i < m_results->topLevelItemCount(); ++i) {
                auto item = m_results->topLevelItem(i);
                if (item->data(0, Qt::UserRole).toMap().value("thumbnail").toString() == url) {
                    item->setData(0, Qt::UserRole + 1, true);
                    item->setIcon(0, icon);
                }
            }
        });
    }
}
void MainWindow::downloadResults() {
    const auto items = m_results->selectedItems();
    if (items.isEmpty()) {
        m_resultStatus->setText("请先选择要下载的结果");
        return;
    }
    applyLyricSource();
    for (auto item : items)
        m_import.enqueue(item->data(0, Qt::UserRole).toMap().value("url").toString(),
                         lyricFeaturesEnabled());
}
void MainWindow::completeDownload(const QString &taskId, qint64 trackId) {
    if (!lyricFeaturesEnabled())
        return;
    const auto track = m_library->track(trackId);
    if (track.isEmpty())
        return;
    if (!track.value("staged").toBool()) {
        delete m_jobItems.take(taskId);
        return;
    }
    const bool url = track.value("storage").toString() == "url";
    updateDownloadTask(taskId, track.value("title").toString() + (url ? " [URL]" : ""),
                       url ? "URL 待收录" : "下载完成", trackId);
}
void MainWindow::updateDownloadTask(const QString &taskId, const QString &title, const QString &state,
                                    qint64 trackId) {
    auto item = m_jobItems.value(taskId);
    if (!item) {
        item = new QListWidgetItem;
        m_jobs->insertItem(0, item);
        m_jobItems[taskId] = item;
    }
    if (trackId)
        item->setData(Qt::UserRole, trackId);
    item->setToolTip(title + " · " + state);
    auto cell = m_jobs->itemWidget(item);
    if (!cell) {
        cell = new QWidget;
        cell->setObjectName("downloadEntry");
        auto card = new QVBoxLayout(cell);
        card->setContentsMargins(12, 10, 12, 10);
        card->setSpacing(6);
        auto name = label({}, "downloadTaskTitle");
        name->setWordWrap(true);
        name->setMinimumHeight(30);
        name->setMaximumHeight(name->fontMetrics().lineSpacing() * 2 + 4);
        name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        name->setAttribute(Qt::WA_TransparentForMouseEvents);
        card->addWidget(name, 1);
        auto status = label({}, "muted");
        status->setProperty("downloadStatus", true);
        status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        status->setAttribute(Qt::WA_TransparentForMouseEvents);
        card->addWidget(status);
        auto actions = new QHBoxLayout;
        actions->setSpacing(8);
        actions->addStretch();
        const QStringList names{"discardDownloadedTrack", "collectDownloadedTrack", "openDownloadedTrack"};
        const QStringList captions{"删除", "收录到乐库", "工作台"};
        for (int type = 0; type < 3; ++type) {
            auto button = action({}, names[type]);
            button->setProperty("downloadAction", type);
            button->setToolTip(captions[type]);
            button->setAccessibleName(captions[type]);
            button->setFixedSize(44, 32);
            button->setIconSize(QSize(20, 20));
            button->setStyleSheet("padding:0;");
            button->setIcon(downloadActionIcon(type, UiTheme::forWidget(this), devicePixelRatioF()));
            actions->addWidget(button);
            if (type == 0)
                connect(button, &QPushButton::clicked, this, [this, taskId] {
                    auto current = m_jobItems.value(taskId);
                    if (!current)
                        return;
                    if (current->data(Qt::UserRole).toLongLong())
                        discardDownload(current);
                    else
                        m_import.cancelTask(taskId);
                });
            else if (type == 1)
                connect(button, &QPushButton::clicked, this, [this, taskId] {
                    auto current = m_jobItems.value(taskId);
                    const auto id = current ? current->data(Qt::UserRole).toLongLong() : 0;
                    if (id && m_pendingCommit == id)
                        m_pendingCommit = 0;
                    if (id && m_library->saveWorkbench(id))
                        finishWorkbenchImport(id, "已导入乐库");
                });
            else
                connect(button, &QPushButton::clicked, this,
                        [this, taskId] { openDownload(m_jobItems.value(taskId)); });
        }
        card->addLayout(actions);
        item->setSizeHint(QSize(0, 128));
        m_jobs->setItemWidget(item, cell);
    }
    cell->findChild<QLabel *>("downloadTaskTitle")->setText(title);
    for (auto status : cell->findChildren<QLabel *>())
        if (status->property("downloadStatus").toBool())
            status->setText(state);
    const bool ready = item->data(Qt::UserRole).toLongLong() != 0;
    cell->findChild<QPushButton *>("collectDownloadedTrack")->setEnabled(ready);
    cell->findChild<QPushButton *>("openDownloadedTrack")->setEnabled(ready && lyricFeaturesEnabled());
}
void MainWindow::discardDownload(QListWidgetItem *item) {
    if (!item)
        return;
    const auto id = item->data(Qt::UserRole).toLongLong();
    const auto track = m_library->track(id);
    if (!id || track.isEmpty() || !track.value("staged").toBool())
        return;
    QStringList files;
    // Only downloaded UUID-named files belong to this task; imported local sources stay intact.
    const auto original = QFileInfo(track.value("original").toString());
    static const QRegularExpression ownedName("^([0-9a-fA-F-]{36})\\.source\\.");
    const auto match = ownedName.match(original.fileName());
    const QUrl source(track.value("source").toString());
    if (source.scheme() == "https" && match.hasMatch()) {
        const auto root = original.dir().canonicalPath();
        if (!root.isEmpty())
            for (const auto &file : QDir(root).entryInfoList(QDir::Files | QDir::Hidden | QDir::NoSymLinks))
                if (file.fileName().startsWith(match.captured(1) + ".") && file.dir().canonicalPath() == root)
                    files << file.absoluteFilePath();
    } else {
        const QFileInfo rendered(track.value("path").toString());
        static const QRegularExpression generatedName("^([0-9a-fA-F-]{36})\\.");
        const auto generated = generatedName.match(rendered.fileName());
        if (original.isFile() && original.canonicalFilePath() != rendered.canonicalFilePath() &&
            generated.hasMatch()) {
            const auto root = rendered.dir().canonicalPath();
            if (!root.isEmpty())
                for (const auto &file :
                     QDir(root).entryInfoList(QDir::Files | QDir::Hidden | QDir::NoSymLinks))
                    if (file.fileName().startsWith(generated.captured(1) + ".") &&
                        file.dir().canonicalPath() == root &&
                        file.canonicalFilePath() != original.canonicalFilePath())
                        files << file.absoluteFilePath();
        }
    }
    if (m_pendingCommit == id)
        m_pendingCommit = 0;
    if (m_lyricTrack == id)
        m_lyrics.cancel();
    m_lyricQueue->cancelTrack(id);
    m_workbenchTracks.remove(id);
    m_library->deleteTrack(id);
    QStringList remaining;
    for (const auto &file : files)
        if (QFile::exists(file) && !QFile::remove(file))
            remaining << file;
    if (!remaining.isEmpty())
        QTimer::singleShot(150, this, [this, remaining] {
            QStringList locked;
            for (const auto &file : remaining)
                if (QFile::exists(file) && !QFile::remove(file))
                    locked << file;
            if (!locked.isEmpty())
                notify("任务已移除，部分下载文件仍被占用：" + locked.join("；"));
        });
}
void MainWindow::openDownload(QListWidgetItem *item) {
    if (!item || !lyricFeaturesEnabled())
        return;
    const auto id = item->data(Qt::UserRole).toLongLong();
    const auto track = m_library->track(id);
    if (!id || track.isEmpty())
        return;
    m_workbenchTracks.insert(id);
    refreshLyricLibrary();
    selectLyricTrack(id, false);
    navigate(3);
}
void MainWindow::collectUrls() {
    const auto items = m_results->selectedItems();
    int count = 0;
    for (auto item : items) {
        const auto id = m_library->addUrl(item->data(0, Qt::UserRole).toMap(), true);
        if (id && m_library->track(id).value("staged").toBool()) {
            completeDownload("url-" + QString::number(id), id);
            ++count;
        }
    }
    m_resultStatus->setText(count ? QString("已加入收录队列 %1 首 URL 歌曲").arg(count)
                                 : "请选择尚未收录的歌曲；已在乐库的 URL 可右击载入工作台。");
}
