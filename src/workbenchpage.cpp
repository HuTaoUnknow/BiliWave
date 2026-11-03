#include "lyricworkflow.h"
#include "theme.h"
#include "window.h"
#include <QBoxLayout>
#include <QHeaderView>
#include <QActionGroup>
#include <QButtonGroup>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QInputDialog>
#include <QMenu>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QSplitter>
#include <algorithm>
#include <memory>

#include "uihelpers.h"
#include <QAction>
using namespace Ui;

namespace {
// Keep the added timeline controls available when the workbench is narrow.
class WorkbenchActionsLayout : public QLayout {
  public:
    explicit WorkbenchActionsLayout(QWidget *parent) : QLayout(parent) {
        setContentsMargins(10, 0, 8, 0);
        setSpacing(6);
    }
    ~WorkbenchActionsLayout() override {
        while (auto item = takeAt(0)) delete item;
    }
    void addItem(QLayoutItem *item) override { m_items << item; }
    int count() const override { return int(m_items.size()); }
    QLayoutItem *itemAt(int index) const override { return m_items.value(index); }
    QLayoutItem *takeAt(int index) override {
        return index >= 0 && index < m_items.size() ? m_items.takeAt(index) : nullptr;
    }
    Qt::Orientations expandingDirections() const override { return Qt::Horizontal; }
    bool hasHeightForWidth() const override { return true; }
    int heightForWidth(int width) const override { return arrange(QRect(0, 0, width, 0), false); }
    QSize minimumSize() const override {
        QSize size;
        for (auto item : m_items) size = size.expandedTo(item->minimumSize());
        const auto margins = contentsMargins();
        return size + QSize(margins.left() + margins.right(), margins.top() + margins.bottom());
    }
    QSize sizeHint() const override { return minimumSize(); }
    void setGeometry(const QRect &rect) override {
        QLayout::setGeometry(rect);
        arrange(rect, true);
    }
  private:
    int arrange(const QRect &rect, bool apply) const {
        const auto margins = contentsMargins();
        const auto area = rect.marginsRemoved(margins);
        int x = area.left(), y = area.top(), lineHeight = 0;
        for (auto item : m_items) {
            const auto size = item->sizeHint().expandedTo(item->minimumSize());
            if (lineHeight && x + size.width() > area.right() + 1) {
                x = area.left();
                y += lineHeight + spacing();
                lineHeight = 0;
            }
            if (item->widget()->property("alignWorkbenchRight").toBool())
                x = qMax(x, area.right() + 1 - size.width());
            if (apply) item->setGeometry(QRect(QPoint(x, y), size));
            x += size.width() + spacing();
            lineHeight = qMax(lineHeight, size.height());
        }
        return y + lineHeight - rect.top() + margins.bottom();
    }
    QList<QLayoutItem *> m_items;
};

class WorkbenchPanelSplitter : public QSplitter {
  public:
    explicit WorkbenchPanelSplitter(QSettings *settings) : m_settings(settings) {
        setObjectName("workbenchPanels");
        const auto saved = settings->value("ui/workbenchPanelRatios").toList();
        if (saved.size() == 2 && saved[0].toDouble() > 0 && saved[1].toDouble() > 0) {
            const double sum = saved[0].toDouble() + saved[1].toDouble();
            for (int i = 0; i < 2; ++i)
                m_ratios[i] = saved[i].toDouble() / sum;
        } else
            settings->setValue("ui/workbenchPanelRatios", QVariantList{.75, .25});
        connect(this, &QSplitter::splitterMoved, this, [this] {
            if (m_applying)
                return;
            const auto values = sizes();
            const int sum = values.value(0) + values.value(1);
            if (sum <= 0 || values.contains(0))
                return;
            QVariantList saved;
            for (int i = 0; i < 2; ++i) {
                m_ratios[i] = double(values[i]) / sum;
                saved << m_ratios[i];
            }
            m_settings->setValue("ui/workbenchPanelRatios", saved);
            m_settings->sync();
        });
        setChildrenCollapsible(false);
        QTimer::singleShot(0, this, [this] { applyRatios(); });
    }

  protected:
    void resizeEvent(QResizeEvent *event) override {
        QSplitter::resizeEvent(event);
        applyRatios();
    }

  private:
    void applyRatios() {
        if (count() != 2)
            return;
        const int available = qMax(1, width() - handleWidth());
        m_applying = true;
        setSizes({qRound(available * m_ratios[0]), qRound(available * m_ratios[1])});
        m_applying = false;
    }
    QSettings *m_settings;
    double m_ratios[2]{.75, .25};
    bool m_applying = false;
};

void lyricSources(QComboBox *combo) {
    combo->addItem("线上匹配", "online");
    combo->addItem("本机识别", "offline");
    combo->addItem("导入 LRC", "local");
}
} // namespace

QWidget *MainWindow::createWorkbenchPage() {
    auto page = new QWidget;
    page->setObjectName("workbenchPage");
    auto layout = new QVBoxLayout(page);
    layout->setContentsMargins(0, 0, 0, 0);
    if (lyricFeaturesEnabled()) {
        auto split = new WorkbenchPanelSplitter(m_settings);
        m_lyricQueue = new LyricQueue(m_library, m_settings);
        auto left = new QWidget;
        auto leftLayout = new QVBoxLayout(left);
        leftLayout->setContentsMargins(0, 0, 0, 0);
        auto scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setMinimumWidth(325);
        auto workbench = createLyricWorkbench();
        auto actions = workbench->findChild<QWidget *>("workbenchActions");
        workbench->layout()->removeWidget(actions);
        scroll->setWidget(workbench);
        leftLayout->addWidget(scroll, 1);
        leftLayout->addWidget(actions);
        split->addWidget(left);
        split->addWidget(m_lyricQueue);
        split->setStretchFactor(0, 3);
        split->setStretchFactor(1, 1);
        layout->addWidget(split);
    }
    return page;
}
void MainWindow::commitLyricTrack(bool skipLyrics) {
    if (!lyricFeaturesEnabled())
        return;
    if (!m_lyricTrack) {
        m_lyricStatus->setText("请选择歌曲源");
        return;
    }
    const auto id = m_lyricTrack;
    m_pendingCommit = 0;
    if (!rememberWorkbenchMetadata())
        return;
    if (skipLyrics || !Lrc::parse(m_library->workbenchTrack(m_lyricTrack).value("lrc").toString()).isEmpty()) {
        m_pendingCommit = 0;
        if (m_library->saveWorkbench(id, !skipLyrics))
            finishWorkbenchImport(id, "已导入乐库");
    } else {
        m_pendingCommit = m_lyricTrack;
        applyLyricSource();
        if (m_lyricSource->currentData().toString() == "offline")
            matchLyrics();
        else
            m_lyrics.fetch(m_lyricTrack, true, m_lyricLookup->text(), true);
    }
}
void MainWindow::loadLibraryTrack(qint64 id) {
    if (!lyricFeaturesEnabled())
        return;
    const auto track = m_library->track(id);
    if (!id || track.isEmpty())
        return;
    m_library->beginWorkbench(id);
    m_workbenchTracks.insert(id);
    refreshLyricLibrary();
    selectLyricTrack(id, false);
    navigate(3);
}
void MainWindow::saveWorkbenchTrack() {
    if (!m_lyricTrack)
        return;
    const auto id = m_lyricTrack;
    if (!rememberWorkbenchMetadata() || !m_library->saveWorkbench(id)) {
        m_lyricStatus->setText("请填写歌曲名称。");
        return;
    }
    finishWorkbenchImport(id, "已保存到乐库");
}
void MainWindow::finishWorkbenchImport(qint64 id, const QString &message) {
    m_workbenchTracks.remove(id);
    m_candidateCache.remove(id);
    m_candidateQueries.remove(id);
    if (m_pendingCommit == id)
        m_pendingCommit = 0;
    if (m_previewTrack == id) {
        m_previewTrack = 0;
        m_lyricFingerprint.clear();
        updateCurrent();
    }
    if (m_lyricTrack != id) {
        refreshLyricLibrary();
        m_importStatus->setText(message);
        return;
    }
    m_lyrics.cancel();
    m_workspaceFingerprint.clear();
    m_lyricTrack = 0;
    refreshLyricLibrary();
    selectLyricTrack(0, false);
    m_importStatus->setText(message);
}
bool MainWindow::rememberWorkbenchMetadata() {
    if (!m_lyricTrack || m_matchTitle->text().trimmed().isEmpty()) {
        m_lyricStatus->setText("请填写歌曲名称。");
        return false;
    }
    return m_library->setWorkbenchMetadata(m_lyricTrack, m_matchTitle->text(), m_matchArtist->text());
}

QWidget *MainWindow::createLyricWorkbench() {
    auto page = new QWidget;
    page->setStyleSheet("QPushButton{padding:6px 8px;} QLineEdit,QComboBox,QSpinBox{padding:5px;}");
    page->setMinimumWidth(310);
    auto layout = new QVBoxLayout(page);
    layout->setContentsMargins(10, 0, 0, 0);
    layout->setSizeConstraint(QLayout::SetDefaultConstraint);
    layout->setSpacing(7);
    auto heading = new QHBoxLayout;
    heading->addWidget(label("歌词工作台", "section"), 1);
    m_commitWithLyrics = action("导入乐库", "commitWithLyrics");
    m_commitWithoutLyrics = action("不配置歌词导入乐库", "commitWithoutLyrics");
    m_saveWorkbench = action("保存", "saveWorkbenchTrack");
    m_saveWorkbench->hide();
    heading->addWidget(m_commitWithLyrics);
    heading->addWidget(m_commitWithoutLyrics);
    heading->addWidget(m_saveWorkbench);
    layout->addLayout(heading);
    connect(m_commitWithLyrics, &QPushButton::clicked, this, [this] { commitLyricTrack(false); });
    connect(m_commitWithoutLyrics, &QPushButton::clicked, this, [this] { commitLyricTrack(true); });
    connect(m_saveWorkbench, &QPushButton::clicked, this, &MainWindow::saveWorkbenchTrack);
    m_lyricTracks = new QComboBox;
    m_lyricTracks->setObjectName("lyricTrackSelector");
    m_lyricTracks->setMinimumContentsLength(15);
    m_lyricTracks->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    auto sourceRow = new QHBoxLayout;
    sourceRow->addWidget(label("歌曲源"));
    sourceRow->addWidget(m_lyricTracks, 1);
    layout->addLayout(sourceRow);
    auto metadataRow = new QHBoxLayout;
    m_matchTitle = new QLineEdit;
    m_matchTitle->setObjectName("lyricSongTitle");
    m_matchArtist = new QLineEdit;
    m_matchArtist->setObjectName("lyricSongArtist");
    m_matchArtist->setPlaceholderText("可填写真实歌手，提高匹配准确度");
    metadataRow->addWidget(label("歌曲名"));
    metadataRow->addWidget(m_matchTitle, 1);
    metadataRow->addWidget(label("歌手"));
    metadataRow->addWidget(m_matchArtist, 1);
    layout->addLayout(metadataRow);
    auto remember = [this] {
        if (m_lyricTrack)
            m_library->setWorkbenchMetadata(m_lyricTrack, m_matchTitle->text(), m_matchArtist->text());
    };
    connect(m_matchTitle, &QLineEdit::textChanged, this, remember);
    connect(m_matchArtist, &QLineEdit::textChanged, this, remember);
    m_lyricLookup = new QLineEdit;
    m_lyricLookup->setObjectName("lyricLookupInput");
    m_lyricLookup->setPlaceholderText("可选：搜索关键词、塞壬官网或 LRCLIB 歌词链接");
    auto meta = new QFormLayout;
    auto lookupRow = new QHBoxLayout;
    lookupRow->addWidget(m_lyricLookup, 1);
    auto sourceOptions = action("歌词源", "onlineSourceOptions");
    sourceOptions->setToolTip("设置优先源和备用源；仅当当前源没有结果时查询下一个。");
    auto sourceMenu = new QMenu(sourceOptions);
    auto priorityMenu = sourceMenu->addMenu("优先查询");
    auto priorityGroup = new QActionGroup(sourceMenu);
    priorityGroup->setExclusive(true);
    for (const auto &entry : LyricsSourcesSearch::sources()) {
        auto preferred = priorityMenu->addAction(entry.second);
        preferred->setCheckable(true);
        preferred->setData(entry.first);
        priorityGroup->addAction(preferred);
        connect(preferred, &QAction::triggered, this, [this, entry] {
            m_settings->setValue("lyrics/primaryOnlineSource", entry.first);
            auto enabled = LyricsSourcesSearch::selectedSources(m_settings);
            if (!enabled.contains(entry.first)) enabled.prepend(entry.first);
            m_settings->setValue("lyrics/onlineSources", enabled);
            m_settings->sync();
        });
        auto enabled = sourceMenu->addAction(entry.second);
        enabled->setCheckable(true);
        enabled->setData(entry.first);
        connect(enabled, &QAction::triggered, this, [this, entry](bool checked) {
            auto enabled = LyricsSourcesSearch::selectedSources(m_settings);
            enabled.removeAll(entry.first);
            if (checked) enabled << entry.first;
            m_settings->setValue("lyrics/onlineSources", enabled);
            m_settings->sync();
        });
    }
    connect(sourceMenu, &QMenu::aboutToShow, this, [this, sourceMenu, priorityGroup] {
        const auto enabled = LyricsSourcesSearch::selectedSources(m_settings);
        for (auto item : sourceMenu->actions())
            if (item->isCheckable()) item->setChecked(enabled.contains(item->data().toString()));
        for (auto item : priorityGroup->actions())
            item->setChecked(item->data().toString() == m_settings->value("lyrics/primaryOnlineSource", "msr"));
    });
    sourceOptions->setMenu(sourceMenu);
    lookupRow->addWidget(sourceOptions);
    meta->addRow("在线搜索", lookupRow);
    layout->addLayout(meta);
    auto divider = new QFrame;
    divider->setObjectName("lyricSectionDivider");
    divider->setFrameShape(QFrame::HLine);
    divider->setProperty("separator", true);
    layout->addWidget(divider);
    layout->addWidget(label("歌词匹配", "section"));
    auto options = new QWidget;
    options->setObjectName("lyricMatchingOptions");
    options->setFixedHeight(recognitionFeaturesEnabled() ? 116 : 76);
    auto optionLayout = new QVBoxLayout(options);
    optionLayout->setContentsMargins(0, 0, 0, 0);
    optionLayout->setSpacing(6);
    layout->addWidget(options);
    m_lyricSource = new QComboBox(page);
    m_lyricSource->setObjectName("workbenchLyricSource");
    if (recognitionFeaturesEnabled())
        lyricSources(m_lyricSource);
    else
        m_lyricSource->addItem("线上匹配", "online");
    m_lyricSource->setCurrentIndex(
        qMax(0, m_lyricSource->findData(m_settings->value("lyrics/source", "auto"))));
    m_lyricSource->hide();
    auto matchingSource = new QHBoxLayout;
    auto categories = new QButtonGroup(page);
    categories->setExclusive(true);
    QList<QPushButton *> categoryButtons;
    for (int i = 0; i < m_lyricSource->count(); ++i) {
        auto button = action(m_lyricSource->itemText(i), QString("lyricCategory%1").arg(i));
        button->setCheckable(true);
        categories->addButton(button, i);
        matchingSource->addWidget(button);
        categoryButtons << button;
    }
    connect(categories, &QButtonGroup::idClicked, m_lyricSource, &QComboBox::setCurrentIndex);
    matchingSource->addStretch();
    optionLayout->addLayout(matchingSource);
    m_localLyricMethod = new QComboBox(page);
    m_localLyricMethod->setObjectName("localLyricMethod");
    m_localLyricMethod->addItem("识别字幕 · 优先使用同源视频", 1);
    if (alignmentFeaturesEnabled())
        m_localLyricMethod->addItem("导入歌词 · 按音频匹配时间", 0);
    m_localLyricMethod->hide();
    auto methodPages = new QStackedWidget;
    methodPages->setObjectName("lyricMethodPages");
    methodPages->setFixedHeight(34);
    methodPages->addWidget(new QWidget);
    auto methods = new QWidget;
    auto methodLayout = new QHBoxLayout(methods);
    methodLayout->setContentsMargins(0, 0, 0, 0);
    auto methodGroup = new QButtonGroup(page);
    methodGroup->setExclusive(true);
    QList<QPushButton *> methodButtons;
    const QStringList methodNames = alignmentFeaturesEnabled() ? QStringList{"识别字幕", "导入歌词"}
                                                              : QStringList{"识别字幕"};
    for (int i = 0; i < methodNames.size(); ++i) {
        auto button = action(methodNames[i], QString("localLyricOption%1").arg(i));
        button->setCheckable(true);
        methodGroup->addButton(button, i);
        methodLayout->addWidget(button);
        methodButtons << button;
    }
    connect(methodGroup, &QButtonGroup::idClicked, m_localLyricMethod, &QComboBox::setCurrentIndex);
    methodLayout->addStretch();
    methodPages->addWidget(methods);
    methodPages->addWidget(new QWidget);
    if (recognitionFeaturesEnabled())
        optionLayout->addWidget(methodPages);
    else {
        methodPages->setParent(page);
        methodPages->hide();
    }
    auto buttons = new QHBoxLayout;
    connect(m_lyricLookup, &QLineEdit::returnPressed, this, [this] {
        m_candidateQueries.remove(m_lyricTrack);
        matchLyrics();
    });
    auto match = action("线上匹配", "primary");
    match->setObjectName("matchLyrics");
    optionLayout->addLayout(buttons);
    auto updateSource = [this, match, methodPages, categoryButtons, methodButtons] {
        const auto source = m_lyricSource->currentData().toString();
        methodPages->setCurrentIndex(m_lyricSource->currentIndex());
        categoryButtons[m_lyricSource->currentIndex()]->setChecked(true);
        if (m_localLyricMethod->currentIndex() >= 0 &&
            m_localLyricMethod->currentIndex() < methodButtons.size())
            methodButtons[m_localLyricMethod->currentIndex()]->setChecked(true);
        match->setText(source == "online"                               ? (m_candidateQueries.contains(m_lyricTrack) ? "继续搜索" : "线上匹配")
                       : source == "local"                              ? "导入 LRC"
                       : m_localLyricMethod->currentData().toInt() == 1 ? "识别字幕"
                                                                        : "导入歌词");
    };
    connect(m_lyricSource, &QComboBox::currentIndexChanged, this, updateSource);
    connect(m_localLyricMethod, &QComboBox::currentIndexChanged, this, updateSource);
    updateSource();
    connect(&m_lyrics, &LyricService::activityChanged, match, [this, match](bool busy) {
        setLyricButtonBusy(match, busy || m_lyricQueue->activeFor(m_lyricTrack));
    });
    connect(m_lyricQueue, &LyricQueue::activity, match, [this, match](qint64, const QString &, bool) {
        const bool busy = m_lyrics.matching() || m_lyricQueue->activeFor(m_lyricTrack);
        setLyricButtonBusy(match, busy);
        m_lyricSpinner->setActive(busy);
    });
    m_lyricStatus = label("", "muted");
    m_lyricStatus->setObjectName("lyricMatchStatus");
    m_lyricStatus->setWordWrap(true);
    auto statusRow = new QHBoxLayout;
    m_lyricSpinner = new BusySpinner;
    m_lyricSpinner->setObjectName("lyricMatchSpinner");
    statusRow->addWidget(m_lyricSpinner);
    statusRow->addWidget(m_lyricStatus, 1);
    buttons->addLayout(statusRow, 1);
    m_candidates = new QTreeWidget;
    m_candidates->setObjectName("lyricCandidates");
    m_candidates->setColumnCount(2);
    m_candidates->setHeaderLabels({"候选版本 / 歌手", "时长差"});
    m_candidates->setRootIsDecorated(false);
    m_candidates->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_candidates->header()->resizeSection(1, 65);
    m_candidates->setMaximumHeight(130);
    m_candidates->setMinimumHeight(90);
    m_candidates->hide();
    layout->addWidget(m_candidates);
    m_candidatePreview = new QPlainTextEdit;
    m_candidatePreview->setReadOnly(true);
    m_candidatePreview->setMaximumHeight(100);
    m_candidatePreview->hide();
    layout->addWidget(m_candidatePreview);
    auto use = action("采用候选并试听");
    use->setObjectName("useLyricCandidate");
    use->hide();
    layout->addWidget(use);
    auto realign = action("使用候选文字，重新匹配时间");
    realign->setObjectName("realignLyricCandidate");
    realign->hide();
    layout->addWidget(realign);
    connect(realign, &QPushButton::clicked, this, [this] {
        auto item = m_candidates->currentItem();
        if (!item || m_candidateTrack != m_lyricTrack)
            return;
        if (m_lyrics.busy()) {
            m_lyricStatus->setText("请先取消正在运行的本机听写。");
            return;
        }
        auto c = item->data(0, Qt::UserRole).toMap();
        auto text = c.value("plainLyrics").toString();
        if (text.isEmpty())
            text = LyricWizard::plainText(c.value("syncedLyrics").toString());
        if (!recognitionFeaturesEnabled())
            return;
        LyricWizard wizard(m_library, m_settings, m_lyricTrack, text, this);
        wizard.setQueue(m_lyricQueue);
        wizard.exec();
    });
    m_lyricPreview = new QListWidget;
    m_lyricPreview->setObjectName("timedLyricPreview");
    m_lyricPreview->setStyleSheet("QListWidget#timedLyricPreview::item{padding:0;}"
                                  "QWidget#lyricLineCell{background:transparent;}");
    m_lyricPreview->setMinimumHeight(220);
    m_lyricPreview->setContextMenuPolicy(recognitionFeaturesEnabled() ? Qt::CustomContextMenu
                                                                      : Qt::NoContextMenu);
    connect(m_lyricPreview, &QListWidget::customContextMenuRequested, this, [this](const QPoint &point) {
        auto item = m_lyricPreview->itemAt(point);
        if (!item || !item->data(Qt::UserRole).isValid())
            return;
        const auto row = m_lyricPreview->row(item);
        QMenu menu(this);
        menu.addAction("在上方添加歌词", this, [this, row] { editLyricLine(row, 3); });
        menu.addAction("在下方添加歌词", this, [this, row] { editLyricLine(row, 2); });
        menu.addSeparator();
        menu.addAction("删除歌词", this, [this, row] { editLyricLine(row, 5); });
        menu.addAction("删除歌词并向上合并时间", this, [this, row] { editLyricLine(row, 6); });
        menu.addAction("删除歌词并向下合并时间", this, [this, row] { editLyricLine(row, 7); });
        menu.addSeparator();
        menu.addAction("进度条转到此", this, [this, row] { editLyricLine(row, 8); });
        menu.exec(m_lyricPreview->viewport()->mapToGlobal(point));
    });
    m_lyricPreview->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    layout->addWidget(m_lyricPreview, 1);
    auto offsetRow = new QHBoxLayout;
    offsetRow->addWidget(label("整体偏移", "muted"));
    auto offsetControls = new QWidget;
    offsetControls->setObjectName("lyricOffsetControls");
    auto offsetButtons = new QHBoxLayout(offsetControls);
    offsetButtons->setContentsMargins(0, 0, 0, 0);
    offsetButtons->setSpacing(6);
    m_matchOffset = new QSpinBox;
    m_matchOffset->setObjectName("lyricTimeOffset");
    m_matchOffset->setRange(-600000, 600000);
    m_matchOffset->setSingleStep(100);
    m_matchOffset->setSuffix(" ms");
    m_matchOffset->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_matchOffset->setFixedWidth(m_matchOffset->fontMetrics().horizontalAdvance("-100000 ms") + 24);
    offsetButtons->addWidget(m_matchOffset);
    auto earlier = action("歌词提前显示", "lyricsShowEarlier");
    auto later = action("歌词延后显示", "lyricsShowLater");
    earlier->setToolTip("整首歌词提前显示 100 ms");
    later->setToolTip("整首歌词延后显示 100 ms");
    offsetButtons->addWidget(earlier);
    offsetButtons->addWidget(later);
    connect(earlier, &QPushButton::clicked, m_matchOffset, &QSpinBox::stepUp);
    connect(later, &QPushButton::clicked, m_matchOffset, &QSpinBox::stepDown);
    offsetRow->addWidget(offsetControls);
    auto timing = label("正值提前显示。试听时可实时调整；双击歌词跳转。", "muted");
    timing->setWordWrap(true);
    offsetRow->addWidget(timing, 1);
    layout->addLayout(offsetRow);
    auto actions = new QWidget;
    actions->setObjectName("workbenchActions");
    auto actionRows = new QVBoxLayout(actions);
    actionRows->setContentsMargins(0, 0, 0, 0);
    actionRows->setSpacing(6);
    auto trimRowWidget = new QWidget;
    trimRowWidget->setObjectName("workbenchTrimRow");
    auto trimRow = new WorkbenchActionsLayout(trimRowWidget);
    auto progressRowWidget = new QWidget;
    progressRowWidget->setObjectName("workbenchProgressRow");
    auto trialRow = new WorkbenchActionsLayout(progressRowWidget);
    actionRows->addWidget(trimRowWidget);
    actionRows->addWidget(progressRowWidget);
    auto seekGroup = new QWidget;
    auto seekRow = new QHBoxLayout(seekGroup);
    seekRow->setContentsMargins(0, 0, 0, 0);
    seekRow->setSpacing(6);
    auto trial = action("▷ 试听当前歌曲", "primary");
    trial->setObjectName("trialLyrics");
    seekRow->addWidget(label("进度", "muted"));
    auto seekStep = new QDoubleSpinBox;
    seekStep->setObjectName("lyricPreviewSeekStep");
    seekStep->setDecimals(1);
    seekStep->setRange(.1, 100);
    seekStep->setSingleStep(.1);
    seekStep->setSuffix(" s");
    seekStep->setValue(m_settings->value("lyrics/previewSeekSeconds", 1.).toDouble());
    seekStep->setFixedWidth(seekStep->fontMetrics().horizontalAdvance("100.0 s") + 42);
    seekStep->setToolTip("前进、后退的进度调整量，默认 1 秒；可调整到 0.1～100 秒。");
    seekRow->addWidget(seekStep);
    auto seekButtons = new QWidget;
    seekButtons->setObjectName("lyricPreviewSeekButtons");
    auto seekLayout = new QHBoxLayout(seekButtons);
    seekLayout->setContentsMargins(0, 0, 0, 0);
    seekLayout->setSpacing(6);
    auto backward = action("←", "lyricPreviewBackward");
    auto forward = action("→", "lyricPreviewForward");
    backward->setAccessibleName("后退");
    forward->setAccessibleName("前进");
    backward->setFixedWidth(40);
    forward->setFixedWidth(40);
    backward->setToolTip("按设定秒数后退当前歌曲的播放进度，保留播放或暂停状态。");
    forward->setToolTip("按设定秒数前进当前歌曲的播放进度，保留播放或暂停状态。");
    seekLayout->addWidget(backward);
    seekLayout->addWidget(forward);
    seekRow->addWidget(seekButtons);
    trialRow->addWidget(seekGroup);
    const auto updateSeek = [this] { updateLyricPreviewControls(); };
    connect(&m_player, &Player::currentChanged, actions, updateSeek);
    connect(m_player.media(), &QMediaPlayer::seekableChanged, actions, updateSeek);
    connect(seekStep, &QDoubleSpinBox::valueChanged, this, [this](double seconds) {
        m_settings->setValue("lyrics/previewSeekSeconds", seconds);
    });
    const auto seek = [this, seekStep](int direction) {
        auto media = m_player.media();
        if (!m_lyricTrack || m_player.currentId() != m_lyricTrack || !media->isSeekable())
            return;
        const auto position = qMax(qint64(0), media->position() + direction * qRound64(seekStep->value() * 1000));
        media->setPosition(media->duration() > 0 ? qMin(position, media->duration()) : position);
    };
    connect(backward, &QPushButton::clicked, this, [seek] { seek(-1); });
    connect(forward, &QPushButton::clicked, this, [seek] { seek(1); });
    auto trimInput = new QWidget;
    auto trimInputRow = new QHBoxLayout(trimInput);
    trimInputRow->setContentsMargins(0, 0, 0, 0);
    trimInputRow->setSpacing(6);
    trimInputRow->addWidget(label("裁剪", "muted"));
    auto trimSeconds = new QDoubleSpinBox;
    trimSeconds->setObjectName("lyricTrimSeconds");
    trimSeconds->setDecimals(1);
    trimSeconds->setRange(.1, 3600);
    trimSeconds->setSingleStep(.1);
    trimSeconds->setSuffix(" s");
    trimSeconds->setValue(m_settings->value("lyrics/trimSeconds", 1.).toDouble());
    trimSeconds->setFixedWidth(trimSeconds->fontMetrics().horizontalAdvance("3600.0 s") + 42);
    trimSeconds->setToolTip("删除当前试听位置前后多少秒内的时间轴歌词；不改变其余歌词时间和音频。");
    trimInputRow->addWidget(trimSeconds);
    trimRow->addWidget(trimInput);
    connect(trimSeconds, &QDoubleSpinBox::valueChanged, this, [this](double seconds) {
        m_settings->setValue("lyrics/trimSeconds", seconds);
    });
    const QStringList trimNames{"lyricTrimAfter", "lyricTrimBefore", "lyricTrimToStart", "lyricTrimToEnd"};
    const QStringList trimTexts{"裁剪后方", "裁剪前方", "向前裁剪至开头", "向后裁剪至结束"};
    const QStringList trimTips{
        "删除从当前试听位置起、指定秒数内的歌词（含当前位置，不含区间终点）。",
        "删除当前试听位置前指定秒数内的歌词（不含当前位置）。",
        "删除当前试听位置之前的全部歌词（不含当前位置）。",
        "删除从当前试听位置到结尾的全部歌词（含当前位置）。"};
    for (int i = 0; i < trimNames.size(); ++i) {
        auto button = action(trimTexts[i], trimNames[i]);
        button->setProperty("lyricTrimAction", true);
        button->setToolTip(trimTips[i] + "只修改工作台草稿，整体偏移已计入。");
        button->setStyleSheet("padding:8px 7px;");
        trimRow->addWidget(button);
        connect(button, &QPushButton::clicked, this, [this, i] { trimLyricTimeline(i == 0 || i == 3 ? 1 : -1, i >= 2); });
    }
    auto transport = new QWidget;
    transport->setProperty("alignWorkbenchRight", true);
    auto transportRow = new QHBoxLayout(transport);
    transportRow->setContentsMargins(0, 0, 0, 0);
    transportRow->setSpacing(6);
    transportRow->addWidget(trial);
    transportRow->addWidget(match);
    trialRow->addWidget(transport);
    updateSeek();
    layout->addWidget(actions);
    connect(m_lyricTracks, &QComboBox::currentIndexChanged, this, [this](int index) {
        auto id = m_lyricTracks->itemData(index).toLongLong();
        selectLyricTrack(id, true);
    });
    connect(m_lyricSource, &QComboBox::currentIndexChanged, this, [this] { applyLyricSource(); });
    connect(match, &QPushButton::clicked, this, [this] { matchLyrics(); });
    connect(trial, &QPushButton::clicked, this, &MainWindow::playLyricPreview);
    connect(m_matchOffset, &QSpinBox::valueChanged, this, [this](int value) {
        if (m_lyricTrack)
            m_library->setWorkbenchOffset(m_lyricTrack, value);
    });
    connect(m_candidates, &QTreeWidget::currentItemChanged, this, [this](auto item, auto) {
        if (item) {
            auto c = item->data(0, Qt::UserRole).toMap();
            auto text = c.value("syncedLyrics").toString();
            m_candidatePreview->setPlainText(
                Lrc::simplifiedChinese(text.isEmpty() ? c.value("plainLyrics").toString() : text,
                                       m_settings->value("lyrics/autoSimplifyChinese", true).toBool()));
        }
    });
    connect(m_candidates, &QTreeWidget::itemSelectionChanged, use, [this, use] {
        const auto item = m_candidates->currentItem();
        const bool timed =
            item &&
            !Lrc::parse(item->data(0, Qt::UserRole).toMap().value("syncedLyrics").toString()).isEmpty();
        use->setEnabled(recognitionFeaturesEnabled() || timed);
        use->setToolTip(timed ? "采用已有时间轴" : "仅有文本；在线版需选择已有时间轴的候选");
    });
    connect(use, &QPushButton::clicked, this, [this] {
        auto item = m_candidates->currentItem();
        if (!item || m_candidateTrack != m_lyricTrack)
            return;
        auto c = item->data(0, Qt::UserRole).toMap();
        if (Lrc::parse(c.value("syncedLyrics").toString()).isEmpty()) {
            if (m_lyrics.busy()) {
                m_lyricStatus->setText("请先取消正在运行的本机听写。");
                return;
            }
            if (!recognitionFeaturesEnabled()) {
                m_lyricStatus->setText("该候选只有文字，请选择已有时间轴的版本。");
                return;
            }
            LyricWizard wizard(m_library, m_settings, m_lyricTrack, c.value("plainLyrics").toString(), this);
            wizard.setQueue(m_lyricQueue);
            wizard.exec();
        } else {
            m_lyrics.useCandidate(m_lyricTrack, c, true);
            playLyricPreview();
        }
    });
    connect(m_lyricPreview, &QListWidget::itemDoubleClicked, this, [this](auto item) {
        if (!m_lyricTrack)
            return;
        editLyricLine(m_lyricPreview->row(item), 4);
    });
    return page;
}
void MainWindow::refreshLyricLibrary() {
    if (!lyricFeaturesEnabled())
        return;
    QSignalBlocker guard(m_lyricTracks);
    m_lyricTracks->clear();
    m_lyricTracks->addItem("", 0);
    if (!recognitionFeaturesEnabled())
        for (const auto &track : m_library->tracks())
            m_workbenchTracks.insert(track.value("id").toLongLong());
    for (const auto id : m_workbenchTracks) {
        const auto t = m_library->workbenchTrack(id);
        if (!t.isEmpty())
            m_lyricTracks->addItem(
                (t.value("staged").toBool() ? "[待入库] " : "") + t.value("title").toString() +
                    (t.value("storage").toString() == "url" ? " [URL]" : ""), id);
    }
    const int index = m_lyricTracks->findData(m_lyricTrack);
    if (index < 0)
        m_lyricTrack = 0;
    m_lyricTracks->setCurrentIndex(qMax(0, index));
}
void MainWindow::selectLyricTrack(qint64 id, bool match) {
    if (!lyricFeaturesEnabled())
        return;
    if (id != m_lyricTrack)
        m_pendingCommit = 0;
    if (m_lyricTrack && id != m_lyricTrack)
        m_library->setWorkbenchMetadata(m_lyricTrack, m_matchTitle->text(), m_matchArtist->text());
    if (!recognitionFeaturesEnabled())
        m_workbenchTracks.insert(id);
    m_lyricTrack = m_workbenchTracks.contains(id) && !m_library->track(id).isEmpty() ? id : 0;
    if (m_lyricSource->currentData().toString() == "online")
        findChild<QPushButton *>("matchLyrics")->setText(m_candidateQueries.contains(m_lyricTrack) ? "继续搜索" : "线上匹配");
    if (m_lyricTrack)
        m_library->beginWorkbench(m_lyricTrack);
    {
        QSignalBlocker guard(m_lyricTracks);
        m_lyricTracks->setCurrentIndex(qMax(0, m_lyricTracks->findData(m_lyricTrack)));
    }
    m_candidates->clear();
    m_candidates->hide();
    m_candidatePreview->hide();
    m_candidateTrack = 0;
    refreshLyricWorkbench(true);
    const bool busy = m_lyrics.matching() || m_lyricQueue->activeFor(m_lyricTrack);
    setLyricButtonBusy(findChild<QPushButton *>("matchLyrics"), busy);
    m_lyricSpinner->setActive(busy);
    if (m_candidateCache.contains(id))
        chooseLyrics(id, m_candidateCache.value(id));
    if (match && m_lyricTrack) {
        applyLyricSource();
        m_lyrics.fetch(m_lyricTrack, false, {}, true);
    }
}
void MainWindow::refreshLyricWorkbench(bool force) {
    if (!lyricFeaturesEnabled())
        return;
    const auto t = m_library->workbenchTrack(m_lyricTrack);
    if (t.isEmpty()) {
        m_candidates->clear();
        m_candidates->hide();
        m_candidatePreview->hide();
        m_candidateTrack = 0;
    }
    const auto fingerprint = QString::number(m_lyricTrack) + "|" + t.value("title").toString() + "|" +
                             t.value("artist").toString() + "|" + t.value("lrc").toString() +
                             QString::number(t.value("lyric_offset").toInt());
    if (!force && fingerprint == m_workspaceFingerprint)
        return;
    m_workspaceFingerprint = fingerprint;
    if (m_lyricLookup->property("track").toLongLong() != m_lyricTrack) {
        m_lyricLookup->clear();
        m_lyricLookup->setProperty("track", m_lyricTrack);
    }
    {
        QSignalBlocker titleGuard(m_matchTitle), artistGuard(m_matchArtist);
        if (m_matchTitle->text() != t.value("title").toString())
            m_matchTitle->setText(t.value("title").toString());
        if (m_matchArtist->text() != t.value("artist").toString())
            m_matchArtist->setText(t.value("artist").toString());
    }
    {
        QSignalBlocker guard(m_matchOffset);
        m_matchOffset->setValue(t.value("lyric_offset").toInt());
    }
    m_matchOffset->setEnabled(m_lyricTrack != 0);
    findChild<QWidget *>("lyricOffsetControls")->setEnabled(m_lyricTrack != 0);
    updateLyricPreviewControls();
    const bool selected = m_lyricTrack != 0;
    const bool staged = selected && t.value("staged").toBool();
    m_commitWithLyrics->setEnabled(staged);
    m_commitWithoutLyrics->setEnabled(staged);
    m_saveWorkbench->setVisible(selected && !staged);
    m_saveWorkbench->setEnabled(selected && !staged);
    m_workspaceLines = Lrc::parse(t.value("lrc").toString());
    m_lyricPreview->clear();
    for (const auto &line : m_workspaceLines) {
        const auto stamp = displayTime(line.ms) +
                           QString(".%1").arg(qMax(qint64(0), line.ms) % 1000, 3, 10, QLatin1Char('0'));
        auto item = new QListWidgetItem(stamp + "  " + line.text, m_lyricPreview);
        item->setData(Qt::UserRole, line.ms);
        const int row = m_lyricPreview->row(item);
        auto cell = new QWidget;
        cell->setObjectName("lyricLineCell");
        auto cellLayout = new QHBoxLayout(cell);
        cellLayout->setContentsMargins(6, 2, 4, 2);
        auto text = new QLabel(item->text());
        text->setTextFormat(Qt::PlainText);
        text->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        text->setMinimumWidth(0);
        text->setAttribute(Qt::WA_TransparentForMouseEvents);
        cellLayout->addWidget(text, 1);
        item->setText({});
        item->setToolTip(stamp + "  " + line.text);
        const QStringList captions{"▴", "▾", "×"};
        const int operations[]{-1, 1, 0};
        for (int i = 0; recognitionFeaturesEnabled() && i < 3; ++i) {
            auto button = new QPushButton(captions[i]);
            button->setObjectName(QString("lyricLine%1_%2").arg(row).arg(i));
            button->setFixedSize(26, 26);
            button->setStyleSheet("padding:0;");
            button->setToolTip(i == 0 ? "提前 100 ms" : i == 1 ? "延后 100 ms" : "删除歌词并向上合并时间");
            cellLayout->addWidget(button);
            connect(button, &QPushButton::clicked, this,
                    [this, row, operation = operations[i]] { editLyricLine(row, operation); });
        }
        item->setSizeHint(QSize(0, line.text.contains('\n') ? 58 : 36));
        m_lyricPreview->setItemWidget(item, cell);
    }
    if (m_workspaceLines.isEmpty() && m_lyricTrack)
        m_lyricPreview->addItem("暂无时间轴歌词，可匹配或导入。");
    m_lyricStatus->setText(t.value("lyric_source").toString().isEmpty()
                               ? (m_lyricTrack ? "等待匹配时间轴歌词" : "")
                               : "当前歌词来源：" + t.value("lyric_source").toString());
    syncLyricPreview();
}
void MainWindow::saveLyricLines(const QList<LyricLine> &lines) {
    m_library->setWorkbenchLyrics(m_lyricTrack, Lrc::serialize(lines), "手动调整");
}
void MainWindow::editLyricLine(int row, int operation) {
    auto lines = m_workspaceLines;
    if (row < 0 || row >= lines.size())
        return;
    if (operation == 8) {
        playLyricPreview();
        m_player.media()->setPosition(qMax(qint64(0), lines[row].ms - m_matchOffset->value()));
        return;
    }
    if (!recognitionFeaturesEnabled()) {
        playLyricPreview();
        m_player.media()->setPosition(qMax(qint64(0), lines[row].ms - m_matchOffset->value()));
        return;
    }
    if (operation == 4) {
        bool ok = false;
        const auto text = QInputDialog::getMultiLineText(this, "修改歌词", "歌词", lines[row].text, &ok);
        if (!ok)
            return;
        lines[row].text = text.trimmed();
    } else if (operation == 2 || operation == 3) {
        bool ok = false;
        const auto text = QInputDialog::getMultiLineText(this, "添加歌词", "歌词", {}, &ok);
        if (!ok || text.trimmed().isEmpty())
            return;
        if (operation == 2) {
            const auto next = row + 1 < lines.size() ? lines[row + 1].ms : lines[row].ms + 2000;
            lines.insert(row + 1, {lines[row].ms + (next - lines[row].ms) / 2, text.trimmed()});
        } else {
            const auto previous = row > 0 ? lines[row - 1].ms : qMax(qint64(0), lines[row].ms - 2000);
            lines.insert(row, {previous + (lines[row].ms - previous) / 2, text.trimmed()});
        }
    } else if (operation == 0 || operation == 6) {
        lines.removeAt(row);
    } else if (operation == 5) {
        lines[row].text.clear();
    } else if (operation == 7) {
        const auto removedStart = lines[row].ms;
        lines.removeAt(row);
        if (row < lines.size())
            lines[row].ms = removedStart;
    } else {
        const auto lower = row > 0 ? lines[row - 1].ms + 1 : 0;
        const auto upper = row + 1 < lines.size() ? lines[row + 1].ms - 1 : lines[row].ms + 100;
        lines[row].ms = qMax(lower, qMin(upper, lines[row].ms + operation * 100));
    }
    saveLyricLines(lines);
}
void MainWindow::applyLyricSource() {
    if (!lyricFeaturesEnabled())
        return;
    m_settings->setValue("lyrics/source", m_lyricSource->currentData());
}
void MainWindow::matchLyrics(bool offline) {
    if (!recognitionFeaturesEnabled())
        offline = false;
    if (!lyricFeaturesEnabled())
        return;
    if (!m_lyricTrack) {
        m_lyricStatus->setText("请先选择歌曲，或下载搜索结果。");
        return;
    }
    if (!rememberWorkbenchMetadata()) {
        m_lyricStatus->setText("请填写歌曲名称。");
        return;
    }
    applyLyricSource();
    m_candidateCache.remove(m_lyricTrack);
    m_candidates->clear();
    m_candidates->hide();
    m_candidatePreview->hide();
    if (recognitionFeaturesEnabled() && (offline || m_lyricSource->currentData().toString() == "offline")) {
        openLyricWizard();
    } else if (recognitionFeaturesEnabled() && m_lyricSource->currentData().toString() == "local") {
        const auto path = QFileDialog::getOpenFileName(this, "导入时间轴歌词", {}, "LRC (*.lrc)");
        if (!path.isEmpty())
            m_lyrics.importFile(m_lyricTrack, path, true);
    } else
        m_lyrics.fetch(m_lyricTrack, true, m_lyricLookup->text(), true);
}
void MainWindow::openLyricWizard(const QString &text, int method) {
    if (!recognitionFeaturesEnabled())
        return;
    if (!m_lyricTrack) {
        m_lyricStatus->setText("请先选择歌曲源。");
        return;
    }
    if (!rememberWorkbenchMetadata())
        return;
    if (method < 0)
        method = m_lyricSource->currentData().toString() == "online" ? 2
                 : m_lyricSource->currentData().toString() == "local"
                     ? 0
                     : m_localLyricMethod->currentData().toInt();
    LyricWizard wizard(m_library, m_settings, m_lyricTrack, text, this);
    wizard.setQueue(m_lyricQueue);
    wizard.findChild<QComboBox *>("lyricMethod")->setCurrentIndex(method);
    wizard.exec();
}
void MainWindow::updateLyricPreviewControls() {
    const bool loaded = m_lyricTrack && m_player.currentId() == m_lyricTrack &&
                        m_player.media()->isSeekable();
    if (auto seek = findChild<QWidget *>("lyricPreviewSeekButtons"))
        seek->setEnabled(loaded);
    const auto track = m_library->workbenchTrack(m_lyricTrack);
    const bool canTrim = loaded && !Lrc::parse(track.value("raw_lrc").toString()).isEmpty();
    for (auto button : findChildren<QPushButton *>())
        if (button->property("lyricTrimAction").toBool()) button->setEnabled(canTrim);
}
void MainWindow::trimLyricTimeline(int direction, bool toEdge) {
    auto media = m_player.media();
    if (!m_lyricTrack || m_player.currentId() != m_lyricTrack || !media->isSeekable())
        return;
    const auto track = m_library->workbenchTrack(m_lyricTrack);
    const auto lines = Lrc::parse(track.value("raw_lrc").toString());
    const auto cursor = media->position() + track.value("lyric_offset").toInt();
    const auto span = qRound64(findChild<QDoubleSpinBox *>("lyricTrimSeconds")->value() * 1000);
    QList<LyricLine> kept;
    int removed = 0;
    for (const auto &line : lines) {
        const bool cut = direction > 0 ? line.ms >= cursor && (toEdge || line.ms < cursor + span)
                                       : line.ms < cursor && (toEdge || line.ms >= cursor - span);
        if (cut) ++removed;
        else kept << line;
    }
    if (!removed) {
        notify("该范围内没有时间轴歌词");
        return;
    }
    if (!m_library->setWorkbenchLyrics(m_lyricTrack, Lrc::serialize(kept),
                                      track.value("lyric_source").toString(),
                                      track.value("lyric_offset").toInt())) {
        notify("裁剪保存失败，请重试");
        return;
    }
    updateLyricPreviewControls();
    syncLyricPreview();
    notify(QString("已裁剪 %1 条时间轴歌词，尚未保存到乐库").arg(removed));
}
void MainWindow::playLyricPreview() {
    if (!lyricFeaturesEnabled())
        return;
    if (!m_lyricTrack)
        return;
    m_previewTrack = m_lyricTrack;
    m_lyricFingerprint.clear();
    if (m_player.currentId() != m_lyricTrack)
        m_player.setQueue({m_lyricTrack});
    else
        m_player.media()->play();
    updateCurrent();
}
void MainWindow::syncLyricPreview() {
    if (!lyricFeaturesEnabled())
        return;
    if (m_player.currentId() != m_lyricTrack || m_workspaceLines.isEmpty())
        return;
    const int index = Lrc::indexAt(m_workspaceLines, m_player.media()->position() + m_matchOffset->value());
    if (index >= 0 && index != m_lyricPreview->currentRow()) {
        m_lyricPreview->setCurrentRow(index);
        m_lyricPreview->scrollToItem(m_lyricPreview->item(index), QAbstractItemView::PositionAtCenter);
    }
}
void MainWindow::chooseLyrics(qint64 id, const QList<QVariantMap> &items) {
    if (!lyricFeaturesEnabled())
        return;
    m_candidateCache[id] = items;
    const auto track = m_library->workbenchTrack(id);
    m_candidateQueries[id] = track.value("title").toString().trimmed() + "\n" +
                             track.value("artist").toString().trimmed() + "\n" +
                             (id == m_lyricTrack ? m_lyricLookup->text().trimmed() : QString());
    if (id != m_lyricTrack)
        return;
    if (m_lyricSource->currentData().toString() == "online")
        findChild<QPushButton *>("matchLyrics")->setText("继续搜索");
    m_candidateTrack = id;
    m_candidates->clear();
    for (const auto &candidate : items) {
        auto item = new QTreeWidgetItem(
            m_candidates,
            {candidate.value("trackName").toString() + " / " + candidate.value("artistName").toString() +
                 (candidate.contains("score") ? QString(" · %1%").arg(candidate.value("score").toInt())
                                              : QString()),
             candidate.value("syncedLyrics").toString().isEmpty()
                 ? "纯文本"
                 : QString::number(candidate.value("difference").toDouble(), 'f', 1) + "s"});
        item->setData(0, Qt::UserRole, candidate);
        item->setToolTip(0, candidate.value("sourceUrl").toString());
    }
    // Each click appends only this batch to the result queue.
    // All versions are viewed and applied from the recognition queue.
    m_candidates->hide();
    m_candidatePreview->hide();
    if (!items.isEmpty())
        m_candidates->setCurrentItem(m_candidates->topLevelItem(0));
    m_lyricStatus->setText(items.isEmpty() ? QString() : "在线结果已加入识别队列，双击查看并应用。");
}
