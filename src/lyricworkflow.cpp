#include "lyricworkflow.h"
#include "features.h"
#include "lrc.h"
#include "onlinelyrics.h"
#include "services.h"
#include "theme.h"
#include <QApplication>
#include <QBoxLayout>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QNetworkReply>
#include <QPainter>
#include <QPlainTextEdit>
#include <QProcessEnvironment>
#include <QProgressBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QShortcut>
#include <QShowEvent>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QTabWidget>
#include <QTreeWidget>
#include <QUrl>
#include <QUrlQuery>
#include <QUuid>
#include <cmath>
#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

LyricCropPreview::LyricCropPreview(QWidget *parent) : QWidget(parent) {
    setObjectName("lyricCropPreview");
    setMinimumSize(300, 220);
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}
QRectF LyricCropPreview::imageRect() const {
    if (m_image.isNull())
        return {};
    const auto area = QRectF(rect()).adjusted(8, 8, -8, -8);
    const QSizeF size = QSizeF(m_image.size()).scaled(area.size(), Qt::KeepAspectRatio);
    return {area.center() - QPointF(size.width() / 2, size.height() / 2), size};
}
QSizeF LyricCropPreview::minimumSelection() const {
    return {m_image.isNull() ? .01 : qMin(1., 8. / m_image.width()),
            m_image.isNull() ? .01 : qMin(1., 8. / m_image.height())};
}
void LyricCropPreview::setImage(const QImage &image) {
    m_image = image;
    setSelection(m_selection);
    update();
    emit imageChanged();
}
void LyricCropPreview::clear() {
    m_image = {};
    m_drag = 0;
    unsetCursor();
    update();
    emit imageChanged();
}
void LyricCropPreview::setSelection(const QRectF &selection) {
    const auto minimum = minimumSelection();
    const double left = qBound(0., selection.left(), 1. - minimum.width());
    const double top = qBound(0., selection.top(), 1. - minimum.height());
    const double right = qBound(left + minimum.width(), selection.right(), 1.);
    const double bottom = qBound(top + minimum.height(), selection.bottom(), 1.);
    const QRectF next(QPointF(left, top), QPointF(right, bottom));
    if (next == m_selection)
        return;
    m_selection = next;
    update();
    emit selectionChanged(next);
}
QRectF LyricCropPreview::screenSelection() const {
    const auto area = imageRect();
    return {area.left() + area.width() * m_selection.left(), area.top() + area.height() * m_selection.top(),
            area.width() * m_selection.width(), area.height() * m_selection.height()};
}
QPointF LyricCropPreview::normalizedPoint(const QPointF &point) const {
    const auto area = imageRect();
    return {qBound(0., (point.x() - area.left()) / qMax(1., area.width()), 1.),
            qBound(0., (point.y() - area.top()) / qMax(1., area.height()), 1.)};
}
int LyricCropPreview::hitTest(const QPointF &point) const {
    if (!hasImage() || !imageRect().adjusted(-8, -8, 8, 8).contains(point))
        return 0;
    const auto box = screenSelection();
    if (!box.adjusted(-8, -8, 8, 8).contains(point))
        return 32; // Draw a new selection outside the current box.
    int edges = 0;
    if (qAbs(point.x() - box.left()) <= 7)
        edges |= 1;
    else if (qAbs(point.x() - box.right()) <= 7)
        edges |= 2;
    if (qAbs(point.y() - box.top()) <= 7)
        edges |= 4;
    else if (qAbs(point.y() - box.bottom()) <= 7)
        edges |= 8;
    return edges ? edges : box.contains(point) ? 16 : 32;
}
void LyricCropPreview::updateCursor(const QPointF &point) {
    const int mode = hitTest(point);
    const bool horizontal = mode & 3, vertical = mode & 12;
    if (horizontal && vertical)
        setCursor(mode == 5 || mode == 10 ? Qt::SizeFDiagCursor : Qt::SizeBDiagCursor);
    else if (horizontal)
        setCursor(Qt::SizeHorCursor);
    else if (vertical)
        setCursor(Qt::SizeVerCursor);
    else if (mode == 16)
        setCursor(Qt::SizeAllCursor);
    else if (mode == 32)
        setCursor(Qt::CrossCursor);
    else
        unsetCursor();
}
void LyricCropPreview::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.fillRect(rect(), QColor("#080808"));
    if (!hasImage())
        return;
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    const auto area = imageRect(), box = screenSelection();
    painter.drawImage(area, m_image);
    const QColor shade(0, 0, 0, 115);
    painter.fillRect(QRectF(area.left(), area.top(), area.width(), box.top() - area.top()), shade);
    painter.fillRect(QRectF(area.left(), box.bottom(), area.width(), area.bottom() - box.bottom()), shade);
    painter.fillRect(QRectF(area.left(), box.top(), box.left() - area.left(), box.height()), shade);
    painter.fillRect(QRectF(box.right(), box.top(), area.right() - box.right(), box.height()), shade);
    painter.setPen(QPen(QColor("#00d28c"), 2));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(box);
    painter.setBrush(QColor("#00d28c"));
    for (const auto &point : {box.topLeft(), box.topRight(), box.bottomLeft(), box.bottomRight(),
                              QPointF(box.center().x(), box.top()), QPointF(box.center().x(), box.bottom()),
                              QPointF(box.left(), box.center().y()), QPointF(box.right(), box.center().y())})
        painter.drawRect(QRectF(point.x() - 3, point.y() - 3, 6, 6));
}
void LyricCropPreview::mousePressEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || !hasImage() ||
        !imageRect().adjusted(-7, -7, 7, 7).contains(event->position()))
        return;
    m_drag = hitTest(event->position());
    m_start = normalizedPoint(event->position());
    m_startSelection = m_selection;
    event->accept();
}
void LyricCropPreview::mouseMoveEvent(QMouseEvent *event) {
    if (!m_drag) {
        updateCursor(event->position());
        return;
    }
    const auto point = normalizedPoint(event->position());
    const auto delta = point - m_start;
    auto box = m_startSelection;
    const auto minimum = minimumSelection();
    if (m_drag == 16) {
        box.moveTopLeft({qBound(0., box.left() + delta.x(), 1. - box.width()),
                         qBound(0., box.top() + delta.y(), 1. - box.height())});
    } else if (m_drag == 32) {
        box = QRectF(m_start, point).normalized();
    } else {
        if (m_drag & 1)
            box.setLeft(qBound(0., point.x(), box.right() - minimum.width()));
        if (m_drag & 2)
            box.setRight(qBound(box.left() + minimum.width(), point.x(), 1.));
        if (m_drag & 4)
            box.setTop(qBound(0., point.y(), box.bottom() - minimum.height()));
        if (m_drag & 8)
            box.setBottom(qBound(box.top() + minimum.height(), point.y(), 1.));
    }
    setSelection(box);
    event->accept();
}
void LyricCropPreview::mouseReleaseEvent(QMouseEvent *event) {
    if (event->button() != Qt::LeftButton || !m_drag)
        return;
    // Apply the release point as well, including when the pointer leaves the image.
    mouseMoveEvent(event);
    m_drag = 0;
    updateCursor(event->position());
    event->accept();
}
void LyricCropPreview::leaveEvent(QEvent *) {
    if (!m_drag)
        unsetCursor();
}

static QString lyricJobTemplate(QSettings *settings) {
    const auto folder = QDir(QFileInfo(settings->fileName()).absolutePath()).filePath("lyric-jobs");
    QDir().mkpath(folder);
    return QDir(folder).filePath("job-XXXXXX");
}
LyricJob::LyricJob(QSettings *settings, QObject *parent)
    : QObject(parent), m_settings(settings), m_work(lyricJobTemplate(settings)) {
    m_timeout.setSingleShot(true);
#ifdef Q_OS_WIN
    auto handle = CreateJobObjectW(nullptr, nullptr);
    if (handle) {
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        if (SetInformationJobObject(handle, JobObjectExtendedLimitInformation, &limits, sizeof(limits)))
            m_jobHandle = handle;
        else
            CloseHandle(handle);
    }
    connect(&m_process, &QProcess::started, this, [this] {
        auto child = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(m_process.processId()));
        if (child) {
            if (m_jobHandle)
                AssignProcessToJobObject(HANDLE(m_jobHandle), child);
            CloseHandle(child);
        }
    });
#endif
    connect(&m_process, &QProcess::started, this, [this] {
        if (m_cancelled)
            cancel();
    });
    connect(&m_timeout, &QTimer::timeout, this, [this] { cancel(); });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, [this] {
        m_output += m_process.readAllStandardOutput();
        int newline;
        while ((newline = m_output.indexOf('\n')) >= 0) {
            auto value = QJsonDocument::fromJson(m_output.left(newline)).object();
            m_output.remove(0, newline + 1);
            if (value.contains("message"))
                emit progress(value["message"].toString(), value["percent"].toInt(-1));
        }
        if (m_output.size() > 8192)
            m_output = m_output.right(4096);
    });
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
        m_errors += m_process.readAllStandardError();
        m_errors = m_errors.right(4000);
    });
    connect(&m_process, &QProcess::finished, this, [this] { complete(); });
    connect(&m_process, &QProcess::errorOccurred, this, [this](auto e) {
        if (e == QProcess::FailedToStart)
            complete();
    });
}
LyricJob::~LyricJob() {
    m_process.disconnect(this);
    cancel();
    m_process.waitForFinished(3000);
#ifdef Q_OS_WIN
    if (m_jobHandle)
        CloseHandle(HANDLE(m_jobHandle));
#endif
}
bool LyricJob::start(QJsonObject config) {
    if (!recognitionFeaturesEnabled()) {
        emit finished({{"ok", false}, {"error", "在线版不提供本机识别或导入"}});
        return false;
    }
    if (busy())
        return false;
    const auto root = resourceRoot();
    config = prepareConfig(m_settings, config);
    const auto python = config["python"].toString();
    if (!m_work.isValid() || !QFileInfo::exists(python) ||
        !QFileInfo::exists(root + "/workers/lyric_worker.py")) {
        emit finished({{"ok", false}, {"error", "缺少歌词工具，请运行 scripts/setup-lyric-tools.ps1"}});
        return false;
    }
    m_cancelled = false;
    m_delivered = false;
    m_output.clear();
    m_errors.clear();
    const auto key = QUuid::createUuid().toString(QUuid::WithoutBraces);
    m_result = m_work.filePath(key + ".result.json");
    m_cancel = m_work.filePath(key + ".cancel");
    config["result"] = m_result;
    config["cancel"] = m_cancel;
    config["work"] = m_work.path();
    QFile file(m_work.filePath(key + ".json"));
    if (!file.open(QIODevice::WriteOnly)) {
        emit finished({{"ok", false}, {"error", "无法创建临时任务文件"}});
        return false;
    }
    file.write(QJsonDocument(config).toJson());
    file.close();
    auto env = QProcessEnvironment::systemEnvironment();
    env.insert("PYTHONUTF8", "1");
    env.insert("NUMBA_CACHE_DIR", m_work.filePath("numba"));
    m_process.setProcessEnvironment(env);
    m_process.start(python, {"-u", root + "/workers/lyric_worker.py", file.fileName()});
    m_timeout.start(7200000);
    return true;
}
static double ocrSpaceProbability(QSettings *settings) {
    if (!settings->contains("lyrics/ocrSpaceProbability")) {
        auto weight = settings->value("lyrics/ocrSpaceWeight", 1.).toDouble();
        if (!std::isfinite(weight) || weight <= 0)
            weight = 1.;
        const auto probability =
            qRound(qBound(0., .5 + .5 * std::log(weight) / std::log(20.), 1.) * 1000.) / 1000.;
        settings->setValue("lyrics/ocrSpaceProbability", probability);
    }
    const auto value = settings->value("lyrics/ocrSpaceProbability", .5).toDouble();
    return std::isfinite(value) ? qBound(0., value, 1.) : .5;
}
QJsonObject LyricJob::prepareConfig(QSettings *settings, QJsonObject config) {
    const auto root = resourceRoot();
    const QJsonObject defaults{
        {"python",
         settings->value("tools/lyric-python", root + "/tools/lyrics-runtime/python.exe").toString()},
        {"ffmpeg", toolPath(settings, "ffmpeg")},
        {"ytdlp", toolPath(settings, "yt-dlp")},
        {"cookies", settings->value("download/cookies").toString()},
        {"ocr_device", settings->value("lyrics/ocrDevice", "auto").toString()},
        {"ocr_language", settings->value("lyrics/ocrLanguage", "zh").toString()},
        {"model", settings->value("lyrics/alignmentModel", root + "/models/small.pt").toString()}};
    for (auto it = defaults.begin(); it != defaults.end(); ++it)
        if (!config.contains(it.key()))
            config[it.key()] = it.value();
    if (!config.contains("ocr_space_probability") && !config.contains("ocr_space_weight") &&
        !config.contains("ocr_space_min_probability"))
        config["ocr_space_probability"] = ocrSpaceProbability(settings);
    return config;
}

void setLyricButtonBusy(QPushButton *button, bool busy) {
    auto timer = button->findChild<QTimer *>("lyricButtonAnimation");
    if (!timer) {
        timer = new QTimer(button);
        timer->setObjectName("lyricButtonAnimation");
        timer->setInterval(75);
        QObject::connect(timer, &QTimer::timeout, button, [button] {
            int angle = (button->property("lyricBusyAngle").toInt() + 30) % 360;
            button->setProperty("lyricBusyAngle", angle);
            QPixmap icon(18, 18);
            icon.fill(Qt::transparent);
            QPainter painter(&icon);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(
                QPen(UiTheme::forWidget(button).color(QPalette::Accent), 2, Qt::SolidLine, Qt::RoundCap));
            painter.drawArc(QRectF(3, 3, 12, 12), angle * 16, 250 * 16);
            painter.end();
            button->setIcon(QIcon(icon));
        });
        // Put the icon after the action label.
        button->setLayoutDirection(Qt::RightToLeft);
    }
    button->setProperty("lyricBusy", busy);
    if (busy)
        timer->start();
    else {
        timer->stop();
        button->setIcon(QIcon());
    }
}

LyricQueue::LyricQueue(Library *library, QSettings *settings, QWidget *parent)
    : QWidget(parent), m_library(library), m_settings(settings) {
    setObjectName("lyricTaskQueue");
    setMinimumWidth(210);
    auto layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 0, 0, 0);
    auto title = new QLabel("识别队列");
    title->setObjectName("section");
    layout->addWidget(title);
    m_list = new QListWidget;
    m_list->setObjectName("lyricQueueTasks");
    m_list->setStyleSheet("QListWidget#lyricQueueTasks::item{padding:0;}"
                          "QWidget#lyricQueueCell{background:transparent;}");
    m_list->setWordWrap(true);
    layout->addWidget(m_list, 1);
    m_status = new QLabel;
    m_status->setWordWrap(true);
    m_status->setTextFormat(Qt::PlainText);
    layout->addWidget(m_status);
    m_runningStatus = new QLabel;
    m_runningStatus->setObjectName("lyricQueueProgressStatus");
    m_runningStatus->setWordWrap(true);
    m_runningStatus->setMaximumHeight(m_runningStatus->fontMetrics().lineSpacing() * 2);
    m_runningStatus->setTextFormat(Qt::PlainText);
    layout->addWidget(m_runningStatus);
    m_progress = new QProgressBar;
    m_progress->setObjectName("lyricQueueProgress");
    m_progress->setTextVisible(true);
    layout->addWidget(m_progress);
    auto buttons = new QHBoxLayout;
    m_cancelButton = new QPushButton("取消");
    m_cancelButton->setObjectName("cancelQueuedLyrics");
    m_retryButton = new QPushButton("重试");
    m_retryButton->setObjectName("retryQueuedLyrics");
    m_reviewButton = new QPushButton("查看 / 应用");
    m_reviewButton->setObjectName("reviewQueuedLyrics");
    for (auto button : {m_cancelButton, m_retryButton, m_reviewButton}) {
        button->setStyleSheet("padding:6px 4px;");
        buttons->addWidget(button);
    }
    layout->addLayout(buttons);
    connect(m_list, &QListWidget::currentRowChanged, this, [this] { refresh(); });
    connect(m_list, &QListWidget::itemDoubleClicked, this, [this] { review(); });
    connect(m_cancelButton, &QPushButton::clicked, this, [this] { cancelTask(m_list->currentRow()); });
    auto remove = new QShortcut(QKeySequence(Qt::Key_Delete), m_list);
    remove->setContext(Qt::WidgetWithChildrenShortcut);
    connect(remove, &QShortcut::activated, this, [this] { cancelTask(m_list->currentRow()); });
    connect(m_retryButton, &QPushButton::clicked, this, [this] {
        const int index = m_list->currentRow();
        if (index < 0 || (m_tasks[index].state != "失败" && m_tasks[index].state != "已取消"))
            return;
        m_tasks[index].state = "等待中";
        m_tasks[index].result = {};
        m_tasks[index].message.clear();
        refresh();
        QTimer::singleShot(0, this, &LyricQueue::startNext);
    });
    connect(m_reviewButton, &QPushButton::clicked, this, &LyricQueue::review);
    connect(library, &Library::changed, this, [this] {
        for (int i = int(m_tasks.size()) - 1; i >= 0; --i)
            if (m_library->track(m_tasks[i].track).isEmpty())
                cancelTask(i);
    });
    refresh();
}
bool LyricQueue::running() const {
    for (const auto &task : m_tasks)
        if (task.job || task.state == "等待中")
            return true;
    return false;
}
bool LyricQueue::activeFor(qint64 track) const {
    for (const auto &task : m_tasks)
        if (task.track == track && task.job && task.state == "运行中")
            return true;
    return false;
}
int LyricQueue::enqueue(qint64 track, QJsonObject config, LyricJob *job) {
    if (!recognitionFeaturesEnabled())
        return -1;
    const auto song = m_library->track(track);
    if (song.isEmpty())
        return -1;
    Task task;
    task.key = QUuid::createUuid().toString();
    task.track = track;
    task.title = song.value("title").toString();
    task.state = job ? "运行中" : "等待中";
    task.config = LyricJob::prepareConfig(m_settings, config);
    task.config["baseline_lrc"] = song.value("lrc").toString();
    m_tasks.append(task);
    const int index = int(m_tasks.size()) - 1;
    auto item = new QListWidgetItem(m_list);
    auto cell = new QWidget;
    cell->setObjectName("lyricQueueCell");
    auto row = new QHBoxLayout(cell);
    row->setContentsMargins(5, 4, 2, 4);
    auto text = new QLabel;
    text->setObjectName("queuedTaskText");
    text->setWordWrap(true);
    text->setTextFormat(Qt::PlainText);
    text->setAttribute(Qt::WA_TransparentForMouseEvents);
    row->addWidget(text, 1);
    auto remove = new QPushButton("×");
    remove->setObjectName("removeQueuedTask");
    remove->setToolTip("删除任务");
    remove->setFixedSize(25, 25);
    remove->setStyleSheet("padding:0;");
    row->addWidget(remove);
    item->setSizeHint(QSize(0, 66));
    m_list->setItemWidget(item, cell);
    connect(remove, &QPushButton::clicked, this, [this, key = task.key] {
        for (int i = 0; i < m_tasks.size(); ++i)
            if (m_tasks[i].key == key) {
                cancelTask(i);
                return;
            }
    });
    m_list->setCurrentRow(index);
    if (job)
        attach(index, job);
    refresh();
    QTimer::singleShot(0, this, &LyricQueue::startNext);
    return index;
}
QJsonObject LyricQueue::taskResult(int index) const {
    return index >= 0 && index < m_tasks.size() ? m_tasks[index].result : QJsonObject();
}
QString LyricQueue::taskState(int index) const {
    return index >= 0 && index < m_tasks.size() ? m_tasks[index].state : QString();
}
int LyricQueue::indexForJob(LyricJob *job) const {
    for (int i = 0; i < m_tasks.size(); ++i)
        if (m_tasks[i].job == job)
            return i;
    return -1;
}
void LyricQueue::attach(int index, LyricJob *job) {
    job->setParent(this);
    m_tasks[index].job = job;
    emit activity(m_tasks[index].track, m_tasks[index].config["operation"].toString(), true);
    connect(job, &LyricJob::progress, this, [this, job](const QString &text, int percent) {
        const int index = indexForJob(job);
        if (index < 0)
            return;
        m_tasks[index].message = text;
        m_tasks[index].percent = percent;
        refresh();
    });
    connect(job, &LyricJob::finished, this, [this, job](const QJsonObject &result) {
        const int index = indexForJob(job);
        if (index < 0)
            return;
        auto &task = m_tasks[index];
        task.result = result;
        if (task.state != "已取消")
            task.state = result["ok"].toBool() ? "待应用" : "失败";
        task.message =
            result["ok"].toBool() ? "完成后可检查文字和时间轴，再应用到原歌曲。" : result["error"].toString();
        task.percent = result["ok"].toBool() ? 100 : -1;
        task.job.clear();
        emit activity(task.track, task.config["operation"].toString(), false);
        refresh();
        emit taskFinished(index);
        job->deleteLater();
        QTimer::singleShot(0, this, &LyricQueue::startNext);
    });
}
void LyricQueue::startNext() {
    for (const auto &task : m_tasks)
        if (task.job)
            return;
    for (int i = 0; i < m_tasks.size(); ++i) {
        if (m_tasks[i].state != "等待中")
            continue;
        if (m_library->track(m_tasks[i].track).isEmpty()) {
            m_tasks[i].state = "已取消";
            continue;
        }
        m_tasks[i].state = "运行中";
        auto job = new LyricJob(m_settings, this);
        attach(i, job);
        refresh();
        job->start(m_tasks[i].config);
        return;
    }
    refresh();
}
void LyricQueue::cancelTrack(qint64 track) {
    for (int i = m_tasks.size() - 1; i >= 0; --i)
        if (m_tasks[i].track == track)
            cancelTask(i);
}
void LyricQueue::cancelTask(int index) {
    if (index < 0 || index >= m_tasks.size())
        return;
    const auto task = m_tasks[index];
    if (task.job) {
        disconnect(task.job, nullptr, this, nullptr);
        task.job->cancel();
        task.job->deleteLater();
    }
    {
        QSignalBlocker guard(m_list);
        m_tasks.removeAt(index);
        delete m_list->takeItem(index);
        if (!m_tasks.isEmpty())
            m_list->setCurrentRow(qMin(index, int(m_tasks.size()) - 1));
    }
    emit activity(task.track, task.config["operation"].toString(), false);
    refresh();
    QTimer::singleShot(0, this, &LyricQueue::startNext);
}
void LyricQueue::refresh() {
    for (int i = 0; i < m_tasks.size(); ++i) {
        const auto &task = m_tasks[i];
        const auto operation = task.config["operation"].toString();
        const auto method = operation == "ocr" ? "识别字幕" : "歌词时间匹配";
        const auto caption = task.title + "\n" + method + " · " + task.state;
        auto cell = m_list->itemWidget(m_list->item(i));
        cell->findChild<QLabel *>("queuedTaskText")->setText(caption);
        m_list->item(i)->setToolTip(caption);
        m_list->item(i)->setData(Qt::UserRole, task.track);
    }
    const int index = m_list->currentRow();
    const bool selected = index >= 0 && index < m_tasks.size();
    const auto state = selected ? m_tasks[index].state : QString();
    m_status->setText(selected ? m_tasks[index].message : QString());
    m_cancelButton->setEnabled(selected);
    m_cancelButton->setText(state == "等待中" || state == "运行中" ? "取消" : "删除");
    m_retryButton->setEnabled(state == "失败" || state == "已取消");
    m_reviewButton->setEnabled(state == "待应用" || state == "已应用");
    int active = -1, waiting = 0;
    for (int i = 0; i < m_tasks.size(); ++i) {
        if (m_tasks[i].state == "运行中")
            active = i;
        if (m_tasks[i].state == "等待中")
            ++waiting;
    }
    m_progress->show();
    if (active >= 0) {
        const auto &task = m_tasks[active];
        m_progress->setProperty("trackId", task.track);
        const int percent = task.percent;
        m_progress->setRange(0, percent < 0 ? 0 : 100);
        m_progress->setFormat("%p%");
        if (percent >= 0)
            m_progress->setValue(percent);
        m_runningStatus->setText(task.title + " · " + (task.message.isEmpty() ? "准备中" : task.message));
        m_runningStatus->setToolTip(m_runningStatus->text());
    } else {
        m_progress->setProperty("trackId", 0);
        m_progress->setRange(0, 100);
        const bool complete = selected && (state == "待应用" || state == "已应用");
        m_progress->setValue(complete ? 100 : 0);
        m_progress->setFormat(waiting ? "等待执行" : complete ? "100% · 已完成" : "等待任务");
        m_runningStatus->clear();
    }
}
void LyricQueue::review() {
    const int index = m_list->currentRow();
    if (index < 0 || !m_tasks[index].result["ok"].toBool())
        return;
    const auto track = m_tasks[index].track;
    QDialog dialog(this);
    dialog.setObjectName("queuedLyricReview");
    dialog.setWindowTitle(m_tasks[index].title + " · 检查歌词");
    dialog.resize(760, 560);
    auto layout = new QVBoxLayout(&dialog);
    layout->setContentsMargins(20, 18, 20, 18);
    layout->setSpacing(14);
    auto heading = new QLabel(m_tasks[index].title + " · 检查歌词");
    heading->setObjectName("section");
    layout->addWidget(heading);
    auto summary = new QLabel(QString("%1 · %2 句歌词 · 检查后应用到原歌曲")
                                  .arg(m_tasks[index].state)
                                  .arg(Lrc::parse(m_tasks[index].result["lrc"].toString()).size()));
    summary->setObjectName("muted");
    layout->addWidget(summary);
    auto warning = new QPlainTextEdit;
    warning->setObjectName("queuedLyricWarnings");
    warning->setReadOnly(true);
    warning->setMaximumHeight(140);
    QStringList warnings;
    for (const auto &value : m_tasks[index].result["warnings"].toArray())
        warnings << value.toString();
    if (m_library->track(track).value("lrc").toString() != m_tasks[index].config["baseline_lrc"].toString())
        warnings.prepend("此歌曲的歌词在任务提交后有过修改；应用将替换目前版本。");
    warning->setPlainText(warnings.join('\n'));
    warning->setVisible(!warnings.isEmpty());
    layout->addWidget(warning);
    auto editor = new QPlainTextEdit(m_tasks[index].result["lrc"].toString());
    editor->setObjectName("queuedLyricResult");
    auto tabs = new QTabWidget;
    tabs->setObjectName("queuedLyricReviewTabs");
    auto lines = new QTreeWidget;
    lines->setObjectName("queuedLyricLines");
    lines->setColumnCount(3);
    lines->setHeaderLabels({"时间", "歌词（双击编辑）", "操作"});
    lines->setRootIsDecorated(false);
    lines->setAlternatingRowColors(true);
    lines->setStyleSheet("QTreeWidget::item{padding:0;}");
    lines->header()->setStretchLastSection(false);
    lines->header()->setSectionResizeMode(0, QHeaderView::Fixed);
    lines->header()->resizeSection(0, 120);
    lines->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    lines->header()->setSectionResizeMode(2, QHeaderView::Fixed);
    lines->header()->resizeSection(2, 108);
    auto populate = [lines, editor, summary, state = m_tasks[index].state] {
        const QSignalBlocker guard(lines);
        lines->clear();
        const auto parsed = Lrc::parse(editor->toPlainText());
        summary->setText(QString("%1 · %2 句歌词 · 检查后应用到原歌曲").arg(state).arg(parsed.size()));
        for (int row = 0; row < parsed.size(); ++row) {
            const auto &line = parsed[row];
            const auto time = QString("%1:%2.%3")
                                  .arg(line.ms / 60000, 2, 10, QChar('0'))
                                  .arg(line.ms / 1000 % 60, 2, 10, QChar('0'))
                                  .arg(line.ms % 1000, 3, 10, QChar('0'));
            auto item = new QTreeWidgetItem(lines, {time, line.text, {}});
            item->setFlags(item->flags() | Qt::ItemIsEditable);
            item->setToolTip(1, line.text);
            auto cell = new QWidget;
            auto actions = new QHBoxLayout(cell);
            actions->setContentsMargins(2, 2, 2, 2);
            actions->setSpacing(4);
            for (int action : {-1, 1, 0}) {
                auto button = new QPushButton(action < 0 ? "▴" : action > 0 ? "▾" : "×");
                button->setFixedSize(28, 28);
                button->setStyleSheet("padding:0;");
                button->setToolTip(action < 0 ? "提前 100 ms" : action > 0 ? "延后 100 ms" : "删除这一句");
                actions->addWidget(button);
                QObject::connect(button, &QPushButton::clicked, editor, [editor, row, action] {
                    auto updated = Lrc::parse(editor->toPlainText());
                    if (row >= updated.size())
                        return;
                    if (!action)
                        updated.removeAt(row);
                    else {
                        const auto lower = row ? updated[row - 1].ms + 1 : 0;
                        const auto upper =
                            row + 1 < updated.size() ? updated[row + 1].ms - 1 : updated[row].ms + 100;
                        updated[row].ms = qBound(lower, updated[row].ms + action * 100, upper);
                    }
                    editor->setPlainText(Lrc::serialize(updated));
                });
            }
            item->setSizeHint(0, QSize(0, qMax(42, line.text.count('\n') * 20 + 30)));
            lines->setItemWidget(item, 2, cell);
        }
    };
    bool validRowEdits = true;
    connect(editor, &QPlainTextEdit::textChanged, &dialog, [&, populate] {
        validRowEdits = true;
        populate();
    });
    connect(lines, &QTreeWidget::itemChanged, &dialog, [&](QTreeWidgetItem *, int column) {
        if (column > 1)
            return;
        QList<LyricLine> updated;
        for (int row = 0; row < lines->topLevelItemCount(); ++row) {
            const auto item = lines->topLevelItem(row);
            const auto parsed = Lrc::parse("[" + item->text(0) + "]x");
            if (parsed.size() != 1 || item->text(1).trimmed().isEmpty()) {
                validRowEdits = false;
                warning->setPlainText("请填写有效的时间（分:秒.毫秒）与歌词，或使用 × 删除整句。");
                warning->show();
                return;
            }
            updated << LyricLine{parsed[0].ms, item->text(1)};
        }
        // Save the edit immediately, including when Enter also activates Apply;
        // rebuild only after the active item editor has finished.
        validRowEdits = true;
        {
            const QSignalBlocker guard(editor);
            editor->setPlainText(Lrc::serialize(updated));
        }
        QTimer::singleShot(0, &dialog, populate);
    });
    populate();
    tabs->addTab(lines, "逐句检查");
    tabs->addTab(editor, "LRC 文本");
    layout->addWidget(tabs, 1);
    auto buttons = new QDialogButtonBox(QDialogButtonBox::Apply | QDialogButtonBox::Close);
    buttons->button(QDialogButtonBox::Apply)->setText("应用歌词");
    buttons->button(QDialogButtonBox::Apply)->setProperty("primary", true);
    buttons->button(QDialogButtonBox::Close)->setText("关闭");
    if (m_tasks[index].result["timing_kind"].toString() == "video_caption" &&
        m_library->track(track).value("storage").toString() != "url" &&
        QFileInfo::exists(m_library->track(track).value("path").toString())) {
        auto calibrate = buttons->addButton("按音频校准…", QDialogButtonBox::ActionRole);
        calibrate->setObjectName("calibrateCaptionTiming");
        calibrate->setToolTip("先核对字幕文字和演唱顺序，再用同源字幕时间定位每句，按本地音频校准");
        connect(calibrate, &QPushButton::clicked, &dialog, [&] {
            LyricWizard wizard(m_library, m_settings, track, editor->toPlainText(), &dialog);
            wizard.setQueue(this);
            wizard.findChild<QCheckBox *>("useExistingTimingHints")->setChecked(true);
            if (wizard.exec() == QDialog::Accepted)
                dialog.accept();
        });
    }
    layout->addWidget(buttons);
    connect(buttons->button(QDialogButtonBox::Close), &QPushButton::clicked, &dialog, &QDialog::reject);
    connect(buttons->button(QDialogButtonBox::Apply), &QPushButton::clicked, &dialog, [&] {
        if (!validRowEdits)
            return;
        if (Lrc::parse(editor->toPlainText()).isEmpty()) {
            warning->setPlainText("请保留有效的 LRC 时间轴。");
            warning->show();
            return;
        }
        if (!m_library->setLyrics(track, editor->toPlainText(), m_tasks[index].result["source"].toString())) {
            warning->setPlainText("歌曲已不存在或保存失败。");
            warning->show();
            return;
        }
        m_library->setOffset(track, 0);
        m_tasks[index].result["lrc"] = editor->toPlainText();
        m_tasks[index].state = "已应用";
        refresh();
        dialog.accept();
    });
    dialog.exec();
}
void LyricJob::complete() {
    if (m_delivered)
        return;
    m_delivered = true;
    m_timeout.stop();
    QFile file(m_result);
    QJsonObject result;
    if (file.open(QIODevice::ReadOnly))
        result = QJsonDocument::fromJson(file.readAll()).object();
    if (m_cancelled)
        result = {{"ok", false}, {"error", "已取消，原歌词保持不变"}};
    else if (result.isEmpty())
        result = {{"ok", false},
                  {"error", "歌词工具未完成：" + (m_errors.isEmpty() ? m_process.errorString()
                                                                     : QString::fromUtf8(m_errors))}};
    emit finished(result);
}
void LyricJob::cancel() {
    if (!busy())
        return;
    m_cancelled = true;
    QFile flag(m_cancel);
    if (flag.open(QIODevice::WriteOnly))
        flag.write("cancel");
#ifdef Q_OS_WIN
    if (m_jobHandle)
        TerminateJobObject(HANDLE(m_jobHandle), 1);
#endif
    m_process.kill();
}

QString LyricWizard::plainText(const QString &text) {
    QStringList lines;
    const auto timed = Lrc::parse(text);
    if (!timed.isEmpty()) {
        for (const auto &line : timed)
            if (!line.text.trimmed().isEmpty())
                lines << line.text.trimmed();
    } else {
        const bool srt = text.contains("-->");
        for (auto line : text.split('\n')) {
            line = line.trimmed();
            line.remove(QChar(0xfeff));
            if (line.isEmpty() || line.contains("-->") ||
                (srt && QRegularExpression("^\\d+$").match(line).hasMatch()) ||
                QRegularExpression("^\\[[a-zA-Z]+:.*\\]$").match(line).hasMatch())
                continue;
            lines << line;
        }
    }
    return lines.join('\n');
}
LyricWizard::LyricWizard(Library *library, QSettings *settings, qint64 track, const QString &initialText,
                         QWidget *parent)
    : QDialog(parent), m_library(library), m_settings(settings), m_track(track),
      m_job(new LyricJob(settings, this)), m_frameWork(lyricJobTemplate(settings)) {
    setObjectName("lyricWizard");
    setWindowTitle("歌词方案 · " + library->track(track).value("title").toString());
    resize(820, 820);
    setMinimumSize(620, 560);
    auto layout = new QVBoxLayout(this);
    m_step = new QLabel;
    m_step->setObjectName("lyricWizardStep");
    layout->addWidget(m_step);
    m_pages = new QStackedWidget;
    layout->addWidget(m_pages, 1);
    auto first = new QWidget;
    auto sourceLayout = new QVBoxLayout(first);
    m_method = new QComboBox;
    m_method->setObjectName("lyricMethod");
    m_method->addItems({"导入歌词 → 音频匹配", "识别字幕 → 字幕出现时间", "在线搜索现成歌词"});
    sourceLayout->addWidget(m_method);
    m_sources = new QStackedWidget;
    sourceLayout->addWidget(m_sources, 1);
    auto local = new QWidget;
    auto localLayout = new QVBoxLayout(local);
    auto hint =
        new QLabel("支持 UTF-8 TXT、LRC、SRT。下一步可逐行编辑文字；副歌应按实际演唱顺序重复。\n有时间轴的 "
                   "LRC 可直接使用，也可按当前音频重新匹配。");
    hint->setWordWrap(true);
    localLayout->addWidget(hint);
    const auto song = m_library->track(m_track);
    auto aiPrompt = new QPlainTextEdit;
    aiPrompt->setObjectName("lyricAiPrompt");
    aiPrompt->setReadOnly(true);
    aiPrompt->setMaximumHeight(160);
    aiPrompt->setPlainText(
        QString("请查找下面歌曲的准确歌词。\n歌名：%1\n歌手：%2\n"
                "请核对演唱版本，按实际演唱顺序每行一句，保留副歌重复。只输出纯歌词，不要标题、"
                "歌手信息、时间标签、解释或 Markdown；无法确认的内容请明确说明，不要编造。")
            .arg(song.value("title").toString(), song.value("artist").toString()));
    localLayout->addWidget(aiPrompt);
    auto ask = new QPushButton("询问 AI · 大肥鱼");
    ask->setObjectName("askLyricAi");
    ask->setToolTip("复制固定提问格式并打开 AI 网页，回复后粘贴歌词或导入 TXT");
    localLayout->addWidget(ask);
    auto input = new QPushButton("输入 / 粘贴歌词");
    input->setObjectName("inputLyricText");
    localLayout->addWidget(input);
    auto load = new QPushButton("导入 TXT / LRC…");
    load->setObjectName("loadLyricText");
    localLayout->addWidget(load);
    localLayout->addStretch();
    m_sources->addWidget(local);
    auto video = new QWidget;
    auto videoLayout = new QVBoxLayout(video);
    videoLayout->setContentsMargins(0, 0, 0, 0);
    auto videoRow = new QHBoxLayout;
    m_video = new QLineEdit(library->track(track).value("source").toString());
    m_video->setObjectName("lyricVideoInput");
    m_video->setPlaceholderText("本地视频文件 / 哔哩哔哩 HTTPS 链接（含 ?p= 分 P）");
    auto browseVideo = new QPushButton("本地视频…");
    browseVideo->setObjectName("browseLyricVideo");
    videoRow->addWidget(m_video, 1);
    videoRow->addWidget(browseVideo);
    auto languageRow = new QHBoxLayout;
    languageRow->addWidget(new QLabel("字幕语言"));
    auto ocrLanguage = new QComboBox;
    ocrLanguage->setObjectName("lyricOcrLanguage");
    ocrLanguage->addItem("中英", "zh");
    ocrLanguage->addItem("俄语", "ru");
    ocrLanguage->addItem("意大利语", "it");
    ocrLanguage->addItem("英汉双语", "en_zh");
    ocrLanguage->addItem("俄汉双语", "ru_zh");
    ocrLanguage->addItem("意汉双语", "it_zh");
    ocrLanguage->setCurrentIndex(qMax(0, ocrLanguage->findData(settings->value("lyrics/ocrLanguage", "zh"))));
    ocrLanguage->setMaximumWidth(150);
    languageRow->addWidget(ocrLanguage);
    languageRow->addStretch();
    videoLayout->addLayout(videoRow);
    m_preview = new LyricCropPreview;
    videoLayout->addWidget(m_preview, 1);
    auto cropRow = new QHBoxLayout;
    const QStringList captions{"左边距", "右边距", "上边距", "下边距"};
    const QStringList names{"lyricMarginLeft", "lyricMarginRight", "lyricMarginTop", "lyricMarginBottom"};
    const double defaults[]{0, 0, 66.6667, 0};
    for (int i = 0; i < 4; ++i) {
        cropRow->addWidget(new QLabel(captions[i]));
        m_margins[i] = new QDoubleSpinBox;
        m_margins[i]->setObjectName(names[i]);
        m_margins[i]->setRange(0, 100);
        m_margins[i]->setDecimals(4);
        m_margins[i]->setValue(defaults[i]);
        m_margins[i]->setSuffix(" %");
        m_margins[i]->setMaximumWidth(110);
        m_margins[i]->setStyleSheet("font-size:12px;padding-left:5px;padding-right:18px;");
        cropRow->addWidget(m_margins[i]);
    }
    auto sampling = new QHBoxLayout;
    sampling->addStretch();
    m_interval = new QDoubleSpinBox;
    m_interval->setRange(.1, 5);
    m_interval->setValue(.5);
    m_interval->setSuffix(" 秒 / 帧");
    m_interval->setObjectName("lyricOcrInterval");
    m_interval->setMaximumWidth(150);
    m_previewTime = new QDoubleSpinBox;
    m_previewTime->setRange(0, 86400);
    m_previewTime->setValue(0);
    m_previewTime->setObjectName("lyricPreviewTime");
    m_previewTime->setReadOnly(true);
    m_previewTime->setButtonSymbols(QAbstractSpinBox::NoButtons);
    m_previewTime->setSuffix(" 秒");
    m_previewTime->setMaximumWidth(100);
    sampling->addWidget(new QLabel("预览位置"));
    sampling->addWidget(m_previewTime);
    m_previewButton = new QPushButton("向后 15 s →");
    m_previewButton->setObjectName("previewLyricCrop");
    sampling->addWidget(m_previewButton);
    videoLayout->addLayout(sampling);
    videoLayout->addLayout(cropRow);
    auto comparison = new QHBoxLayout;
    comparison->addWidget(new QLabel("采样间隔"));
    comparison->addWidget(m_interval);
    comparison->addWidget(new QLabel("画面变化阈值"));
    m_changeThreshold = new QDoubleSpinBox;
    m_changeThreshold->setObjectName("lyricChangeThreshold");
    m_changeThreshold->setRange(5, 80);
    m_changeThreshold->setDecimals(0);
    m_changeThreshold->setSuffix(" %");
    m_changeThreshold->setValue(settings->value("lyrics/changeThreshold", 22).toDouble());
    m_changeThreshold->setMaximumWidth(110);
    m_changeThreshold->setToolTip("越低越容易划分新字幕；背景变化多时可调高。建议截取紧贴字幕的范围。");
    comparison->addWidget(m_changeThreshold);
    comparison->addWidget(new QLabel("文字合并相似度"));
    m_textSimilarity = new QDoubleSpinBox;
    m_textSimilarity->setObjectName("lyricTextSimilarity");
    m_textSimilarity->setRange(60, 100);
    m_textSimilarity->setDecimals(0);
    m_textSimilarity->setSuffix(" %");
    m_textSimilarity->setMaximumWidth(110);
    if (settings->value("lyrics/textSimilarityRevision", 0).toInt() < 1) {
        settings->setValue("lyrics/textSimilarity", 60);
        settings->setValue("lyrics/textSimilarityRevision", 1);
    }
    m_textSimilarity->setValue(settings->value("lyrics/textSimilarity", 60).toDouble());
    m_textSimilarity->setToolTip(
        "相邻文字达到该相似度时合并，采用整段中间位置的识别结果；空白间隔不会合并。");
    comparison->addWidget(m_textSimilarity);
    comparison->addStretch();
    videoLayout->addLayout(comparison);
    videoLayout->addLayout(languageRow);
    auto spacing = new QHBoxLayout;
    spacing->addWidget(new QLabel("空格出现概率"));
    m_spaceProbability = new QDoubleSpinBox;
    m_spaceProbability->setObjectName("lyricOcrSpaceProbability");
    m_spaceProbability->setRange(0, 100);
    m_spaceProbability->setDecimals(1);
    m_spaceProbability->setSingleStep(1);
    m_spaceProbability->setSuffix(" %");
    m_spaceProbability->setValue(ocrSpaceProbability(settings) * 100.);
    m_spaceProbability->setMaximumWidth(140);
    m_spaceProbability->setToolTip(
        "默认 50% 保持模型原始判断；越高越容易出现空格，越低越少，0% 关闭模型空格。"
        "这是空格倾向，不是空格占比或识别准确率；100% 也不会强制插入空格。");
    spacing->addWidget(m_spaceProbability);
    spacing->addStretch();
    videoLayout->addLayout(spacing);
    auto frameCheck = new QHBoxLayout;
    m_frameResult = new QPlainTextEdit;
    m_frameResult->setObjectName("lyricFrameResult");
    m_frameResult->setReadOnly(true);
    m_frameResult->setFrameShape(QFrame::StyledPanel);
    m_frameResult->setPlaceholderText("当前帧的框选识别结果");
    m_frameResult->setStyleSheet("padding:3px;");
    m_frameResult->setFixedHeight(52);
    frameCheck->addWidget(m_frameResult, 1);
    m_frameButton = new QPushButton("识别当前框选内容");
    m_frameButton->setAutoDefault(false);
    m_frameButton->setObjectName("recognizeCurrentLyricFrame");
    m_frameButton->setToolTip("只识别上方当前预览图中的绿框区域；不获取新视频帧，不覆盖歌曲歌词。");
    m_frameButton->setEnabled(false);
    frameCheck->addWidget(m_frameButton, 0, Qt::AlignRight | Qt::AlignVCenter);
    videoLayout->addLayout(frameCheck);
    m_ocrButton = new QPushButton("开始字幕识别");
    m_ocrButton->setObjectName("extractVideoLyrics");
    // Submission uses the existing bottom-right action, not a second visible button.
    m_ocrButton->setParent(video);
    m_ocrButton->hide();
    m_sources->addWidget(video);
    auto online = new QWidget;
    auto onlineLayout = new QVBoxLayout(online);
    auto form = new QFormLayout;
    m_title = new QLineEdit(library->track(track).value("title").toString());
    m_artist = new QLineEdit(library->track(track).value("artist").toString());
    m_title->setObjectName("onlineLyricTitle");
    m_artist->setObjectName("onlineLyricArtist");
    form->addRow("歌名", m_title);
    form->addRow("歌手", m_artist);
    onlineLayout->addLayout(form);
    m_search = new QPushButton("搜索 LRCLIB 歌词（含纯文本）");
    m_search->setObjectName("searchPlainLyrics");
    onlineLayout->addWidget(m_search);
    m_candidates = new QListWidget;
    m_candidates->setObjectName("plainLyricCandidates");
    onlineLayout->addWidget(m_candidates, 1);
    auto onlineHint = new QLabel("选择候选后进入下一步检查文字。现场、翻唱版本可以使用同一歌词重新匹配时间；"
                                 "删句或副歌次数不同需要先修改。");
    onlineHint->setWordWrap(true);
    onlineLayout->addWidget(onlineHint);
    m_sources->addWidget(online);
    auto scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setWidget(first);
    m_pages->addWidget(scroll);
    auto review = new QWidget;
    auto reviewLayout = new QVBoxLayout(review);
    auto reviewHint = new QLabel("步骤 "
                                 "2：每行一句，保留实际唱出的副歌重复。请删除标题、作词者、水印等非演唱文字。"
                                 "模型只匹配时间，保留下面的文字。");
    reviewHint->setWordWrap(true);
    reviewLayout->addWidget(reviewHint);
    m_text = new QPlainTextEdit;
    m_text->setObjectName("lyricTextEditor");
    m_text->setPlaceholderText("在此粘贴或修改歌词，每行一句…");
    reviewLayout->addWidget(m_text, 1);
    m_language = new QComboBox;
    m_language->setObjectName("lyricAlignLanguage");
    m_language->addItem("自动检测", "auto");
    m_language->addItem("中文", "zh");
    m_language->addItem("英文", "en");
    m_language->addItem("日文", "ja");
    m_language->addItem("韩文", "ko");
    m_language->addItem("俄语", "ru");
    m_language->addItem("意大利语", "it");
    m_language->setCurrentIndex(qMax(0, m_language->findData(settings->value("lyrics/language", "auto"))));
    reviewLayout->addWidget(m_language);
    m_keepTiming = new QCheckBox("直接使用已有 LRC 时间轴（不重新对齐）");
    m_keepTiming->setObjectName("keepExistingTiming");
    reviewLayout->addWidget(m_keepTiming);
    m_useTimingHints = new QCheckBox("用已有时间定位，再按音频校准（仅限同一演唱版本）");
    m_useTimingHints->setObjectName("useExistingTimingHints");
    m_useTimingHints->setToolTip("保留字幕的逐句顺序，避免前奏、间奏和重复副歌串位；不同版本的 LRC 请勿勾选");
    m_useTimingHints->setEnabled(false);
    reviewLayout->addWidget(m_useTimingHints);
    connect(m_keepTiming, &QCheckBox::toggled, this, [this](bool keep) {
        m_useTimingHints->setEnabled(!keep && !Lrc::parse(m_text->toPlainText()).isEmpty());
        if (keep)
            m_useTimingHints->setChecked(false);
    });
    m_pages->addWidget(review);
    auto last = new QWidget;
    auto lastLayout = new QVBoxLayout(last);
    auto resultHint = new QLabel(
        "步骤 3：检查生成的 LRC，可修改时间或文字，点击应用后替换当前歌曲的歌词。取消不会覆盖原歌词。");
    resultHint->setWordWrap(true);
    lastLayout->addWidget(resultHint);
    m_result = new QPlainTextEdit;
    m_result->setObjectName("alignedLyricResult");
    lastLayout->addWidget(m_result, 1);
    m_pages->addWidget(last);
    m_status = new QLabel;
    m_status->setObjectName("lyricWizardStatus");
    m_status->setWordWrap(true);
    m_status->setTextFormat(Qt::PlainText);
    layout->addWidget(m_status);
    m_progress = new QProgressBar;
    m_progress->hide();
    layout->addWidget(m_progress);
    auto navigation = new QHBoxLayout;
    m_back = new QPushButton("上一步");
    m_back->setObjectName("lyricWizardBack");
    auto cancelJob = new QPushButton("取消任务");
    cancelJob->setObjectName("cancelLyricJob");
    auto close = new QPushButton("关闭");
    m_next = new QPushButton;
    m_next->setObjectName("lyricWizardNext");
    m_next->setProperty("primary", true);
    navigation->addWidget(m_back);
    navigation->addWidget(cancelJob);
    navigation->addStretch();
    navigation->addWidget(close);
    navigation->addWidget(m_next);
    layout->addLayout(navigation);
    connect(m_method, &QComboBox::currentIndexChanged, this, [this](int index) {
        m_sources->setCurrentIndex(index);
        m_status->clear();
        if (index != 1) {
            m_previewRefresh.stop();
            m_previewPending = false;
            if (m_job && m_job->busy() && m_operation == "preview")
                m_job->cancel();
        }
        updatePage();
        if (index == 1 && isVisible())
            m_previewRefresh.start(0);
    });
    connect(ask, &QPushButton::clicked, this, [this, aiPrompt] {
        QApplication::clipboard()->setText(aiPrompt->toPlainText());
        QDesktopServices::openUrl(
            QUrl(m_settings->value("lyrics/aiUrl", "https://chat.deepseek.com/").toString()));
        m_status->setText("提问格式已复制，请在大肥鱼粘贴提问，再把回复输入或导入 TXT。");
    });
    connect(input, &QPushButton::clicked, this, [this] {
        m_page = 1;
        updatePage();
        m_text->setFocus();
    });
    connect(load, &QPushButton::clicked, this, [this] {
        const auto path =
            QFileDialog::getOpenFileName(this, "导入已有歌词", {}, "歌词 (*.txt *.lrc *.srt);;所有文件 (*)");
        if (path.isEmpty())
            return;
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            m_status->setText("无法打开歌词文件");
            return;
        }
        m_text->setPlainText(QString::fromUtf8(file.readAll()));
        m_source = "本地歌词文件";
        m_page = 1;
        updatePage();
    });
    connect(browseVideo, &QPushButton::clicked, this, [this] {
        auto path = QFileDialog::getOpenFileName(this, "选择当前歌曲的视频", {},
                                                 "视频 (*.mp4 *.mkv *.flv *.webm *.mov *.m4s);;所有文件 (*)");
        if (!path.isEmpty())
            m_video->setText(path);
    });
    m_previewRefresh.setSingleShot(true);
    connect(&m_previewRefresh, &QTimer::timeout, this, &LyricWizard::refreshPreview);
    connect(m_previewButton, &QPushButton::clicked, this, [this] {
        m_previewTime->setValue(m_previewTime->value() + 15);
        refreshPreview();
    });
    connect(m_video, &QLineEdit::textChanged, this, [this] {
        m_previewTime->setValue(0);
        m_preview->clear();
        if (isVisible() && m_method->currentIndex() == 1)
            m_previewRefresh.start(500);
    });
    for (auto margin : m_margins)
        connect(margin, &QDoubleSpinBox::valueChanged, this, [this] {
            const double left = m_margins[0]->value() / 100., right = m_margins[1]->value() / 100.;
            const double top = m_margins[2]->value() / 100., bottom = m_margins[3]->value() / 100.;
            m_preview->setSelection({left, top, 1. - left - right, 1. - top - bottom});
            syncMargins();
        });
    connect(m_preview, &LyricCropPreview::selectionChanged, this, [this] { syncMargins(); });
    connect(m_preview, &LyricCropPreview::selectionChanged, this, &LyricWizard::invalidateFrameResult);
    connect(m_preview, &LyricCropPreview::imageChanged, this, [this] {
        invalidateFrameResult();
        m_frameButton->setEnabled(m_preview->hasImage() && !m_job->busy());
    });
    connect(ocrLanguage, &QComboBox::currentIndexChanged, this, &LyricWizard::invalidateFrameResult);
    connect(m_spaceProbability, &QDoubleSpinBox::valueChanged, this, [this] {
        m_settings->setValue("lyrics/ocrSpaceProbability", m_spaceProbability->value() / 100.);
        invalidateFrameResult();
    });
    connect(m_frameButton, &QPushButton::clicked, this, &LyricWizard::recognizeCurrentFrame);
    syncMargins();
    connect(m_ocrButton, &QPushButton::clicked, this, [this] { runVideo(false); });
    connect(m_search, &QPushButton::clicked, this, &LyricWizard::searchOnline);
    connect(m_candidates, &QListWidget::currentItemChanged, this, [this](auto item, auto) {
        if (!item)
            return;
        auto c = item->data(Qt::UserRole).toMap();
        auto text = c.value("plainLyrics").toString();
        if (text.isEmpty())
            text = plainText(c.value("syncedLyrics").toString());
        m_text->setPlainText(text);
        m_source = "LRCLIB · " + c.value("trackName").toString() + " / " + c.value("artistName").toString();
        m_status->setText(text.left(400));
    });
    connect(m_back, &QPushButton::clicked, this, [this] {
        --m_page;
        updatePage();
    });
    connect(m_next, &QPushButton::clicked, this, &LyricWizard::next);
    connect(close, &QPushButton::clicked, this, &QDialog::reject);
    connect(cancelJob, &QPushButton::clicked, this, [this] {
        if (m_job)
            m_job->cancel();
    });
    connect(m_text, &QPlainTextEdit::textChanged, this, [this] {
        m_keepTiming->setEnabled(!Lrc::parse(m_text->toPlainText()).isEmpty());
        if (!m_keepTiming->isEnabled())
            m_keepTiming->setChecked(false);
        m_useTimingHints->setEnabled(m_keepTiming->isEnabled() && !m_keepTiming->isChecked());
        if (!m_useTimingHints->isEnabled())
            m_useTimingHints->setChecked(false);
    });
    connect(m_job, &LyricJob::progress, this, [this](const QString &text, int percent) {
        m_status->setText(text);
        m_progress->setRange(0, percent < 0 ? 0 : 100);
        if (percent >= 0)
            m_progress->setValue(percent);
    });
    connect(m_job, &LyricJob::finished, this, [this](const QJsonObject &value) {
        updatePage();
        m_progress->hide();
        if (m_previewPending) {
            m_previewPending = false;
            m_previewRefresh.start(0);
            return;
        }
        if (!value["ok"].toBool()) {
            if (m_operation == "ocr_frame")
                m_frameResult->setPlainText(value["error"].toString());
            m_status->setText(value["error"].toString());
            return;
        }
        if (m_operation == "preview") {
            m_preview->setImage(QImage(value["frame_preview"].toString(value["preview"].toString())));
            syncMargins();
            m_previewTime->setValue(value["preview_time"].toDouble());
            m_status->clear();
        } else if (m_operation == "ocr_frame") {
            const auto text = value["text"].toString();
            m_frameResult->setPlainText(text.isEmpty() ? "当前框选区域未识别到文字" : text);
            m_frameResult->setProperty("frameResult", value.toVariantMap());
            m_status->clear();
        } else if (m_operation == "ocr") {
            m_text->setPlainText(value["text"].toString());
            m_result->setPlainText(value["lrc"].toString());
            m_source = value["source"].toString();
            m_page = 2;
            updatePage();
            m_status->setText(
                "已提取字幕出现时间，尚未按音频校准。请检查错字及卡拉 OK 预告句；同源字幕可再按音频校准。");
        } else {
            m_result->setPlainText(value["lrc"].toString());
            m_source = value["source"].toString();
            const auto warnings = value["warnings"].toArray();
            m_status->setText(
                warnings.isEmpty()
                    ? "已生成时间轴，文字保持原样；应用后可在工作台试听。"
                    : QString("%1 行需试听检查。%2").arg(warnings.size()).arg(warnings.first().toString()));
        }
    });
    if (!initialText.isEmpty()) {
        m_text->setPlainText(initialText);
        m_page = 1;
    } else
        m_method->setCurrentIndex(1);
    updatePage();
}
void LyricWizard::showEvent(QShowEvent *event) {
    QDialog::showEvent(event);
    if (m_page == 0 && m_method->currentIndex() == 1)
        m_previewRefresh.start(0);
}
void LyricWizard::refreshPreview() {
    if (!isVisible() || m_page != 0 || m_method->currentIndex() != 1 || m_video->text().trimmed().isEmpty())
        return;
    if (m_job && m_job->busy()) {
        if (m_operation == "preview") {
            m_previewPending = true;
            m_job->cancel();
        }
        return;
    }
    runVideo(true);
}
LyricWizard::~LyricWizard() {
    for (auto reply : m_network.findChildren<QNetworkReply *>()) {
        reply->disconnect(this);
        reply->abort();
    }
    if (m_job)
        m_job->cancel();
}
void LyricWizard::updatePage() {
    m_pages->setCurrentIndex(m_page);
    m_step->setText(QString("%1 / 3    %2")
                        .arg(m_page + 1)
                        .arg(QStringList{"选择歌词来源", "检查歌词文字", "生成并应用时间轴"}[m_page]));
    const bool busy = m_job && m_job->busy();
    m_back->setEnabled(m_page > 0 && !busy);
    m_next->setEnabled(!busy && !m_searching);
    m_method->setEnabled(!busy || m_operation == "preview");
    m_previewButton->setEnabled(!busy);
    m_frameButton->setEnabled(!busy && m_preview->hasImage());
    m_video->setEnabled(!busy || m_operation == "preview");
    findChild<QPushButton *>("browseLyricVideo")->setEnabled(!busy || m_operation == "preview");
    m_preview->setEnabled(!busy || m_operation == "preview");
    for (auto margin : m_margins)
        margin->setEnabled(!busy || m_operation == "preview");
    findChild<QComboBox *>("lyricOcrLanguage")->setEnabled(!busy);
    m_spaceProbability->setEnabled(!busy);
    m_ocrButton->setEnabled(!busy);
    m_search->setEnabled(!busy && !m_searching);
    m_next->setText(QStringList{"下一步：检查文字", "按音频匹配时间", "应用歌词"}[m_page]);
    if (m_page == 0 && m_method->currentIndex() == 1)
        m_next->setText("识别字幕并匹配时间");
    setLyricButtonBusy(m_previewButton, busy && m_operation == "preview");
    setLyricButtonBusy(m_ocrButton, busy && m_operation == "ocr");
    setLyricButtonBusy(m_frameButton, busy && m_operation == "ocr_frame");
    setLyricButtonBusy(m_next, busy && m_operation != "preview" && m_operation != "ocr_frame");
    setLyricButtonBusy(m_search, m_searching);
    m_result->setReadOnly(busy);
    m_progress->setVisible(busy);
}
void LyricWizard::next() {
    if (m_page == 0) {
        if (m_method->currentIndex() == 1) {
            runVideo(false);
            return;
        }
        if (m_method->currentIndex() == 2 && !m_candidates->currentItem()) {
            m_status->setText("请先搜索并选择一个歌词版本。");
            return;
        }
        m_page = 1;
        updatePage();
    } else if (m_page == 1) {
        const auto text = m_text->toPlainText();
        if (m_library->track(m_track).value("storage").toString() == "url" && !m_keepTiming->isChecked()) {
            m_status->setText("URL 歌曲请先下载音频再执行本机时间对齐；已有 LRC 可勾选保留时间轴直接使用。");
            return;
        }
        if (plainText(text).isEmpty()) {
            m_status->setText("请先提供歌词文字。");
            return;
        }
        m_page = 2;
        m_result->clear();
        if (m_keepTiming->isChecked() && !Lrc::parse(text).isEmpty()) {
            m_result->setPlainText(text);
            m_status->setText("使用原有 LRC 时间轴，请检查后应用。");
            updatePage();
            return;
        }
        m_operation = "align";
        m_status->setText("正在启动时间对齐…");
        startJob(alignmentConfig());
        updatePage();
    } else {
        if (Lrc::parse(m_result->toPlainText()).isEmpty()) {
            m_status->setText("尚未生成有效时间轴，请返回上一步重试。");
            return;
        }
        if (!m_library->setLyrics(m_track, m_result->toPlainText(), m_source)) {
            m_status->setText("歌曲已不存在或保存失败。");
            return;
        }
        m_library->setOffset(m_track, 0);
        accept();
    }
}
QJsonObject LyricWizard::videoConfig() const {
    const auto selection = m_preview->selection();
    const QJsonArray crop{selection.left(), selection.top(), selection.width(), selection.height()};
    return {{"video", m_video->text().trimmed()},
            {"crop", crop},
            {"preview_time", m_previewTime->value()},
            {"interval", m_interval->value()},
            {"ocr_language", findChild<QComboBox *>("lyricOcrLanguage")->currentData().toString()},
            {"ocr_space_probability", m_spaceProbability->value() / 100.},
            {"song_title", m_library->track(m_track).value("title").toString()},
            {"song_artist", m_library->track(m_track).value("artist").toString()},
            {"change_threshold", m_changeThreshold->value() / 100.},
            {"text_similarity", m_textSimilarity->value() / 100.}};
}
void LyricWizard::syncMargins() {
    const auto selection = m_preview->selection();
    const double values[]{selection.left() * 100., (1. - selection.right()) * 100., selection.top() * 100.,
                          (1. - selection.bottom()) * 100.};
    const auto minimum = m_preview->minimumSelection();
    const double limits[]{
        100. - values[1] - minimum.width() * 100., 100. - values[0] - minimum.width() * 100.,
        100. - values[3] - minimum.height() * 100., 100. - values[2] - minimum.height() * 100.};
    for (int i = 0; i < 4; ++i) {
        const QSignalBlocker guard(m_margins[i]);
        m_margins[i]->setMaximum(qMax(0., limits[i]));
        m_margins[i]->setValue(values[i]);
    }
}
QJsonObject LyricWizard::taskConfig() {
    const auto track = m_library->track(m_track);
    if (m_page == 0 && m_method->currentIndex() == 1) {
        auto config = videoConfig();
        const auto crop = config["crop"].toArray();
        if (m_video->text().trimmed().isEmpty() || crop[2].toDouble() <= 0 || crop[3].toDouble() <= 0 ||
            crop[0].toDouble() + crop[2].toDouble() > 1.000001 ||
            crop[1].toDouble() + crop[3].toDouble() > 1.000001) {
            m_status->setText("请选择同源视频，并设置有效的字幕截取范围。");
            return {};
        }
        config["operation"] = "ocr";
        return config;
    }
    if (m_page == 0 && m_method->currentIndex() == 2 && !m_candidates->currentItem()) {
        m_status->setText("请先搜索并选择歌词版本，再将时间匹配加入队列。");
        return {};
    }
    if (plainText(m_text->toPlainText()).isEmpty()) {
        m_page = 1;
        updatePage();
        m_status->setText("请先导入或粘贴歌词文字。");
        return {};
    }
    if (m_keepTiming->isChecked() && !Lrc::parse(m_text->toPlainText()).isEmpty())
        return {{"operation", "lrc"}, {"lrc", m_text->toPlainText()}, {"source", m_source}};
    if (track.value("storage").toString() == "url" || !QFileInfo::exists(track.value("path").toString())) {
        m_status->setText("已有歌词时间对齐需要本地音频，请先下载。URL 歌曲可识别同源视频字幕或导入 LRC。");
        return {};
    }
    return alignmentConfig();
}
QJsonObject LyricWizard::alignmentConfig() const {
    QJsonObject config{{"operation", "align"},
                       {"audio", m_library->track(m_track).value("path").toString()},
                       {"text", plainText(m_text->toPlainText())},
                       {"source", m_source},
                       {"language", m_language->currentData().toString()}};
    if (m_useTimingHints->isChecked() && m_useTimingHints->isEnabled())
        config["timed_text"] = m_text->toPlainText();
    return config;
}
void LyricWizard::startJob(QJsonObject config) {
    const auto operation = config["operation"].toString();
    if (m_queue && operation != "preview" && operation != "ocr_frame") {
        m_previewRefresh.stop();
        m_queue->enqueue(m_track, config);
        accept();
        return;
    }
    m_activeConfig = LyricJob::prepareConfig(m_settings, config);
    m_operation = config["operation"].toString();
    m_status->setText("正在启动任务…");
    m_job->start(m_activeConfig);
    updatePage();
}
void LyricWizard::runVideo(bool preview) {
    if (m_video->text().trimmed().isEmpty()) {
        m_status->clear();
        return;
    }
    auto config = videoConfig();
    if (!preview) {
        saveOcrSettings();
    }
    m_operation = preview ? "preview" : "ocr";
    config["operation"] = m_operation;
    m_status->setText(preview ? "准备范围预览…" : "开始提取视频区域文字…");
    startJob(config);
    updatePage();
}
void LyricWizard::saveOcrSettings() {
    m_settings->setValue("lyrics/ocrLanguage", findChild<QComboBox *>("lyricOcrLanguage")->currentData());
    m_settings->setValue("lyrics/changeThreshold", m_changeThreshold->value());
    m_settings->setValue("lyrics/textSimilarity", m_textSimilarity->value());
    m_settings->setValue("lyrics/ocrSpaceProbability", m_spaceProbability->value() / 100.);
}
void LyricWizard::invalidateFrameResult() {
    m_frameResult->clear();
    m_frameResult->setProperty("frameResult", QVariant());
}
void LyricWizard::recognizeCurrentFrame() {
    if (m_job->busy() || !m_preview->hasImage())
        return;
    m_previewRefresh.stop();
    m_previewPending = false;
    const auto frame = m_frameWork.filePath("current-frame.png");
    if (!m_frameWork.isValid() || !m_preview->image().save(frame)) {
        m_frameResult->setPlainText("无法保存当前预览帧");
        return;
    }
    saveOcrSettings();
    invalidateFrameResult();
    auto config = videoConfig();
    config["operation"] = "ocr_frame";
    config["frame_image"] = frame;
    startJob(config);
}
void LyricWizard::searchOnline() {
    if (m_title->text().trimmed().isEmpty()) {
        m_status->setText("请填写歌名。");
        return;
    }
    auto search = findChild<OnlineLyricsSearch *>("wizardOnlineSearch");
    if (!search) {
        search = new OnlineLyricsSearch(this);
        search->setObjectName("wizardOnlineSearch");
        connect(search, &OnlineLyricsSearch::progress, m_status, &QLabel::setText);
        connect(search, &OnlineLyricsSearch::finished, this,
                [this](const QList<QVariantMap> &options, const QString &error) {
                    m_searching = false;
                    updatePage();
                    for (const auto &c : options) {
                        auto item = new QListWidgetItem(
                            c.value("trackName").toString() + " / " + c.value("artistName").toString() +
                                QString(" · 匹配度 %1% · ").arg(c.value("score").toInt()) +
                                (c.value("syncedLyrics").toString().isEmpty() ? "纯文本" : "已有时间轴"),
                            m_candidates);
                        item->setData(Qt::UserRole, c);
                    }
                    if (m_candidates->count())
                        m_candidates->setCurrentRow(0);
                    m_status->setText(!error.isEmpty()        ? error
                                      : m_candidates->count() ? "请选择歌词版本。"
                                                              : "未找到歌词，请调整歌名或歌手。");
                });
    }
    m_searching = true;
    m_candidates->clear();
    updatePage();
    auto track = m_library->track(m_track);
    track["title"] = m_title->text().trimmed();
    track["artist"] = m_artist->text().trimmed();
    search->search(track);
}
