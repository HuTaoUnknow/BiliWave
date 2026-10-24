#pragma once
#include "library.h"
#include <QDialog>
#include <QImage>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QProcess>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>

class QComboBox;
class QDoubleSpinBox;
class QLineEdit;
class QLabel;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QListWidget;
class QCheckBox;
class QProgressBar;
class QShowEvent;

class LyricCropPreview : public QWidget {
    Q_OBJECT
  public:
    explicit LyricCropPreview(QWidget *parent = nullptr);
    void setImage(const QImage &image);
    void clear();
    bool hasImage() const {
        return !m_image.isNull();
    }
    const QImage &image() const {
        return m_image;
    }
    QRectF imageRect() const;
    QRectF selection() const {
        return m_selection;
    }
    QSizeF minimumSelection() const;
    void setSelection(const QRectF &selection);
  signals:
    void selectionChanged(const QRectF &selection);
    void imageChanged();

  protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void leaveEvent(QEvent *event) override;

  private:
    QRectF screenSelection() const;
    QPointF normalizedPoint(const QPointF &point) const;
    int hitTest(const QPointF &point) const;
    void updateCursor(const QPointF &point);
    QImage m_image;
    QRectF m_selection{0., 2. / 3, 1., 1. / 3}, m_startSelection;
    QPointF m_start;
    int m_drag = 0;
};

class LyricJob : public QObject {
    Q_OBJECT
  public:
    explicit LyricJob(QSettings *settings, QObject *parent = nullptr);
    ~LyricJob() override;
    bool start(QJsonObject config);
    static QJsonObject prepareConfig(QSettings *settings, QJsonObject config);
    bool busy() const {
        return m_process.state() != QProcess::NotRunning;
    }
    void cancel();
  signals:
    void progress(const QString &message, int percent);
    void finished(const QJsonObject &result);

  private:
    void complete();
    QSettings *m_settings;
    QTemporaryDir m_work;
    QProcess m_process;
    QTimer m_timeout;
    QByteArray m_output, m_errors;
    QString m_result, m_cancel;
    bool m_cancelled = false, m_delivered = false;
    void *m_jobHandle = nullptr;
};

void setLyricButtonBusy(QPushButton *button, bool busy);

class LyricQueue : public QWidget {
    Q_OBJECT
  public:
    LyricQueue(Library *library, QSettings *settings, QWidget *parent = nullptr);
    int enqueue(qint64 track, QJsonObject config, LyricJob *running = nullptr);
    bool running() const;
    bool activeFor(qint64 track) const;
    void cancelTask(int index);
    void cancelTrack(qint64 track);
    QJsonObject taskResult(int index) const;
    QString taskState(int index) const;
    int count() const {
        return int(m_tasks.size());
    }
  signals:
    void activity(qint64 track, const QString &operation, bool busy);
    void taskFinished(int index);

  private:
    struct Task {
        QString key;
        qint64 track;
        QString title, state, message;
        QJsonObject config, result;
        QPointer<LyricJob> job;
        int percent = -1;
    };
    void startNext();
    void attach(int index, LyricJob *job);
    void refresh();
    void review();
    int indexForJob(LyricJob *job) const;
    Library *m_library;
    QSettings *m_settings;
    QList<Task> m_tasks;
    QListWidget *m_list;
    QLabel *m_status, *m_runningStatus;
    QProgressBar *m_progress;
    QPushButton *m_cancelButton, *m_retryButton, *m_reviewButton;
};

class LyricWizard : public QDialog {
    Q_OBJECT
  public:
    LyricWizard(Library *library, QSettings *settings, qint64 track, const QString &initialText = {},
                QWidget *parent = nullptr);
    ~LyricWizard() override;
    static QString plainText(const QString &text);
    void setQueue(LyricQueue *queue) {
        m_queue = queue;
        updatePage();
    }

  protected:
    void showEvent(QShowEvent *event) override;

  private:
    void next();
    void updatePage();
    void runVideo(bool preview);
    void recognizeCurrentFrame();
    void invalidateFrameResult();
    void saveOcrSettings();
    void searchOnline();
    QJsonObject videoConfig() const;
    QJsonObject taskConfig();
    QJsonObject alignmentConfig() const;
    void startJob(QJsonObject config);
    void refreshPreview();
    void syncMargins();
    Library *m_library;
    QSettings *m_settings;
    qint64 m_track;
    LyricJob *m_job;
    QTemporaryDir m_frameWork;
    QPointer<LyricQueue> m_queue;
    QJsonObject m_activeConfig;
    QNetworkAccessManager m_network;
    QStackedWidget *m_pages, *m_sources;
    QComboBox *m_method, *m_language;
    QLineEdit *m_video, *m_title, *m_artist;
    QDoubleSpinBox *m_margins[4], *m_interval, *m_previewTime, *m_changeThreshold, *m_textSimilarity;
    QDoubleSpinBox *m_spaceProbability;
    QPlainTextEdit *m_text, *m_result;
    QPlainTextEdit *m_frameResult;
    QLabel *m_step, *m_status;
    LyricCropPreview *m_preview;
    QListWidget *m_candidates;
    QPushButton *m_back, *m_next, *m_search, *m_previewButton, *m_ocrButton;
    QPushButton *m_frameButton;
    QCheckBox *m_keepTiming, *m_useTimingHints;
    QProgressBar *m_progress;
    QString m_source = "导入 / 编辑歌词", m_operation;
    int m_page = 0;
    bool m_searching = false;
    QTimer m_previewRefresh;
    bool m_previewPending = false;
};
