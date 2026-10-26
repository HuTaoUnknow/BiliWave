#include "lrc.h"
#include "lyricworkflow.h"
#include "features.h"
#include "services.h"
#include "theme.h"
#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollArea>
#include <QScrollBar>
#include <QSignalSpy>
#include <QtTest>

class LyricWorkflowTests : public QObject {
    Q_OBJECT
  private slots:
    void allResultsHaveSourcesNumbersAndOptionalSimplification() {
        QTemporaryDir dir(QDir::current().filePath("lyric-results-test-XXXXXX"));
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        const auto id = library.addTrack({{"title", "繁體測試"}, {"path", "test.flac"}});
        LyricQueue queue(&library, &settings);
        const QString original = "[00:01.230]這是愛與夢想\n[00:03.000]Мой мир\n[00:03.000]夢裡的世界";
        const QVariantMap synced{{"id", 123},
                                 {"provider", "LRCLIB"},
                                 {"trackName", "繁體測試"},
                                 {"artistName", "歌手"},
                                 {"syncedLyrics", original},
                                 {"sourceUrl", "https://lrclib.net/api/get/123"}};
        queue.addOnlineResults(id, {synced, {{"id", 456}, {"plainLyrics", "這是純文字"}}});
        QCOMPARE(queue.count(), 2);
        QVERIFY(library.track(id).value("lrc").toString().isEmpty());
        QCOMPARE(queue.taskState(0), QString("待应用"));
        QCOMPARE(queue.taskState(1), QString("仅文本"));
        QVERIFY(queue.taskResult(0)["lrc"].toString().contains("这是爱与梦想"));
        QCOMPARE(queue.taskResult(0)["raw_lrc"].toString(), original);
        QCOMPARE(queue.taskResult(1)["text"].toString(), QString("这是纯文字"));
        auto list = queue.findChild<QListWidget *>("lyricQueueTasks");
        QCOMPARE(list->item(0)->data(Qt::UserRole + 1).toInt(), 1);
        QCOMPARE(list->item(0)->data(Qt::UserRole + 2).toString(), QString("LRCLIB #123"));
        QVERIFY(list->item(0)->toolTip().contains("https://lrclib.net/api/get/123"));
        list->setCurrentRow(1);
        QTimer::singleShot(20, &queue, [&] {
            auto dialog = queue.findChild<QDialog *>("queuedLyricReview");
            QVERIFY(dialog);
            QCOMPARE(dialog->findChild<QPlainTextEdit *>("queuedLyricResult")->toPlainText(),
                     QString("这是纯文字"));
            QVERIFY(!dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->isEnabled());
            dialog->reject();
        });
        queue.findChild<QPushButton *>("reviewQueuedLyrics")->click();
        list->setCurrentRow(0);
        QTimer::singleShot(20, &queue, [&] {
            auto dialog = queue.findChild<QDialog *>("queuedLyricReview");
            QVERIFY(dialog->findChild<QLabel *>("queuedLyricOrigin")->text().contains("记录 #123"));
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->click();
        });
        queue.findChild<QPushButton *>("reviewQueuedLyrics")->click();
        QCOMPARE(queue.taskState(0), QString("已应用"));
        QCOMPARE(library.workbenchTrack(id).value("raw_lrc").toString(), original);
        queue.markApplied(id, original, "LRCLIB · https://lrclib.net/api/get/123");
        QCOMPARE(queue.count(), 2); // Applying an existing candidate does not create a second result.
        auto job = new LyricJob(&settings);
        const int local = queue.enqueue(id, {{"operation", "ocr"}, {"video", "BV13x41117TL"}}, job);
        job->finished({{"ok", true}, {"lrc", "[00:01.000]歌聲響起"}, {"source", "视频字幕 OCR"}});
        QCOMPARE(queue.taskState(local), QString("待应用"));
        QVERIFY(queue.taskResult(local)["lrc"].toString().contains("歌声响起"));
        settings.setValue("lyrics/autoSimplifyChinese", false);
        library.setAutoSimplifyLyrics(false);
        queue.reloadTextSettings();
        QCOMPARE(queue.taskResult(0)["lrc"].toString(), original);
        QCOMPARE(library.workbenchTrack(id).value("lrc").toString(), original);
        queue.cancelTask(1);
        queue.addOnlineResults(id, {synced});
        QCOMPARE(queue.taskResult(2)["queue_number"].toInt(), 4);
        QCOMPARE(queue.taskResult(0)["queue_number"].toInt(), 1);
    }
    void importReviewAndApply() {
        QTemporaryDir dir(QDir::current().filePath("lyrics-test-XXXXXX"));
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        const auto id = library.addTrack({{"title", "原文测试"}, {"path", "test.flac"}});
        library.setLyrics(id, "[00:00.10]旧歌词", "旧来源");
        library.setOffset(id, 500);
        const QString text = "[ti:歌曲]\n[00:01.00]123\n[00:03.00]副歌，重复！\n[00:05.00]副歌，重复！";
        QCOMPARE(LyricWizard::plainText(text), QString("123\n副歌，重复！\n副歌，重复！"));
        {
            LyricWizard wizard(&library, &settings, id, text);
            wizard.show();
            auto keep = wizard.findChild<QCheckBox *>("keepExistingTiming");
            QVERIFY(keep->isEnabled());
            keep->setChecked(true);
            auto next = wizard.findChild<QPushButton *>("lyricWizardNext");
            next->click();
            QCOMPARE(library.track(id).value("lrc").toString(), QString("[00:00.10]旧歌词"));
            QVERIFY(wizard.findChild<QPlainTextEdit *>("alignedLyricResult")->toPlainText().contains("123"));
            next->click();
            QCOMPARE(wizard.result(), int(QDialog::Accepted));
            QCOMPARE(library.workbenchTrack(id).value("lrc").toString(), text);
            QCOMPARE(library.workbenchTrack(id).value("lyric_offset").toInt(), 0);
            QCOMPARE(library.track(id).value("lrc").toString(), QString("[00:00.10]旧歌词"));
        }
        {
            LyricWizard wizard(&library, &settings, id, "不要保存的编辑");
            wizard.reject();
            QCOMPARE(library.workbenchTrack(id).value("lrc").toString(), text);
        }
    }
    void videoRangeAndScreenshots() {
        QTemporaryDir dir(QDir::current().filePath("lyrics-test-XXXXXX"));
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        const auto id = library.addTrack({{"title", "视频歌词范围"}, {"path", "test.flac"}});
        settings.setValue("lyrics/textSimilarity", 94);
        LyricWizard wizard(&library, &settings, id);
        QCOMPARE(wizard.findChild<QDoubleSpinBox *>("lyricTextSimilarity")->value(), 60.);
        QCOMPARE(settings.value("lyrics/textSimilarity").toInt(), 60);
        wizard.findChild<QComboBox *>("lyricMethod")->setCurrentIndex(1);
        auto top = wizard.findChild<QDoubleSpinBox *>("lyricMarginTop");
        auto height = wizard.findChild<QDoubleSpinBox *>("lyricMarginBottom");
        QVERIFY(qAbs(top->value() - 66.6667) < .0001);
        QVERIFY(qAbs(height->value()) < .0001);
        wizard.show();
        QTest::qWait(100);
        QDir screenshots(QDir(resourceRoot()).filePath("docs/screenshots"));
        screenshots.mkpath(".");
        QVERIFY(wizard.grab().save(screenshots.filePath("lyric-video-workflow.png")));
        wizard.findChild<QComboBox *>("lyricMethod")->setCurrentIndex(0);
        wizard.findChild<QPushButton *>("lyricWizardNext")->click();
        wizard.findChild<QPlainTextEdit *>("lyricTextEditor")
            ->setPlainText("听见微光，落在窗前\n让下一句，轻轻唱起");
        QVERIFY(wizard.grab().save(screenshots.filePath("lyric-text-review.png")));
    }
    void realAlignment() {
        if (!alignmentFeaturesEnabled())
            QSKIP("Current editions do not ship audio alignment");
        const auto audio =
            QDir(resourceRoot()).filePath(".cache/whisper-source/whisper.cpp-1.9.5/samples/jfk.wav");
        if (!QFile::exists(audio) || !QFile::exists(resourceRoot() + "/models/small.pt"))
            QSKIP("Integration fixture / alignment model unavailable");
        QTemporaryDir dir(QDir::current().filePath("lyrics-test-XXXXXX"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        LyricJob job(&settings);
        QSignalSpy done(&job, &LyricJob::finished);
        const QString text = "And so, my fellow Americans,\nask not what your country can do for you,\nask "
                             "what you can do for your country.";
        QVERIFY(job.start({{"operation", "align"}, {"audio", audio}, {"text", text}, {"language", "en"}}));
        QVERIFY(done.wait(120000));
        const auto result = done.first().first().toJsonObject();
        QVERIFY2(result["ok"].toBool(), qPrintable(result["error"].toString()));
        QCOMPARE(LyricWizard::plainText(result["lrc"].toString()), text);
        QCOMPARE(result["lines"].toArray().size(), 3);
        QVERIFY(result["lines"].toArray()[1].toObject()["start"].toDouble() > 2);
        QVERIFY(!job.busy());
    }
    void sameVersionCaptionHintsUseAudioWithoutSilentlyApplying() {
        if (!alignmentFeaturesEnabled())
            QSKIP("Current editions do not ship audio alignment");
        const auto audio =
            QDir(resourceRoot()).filePath(".cache/whisper-source/whisper.cpp-1.9.5/samples/jfk.wav");
        if (!QFile::exists(audio) || !QFile::exists(resourceRoot() + "/models/small.pt"))
            QSKIP("Integration fixture / alignment model unavailable");
        QTemporaryDir dir(QDir::current().filePath("lyrics-hints-test-XXXXXX"));
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        const auto id = library.addTrack({{"title", "同源字幕校准"}, {"path", audio}});
        library.setLyrics(id, "[00:00.10]原歌词", "旧来源");
        const QString text = "[00:00.32]And so, my fellow Americans,\n"
                             "[00:03.66]ask not what your country can do for you,\n"
                             "[00:08.34]ask what you can do for your country.";
        LyricQueue queue(&library, &settings);
        QSignalSpy finished(&queue, &LyricQueue::taskFinished);
        LyricWizard wizard(&library, &settings, id, text);
        wizard.setQueue(&queue);
        wizard.show();
        auto hints = wizard.findChild<QCheckBox *>("useExistingTimingHints");
        QVERIFY(hints->isEnabled());
        QVERIFY(!hints->isChecked());
        hints->setChecked(true);
        wizard.findChild<QComboBox *>("lyricAlignLanguage")->setCurrentIndex(2);
        wizard.findChild<QPushButton *>("lyricWizardNext")->click();
        QCOMPARE(wizard.result(), int(QDialog::Accepted));
        QCOMPARE(queue.count(), 1);
        QVERIFY(finished.wait(120000));
        const auto result = queue.taskResult(0);
        QVERIFY2(result["ok"].toBool(), qPrintable(result["error"].toString()));
        QCOMPARE(result["timing_kind"].toString(), QString("audio_with_caption_hints"));
        QCOMPARE(result["lines"].toArray().size(), 3);
        QCOMPARE(LyricWizard::plainText(result["lrc"].toString()), LyricWizard::plainText(text));
        QCOMPARE(library.track(id).value("lrc").toString(), QString("[00:00.10]原歌词"));
        QVERIFY(result["lines"].toArray()[1].toObject()["start"].toDouble() > 2);
    }
    void captionReviewCanOpenAudioCalibrationWithoutApplying() {
        if (!alignmentFeaturesEnabled())
            QSKIP("Current editions do not ship audio alignment");
        QTemporaryDir dir(QDir::current().filePath("lyrics-caption-review-XXXXXX"));
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        QFile audio(dir.filePath("source.wav"));
        QVERIFY(audio.open(QIODevice::WriteOnly));
        audio.close();
        const auto id = library.addTrack({{"title", "字幕校准入口"}, {"path", audio.fileName()}});
        LyricQueue queue(&library, &settings);
        auto job = new LyricJob(&settings);
        queue.enqueue(id, {{"operation", "ocr"}}, job);
        QJsonArray warnings;
        for (int i = 0; i < 16; ++i)
            warnings.append(QString("第 %1 行需要检查，当前为字幕出现时间，尚未按音频校准。").arg(i + 1));
        job->finished({{"ok", true},
                       {"timing_kind", "video_caption"},
                       {"lrc", "[00:01.00]Hello world\n[00:05.00]Another day"},
                       {"warnings", warnings}});
        queue.show();
        bool offered = false, prefilled = false, screenshot = false;
        QTimer::singleShot(100, &queue, [&] {
            auto dialog = queue.findChild<QDialog *>("queuedLyricReview");
            if (!dialog)
                return;
            auto calibrate = dialog->findChild<QPushButton *>("calibrateCaptionTiming");
            offered = calibrate;
            QDir shots(QDir(resourceRoot()).filePath("docs/screenshots"));
            shots.mkpath(".");
            screenshot = dialog->grab().save(shots.filePath("caption-audio-calibration-review.png"));
            QTimer::singleShot(100, &queue, [&] {
                auto wizard = queue.findChild<LyricWizard *>("lyricWizard");
                if (!wizard)
                    return;
                auto hints = wizard->findChild<QCheckBox *>("useExistingTimingHints");
                prefilled = hints->isEnabled() && hints->isChecked() &&
                            wizard->findChild<QPlainTextEdit *>("lyricTextEditor")
                                ->toPlainText()
                                .contains("[00:01.00]");
                wizard->reject();
            });
            if (calibrate)
                calibrate->click();
            dialog->reject();
        });
        QTimer::singleShot(1500, &queue, [&] {
            for (auto dialog : queue.findChildren<QDialog *>())
                dialog->reject();
        });
        queue.findChild<QPushButton *>("reviewQueuedLyrics")->click();
        QVERIFY(offered);
        QVERIFY(prefilled);
        QVERIFY(screenshot);
        QCOMPARE(queue.count(), 1);
        QVERIFY(library.track(id).value("lrc").toString().isEmpty());
    }
    void interactiveCropAndMarginsStayInSync() {
        QTemporaryDir dir(QDir::current().filePath("lyrics-crop-test-XXXXXX"));
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        const auto id = library.addTrack({{"title", "拖动范围"}, {"path", "test.flac"}});
        LyricWizard wizard(&library, &settings, id);
        auto preview = wizard.findChild<LyricCropPreview *>("lyricCropPreview");
        QImage image(1000, 500, QImage::Format_RGB32);
        image.fill(QColor("#555555"));
        preview->setImage(image);
        wizard.show();
        QTest::qWait(50);
        auto left = wizard.findChild<QDoubleSpinBox *>("lyricMarginLeft");
        auto right = wizard.findChild<QDoubleSpinBox *>("lyricMarginRight");
        auto top = wizard.findChild<QDoubleSpinBox *>("lyricMarginTop");
        auto bottom = wizard.findChild<QDoubleSpinBox *>("lyricMarginBottom");
        left->setValue(20);
        right->setValue(20);
        top->setValue(60);
        bottom->setValue(10);
        QVERIFY(qAbs(preview->selection().left() - .2) < .00001);
        QVERIFY(qAbs(preview->selection().width() - .6) < .00001);
        QVERIFY(qAbs(preview->selection().height() - .3) < .00001);
        auto at = [preview](double x, double y) {
            const auto area = preview->imageRect();
            return QPoint(qRound(area.left() + x * area.width()), qRound(area.top() + y * area.height()));
        };
        QTest::mousePress(preview, Qt::LeftButton, Qt::NoModifier, at(.5, .75));
        QTest::mouseMove(preview, at(.4, .65));
        QTest::mouseRelease(preview, Qt::LeftButton, Qt::NoModifier, at(.4, .65));
        QVERIFY(qAbs(left->value() - 10) < .5);
        QVERIFY(qAbs(top->value() - 50) < .5);
        QVERIFY(qAbs(right->value() - 30) < .5);
        QVERIFY(qAbs(bottom->value() - 20) < .5);
        // Resize the right edge, then create an entirely new box in the unselected area.
        const auto selection = preview->selection();
        QTest::mousePress(preview, Qt::LeftButton, Qt::NoModifier,
                          at(selection.right(), selection.center().y()));
        QTest::mouseMove(preview, at(.9, selection.center().y()));
        QTest::mouseRelease(preview, Qt::LeftButton, Qt::NoModifier, at(.9, selection.center().y()));
        QVERIFY(qAbs(right->value() - 10) < .5);
        QTest::mousePress(preview, Qt::LeftButton, Qt::NoModifier, at(.15, .1));
        QTest::mouseMove(preview, at(.85, .4));
        QTest::mouseRelease(preview, Qt::LeftButton, Qt::NoModifier, at(.85, .4));
        QVERIFY(qAbs(left->value() - 15) < .5);
        QVERIFY(qAbs(right->value() - 15) < .5);
        QVERIFY(qAbs(top->value() - 10) < .5);
        QVERIFY(qAbs(bottom->value() - 60) < .5);
        const auto normalized = preview->selection();
        wizard.resize(1000, 800);
        QCoreApplication::processEvents();
        QCOMPARE(preview->selection(), normalized);
        left->setValue(100);
        QVERIFY(preview->selection().width() >= preview->minimumSelection().width() - .000001);
        QVERIFY(preview->selection().right() <= 1.);
        for (auto text : wizard.findChildren<QLabel *>())
            QVERIFY(!text->text().contains("步骤 1") && !text->text().contains("请选择视频"));
        QDir screenshots(resourceRoot() + "/docs/screenshots");
        QVERIFY(wizard.grab().save(screenshots.filePath("interactive-subtitle-crop.png")));
    }
    void currentFrameCalibrationStaysInDialogAndUsesSnapshot() {
        const auto fixture = resourceRoot() + "/.cache/lyric-validation/ocr-fixture.mp4";
        QVERIFY(QFile::exists(fixture));
        QTemporaryDir dir(QDir::current().filePath("lyrics-single-frame-XXXXXX"));
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        settings.setValue("lyrics/ocrDevice", "cpu");
        const auto frame = dir.filePath("frame.png");
        QCOMPARE(QProcess::execute(toolPath(&settings, "ffmpeg"),
                                   {"-v", "error", "-i", fixture, "-frames:v", "1", "-y", frame}),
                 0);
        const auto id = library.addTrack({{"title", "单帧校验"}, {"path", "test.flac"}});
        library.setLyrics(id, "[00:01.00]原歌词", "原来源");
        library.setOffset(id, 250);
        LyricQueue queue(&library, &settings);
        LyricWizard wizard(&library, &settings, id);
        wizard.setQueue(&queue);
        // No source is provided: testing must use the existing captured frame.
        wizard.findChild<QLineEdit *>("lyricVideoInput")->clear();
        wizard.show();
        QTest::qWait(40);
        auto preview = wizard.findChild<LyricCropPreview *>("lyricCropPreview");
        auto button = wizard.findChild<QPushButton *>("recognizeCurrentLyricFrame");
        auto result = wizard.findChild<QPlainTextEdit *>("lyricFrameResult");
        auto probability = wizard.findChild<QDoubleSpinBox *>("lyricOcrGapSensitivity");
        QVERIFY(probability);
        QVERIFY(!wizard.findChild<QDoubleSpinBox *>("lyricOcrSpaceWeight"));
        QVERIFY(!wizard.findChild<QDoubleSpinBox *>("lyricOcrSpaceMinimum"));
        QVERIFY(!button->isEnabled());
        QCOMPARE(probability->value(), 50.);
        preview->setImage(QImage(frame));
        QVERIFY(button->isEnabled());
        probability->setValue(65.);
        QSignalSpy frameFinished(wizard.findChild<LyricJob *>(), &LyricJob::finished);
        button->click();
        QVERIFY(!button->isEnabled());
        QVERIFY(!probability->isEnabled());
        QVERIFY(wizard.isVisible());
        QCOMPARE(queue.count(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(frameFinished.size(), 1, 30000);
        const auto workerResult = frameFinished.first().first().toJsonObject();
        QVERIFY2(workerResult["ok"].toBool(), qPrintable(workerResult["error"].toString()));
        QCOMPARE(result->property("frameResult").toMap().value("scope").toString(), QString("current_frame"));
        QVERIFY2(result->toPlainText().contains("听见微光"), qPrintable(result->toPlainText()));
        QVERIFY(!result->toPlainText().contains("TOP"));
        QVERIFY(!result->toPlainText().contains("落在窗前"));
        const auto output = QJsonObject::fromVariantMap(result->property("frameResult").toMap());
        QCOMPARE(output["stats"].toObject()["ocr_calls"].toInt(), 1);
        QCOMPARE(output["stats"].toObject()["sampled_frames"].toInt(), 1);
        QCOMPARE(output["backend"].toObject()["spacing"].toObject()["sensitivity"].toDouble(), .65);
        QVERIFY(!output.contains("lrc"));
        QCOMPARE(queue.count(), 0);
        QVERIFY(wizard.isVisible());
        QCOMPARE(library.track(id).value("lrc").toString(), QString("[00:01.00]原歌词"));
        QCOMPARE(library.track(id).value("lyric_offset").toInt(), 250);
        QVERIFY(button->isEnabled());
        auto scroll = wizard.findChild<QScrollArea *>();
        scroll->verticalScrollBar()->setValue(scroll->verticalScrollBar()->maximum());
        QTest::qWait(30);
        QVERIFY(button->mapTo(&wizard, QPoint()).y() > probability->mapTo(&wizard, QPoint()).y());
        QVERIFY(button->mapTo(&wizard, QPoint()).x() > result->mapTo(&wizard, QPoint()).x());
        QDir screenshots(resourceRoot() + "/docs/screenshots");
        screenshots.mkpath(".");
        for (const auto &theme : {QString("dark"), QString("pink")}) {
            UiTheme::apply(&wizard, theme);
            QTest::qWait(30);
            QCOMPARE(scroll->horizontalScrollBar()->maximum(), 0);
            QVERIFY(wizard.grab().save(screenshots.filePath("subtitle-single-frame-" + theme + ".png")));
        }
        probability->setValue(70.);
        QVERIFY(result->toPlainText().isEmpty());
        QCOMPARE(settings.value("lyrics/ocrGapSensitivity").toDouble(), .7);
        // A new crop invalidates the old result; blank content is a valid check.
        preview->setSelection({0., .45, 1., .1});
        button->click();
        QTRY_COMPARE_WITH_TIMEOUT(result->property("frameResult").toMap().value("scope").toString(),
                                  QString("current_frame"), 30000);
        QCOMPARE(result->toPlainText(), QString("当前框选区域未识别到文字"));
        QCOMPARE(queue.count(), 0);
        QCOMPARE(library.track(id).value("lrc").toString(), QString("[00:01.00]原歌词"));
        const auto russianFrame = resourceRoot() + "/.cache/marmalade-merge-review/spacing-preview.png";
        if (QFile::exists(russianFrame)) {
            auto language = wizard.findChild<QComboBox *>("lyricOcrLanguage");
            language->setCurrentIndex(language->findData("ru_zh"));
            preview->setImage(QImage(russianFrame));
            preview->setSelection({0., 0., 1., 1.});
            probability->setValue(50.);
            button->click();
            QTRY_COMPARE_WITH_TIMEOUT(frameFinished.size(), 3, 30000);
            const auto russianResult = frameFinished.last().first().toJsonObject();
            QVERIFY2(russianResult["ok"].toBool(), qPrintable(russianResult["error"].toString()));
            QCOMPARE(result->toPlainText().split('\n').size(), 2);
            QVERIFY(result->toPlainText().contains("позвонила"));
            QVERIFY(result->toPlainText().contains("没有"));
            for (const auto &theme : {QString("dark"), QString("pink")}) {
                UiTheme::apply(&wizard, theme);
                QTest::qWait(30);
                QVERIFY(wizard.grab().save(
                    screenshots.filePath("subtitle-single-frame-russian-" + theme + ".png")));
            }
            probability->setValue(70.);
        }
        wizard.close();
        LyricWizard restored(&library, &settings, id);
        QCOMPARE(restored.findChild<QDoubleSpinBox *>("lyricOcrGapSensitivity")->value(), 70.);
    }
    void geometricSensitivityHasOwnDefaultAndSubmittedTasksRemainCompatible() {
        QTemporaryDir dir(QDir::current().filePath("lyrics-space-migration-XXXXXX"));
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        settings.setValue("lyrics/ocrSpaceWeight", 5.);
        settings.setValue("lyrics/ocrSpaceMinProbability", .3);
        settings.setValue("lyrics/ocrSpaceProbability", 1.);
        const auto id = library.addTrack({{"title", "空格参数迁移"}, {"path", "test.flac"}});
        LyricWizard wizard(&library, &settings, id);
        auto probability = wizard.findChild<QDoubleSpinBox *>("lyricOcrGapSensitivity");
        QCOMPARE(probability->value(), 50.);
        auto defaults = LyricJob::prepareConfig(&settings, {{"operation", "ocr"}});
        QCOMPARE(defaults["ocr_gap_sensitivity"].toDouble(), .5);
        QVERIFY(!defaults.contains("ocr_space_probability"));
        QVERIFY(!defaults.contains("ocr_space_weight"));
        QVERIFY(!defaults.contains("ocr_space_min_probability"));
        auto legacy = LyricJob::prepareConfig(
            &settings, {{"operation", "ocr"}, {"ocr_space_weight", 2.}, {"ocr_space_min_probability", .03}});
        QVERIFY(!legacy.contains("ocr_space_probability"));
        QVERIFY(!legacy.contains("ocr_gap_sensitivity"));
        QCOMPARE(legacy["ocr_space_weight"].toDouble(), 2.);
        QCOMPARE(legacy["ocr_space_min_probability"].toDouble(), .03);
        const auto previous =
            LyricJob::prepareConfig(&settings, {{"operation", "ocr"}, {"ocr_space_probability", 1.}});
        QVERIFY(!previous.contains("ocr_gap_sensitivity"));
        QCOMPARE(previous["ocr_space_probability"].toDouble(), 1.);
        probability->setValue(20.);
        QCOMPARE(defaults["ocr_gap_sensitivity"].toDouble(), .5);
        LyricWizard restored(&library, &settings, id);
        QCOMPARE(restored.findChild<QDoubleSpinBox *>("lyricOcrGapSensitivity")->value(), 20.);
    }
    void videoOcrPipeline() {
        const auto fixture = resourceRoot() + "/.cache/lyric-validation/ocr-fixture.mp4";
        if (!QFile::exists(fixture))
            QSKIP("Controlled video fixture unavailable");
        QTemporaryDir dir(QDir::current().filePath("lyrics-test-XXXXXX"));
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        const auto id = library.addTrack({{"title", "中文区域测试"}, {"path", "test.flac"}});
        LyricWizard wizard(&library, &settings, id);
        wizard.findChild<QComboBox *>("lyricMethod")->setCurrentIndex(1);
        wizard.findChild<QLineEdit *>("lyricVideoInput")->setText(fixture);
        wizard.show();
        auto preview = wizard.findChild<LyricCropPreview *>("lyricCropPreview");
        QTRY_VERIFY_WITH_TIMEOUT(preview->hasImage(), 20000);
        auto job = wizard.findChild<LyricJob *>();
        QSignalSpy previewEvents(job, &LyricJob::progress);
        wizard.findChild<QDoubleSpinBox *>("lyricMarginLeft")->setValue(5);
        QCOMPARE(preview->selection().left(), .05);
        QTest::qWait(450);
        QVERIFY(!job->busy());
        QCOMPARE(previewEvents.size(), 0);
        wizard.findChild<QDoubleSpinBox *>("lyricMarginLeft")->setValue(0);
        const auto firstPreview = preview->image();
        auto advance = wizard.findChild<QPushButton *>("previewLyricCrop");
        QCOMPARE(advance->text(), QString("向后 15 s →"));
        advance->click();
        // Six-second fixture wraps 15 seconds to 3 seconds, which shows the second caption.
        QTRY_COMPARE_WITH_TIMEOUT(wizard.findChild<QDoubleSpinBox *>("lyricPreviewTime")->value(), 3., 20000);
        QVERIFY(firstPreview != preview->image());
        QDir screenshots(resourceRoot() + "/docs/screenshots");
        QVERIFY(wizard.grab().save(screenshots.filePath("lyric-video-preview.png")));
        wizard.findChild<QPushButton *>("extractVideoLyrics")->click();
        auto editor = wizard.findChild<QPlainTextEdit *>("lyricTextEditor");
        QTRY_VERIFY_WITH_TIMEOUT(editor->toPlainText().contains("落在窗前"), 30000);
        QCOMPARE(editor->toPlainText(), QString("听见微光\n落在窗前"));
        QVERIFY(!editor->toPlainText().contains("TOP"));
        const auto lrc = wizard.findChild<QPlainTextEdit *>("alignedLyricResult")->toPlainText();
        QVERIFY(!lrc.contains("[00:02.00]"));
        QVERIFY(lrc.contains("[00:03.00]落在窗前"));
        QVERIFY(!lrc.contains("[00:05.00]"));
        QVERIFY(library.track(id).value("lrc").toString().isEmpty());
        QVERIFY(wizard.grab().save(screenshots.filePath("lyric-video-text-review.png")));
    }
    void queuedTasksAreIndependentAndReviewed() {
        QTemporaryDir dir(QDir::current().filePath("lyrics-queue-test-XXXXXX"));
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        const auto one = library.addTrack({{"title", "第一首"}, {"path", "one.flac"}});
        const auto two = library.addTrack({{"title", "第二首"}, {"path", "two.flac"}});
        library.setLyrics(one, "[00:00.00]原歌词", "原来源");
        LyricQueue queue(&library, &settings);
        auto progressBar = queue.findChild<QProgressBar *>("lyricQueueProgress");
        QVERIFY(progressBar);
        QVERIFY(!progressBar->isHidden());
        const int a =
            queue.enqueue(one, {{"operation", "lrc"}, {"lrc", "[00:01.00]第一首新歌词"}, {"source", "测试"}});
        const int b = queue.enqueue(two, {{"operation", "lrc"}, {"lrc", "[00:02.00]不应写入"}});
        int failure = queue.enqueue(two, {{"operation", "invalid-operation"}});
        int c =
            queue.enqueue(two, {{"operation", "lrc"}, {"lrc", "[00:03.00]第二首新歌词"}, {"source", "测试"}});
        queue.cancelTask(b);
        // Tool configuration is captured on submission, not on a later start.
        settings.setValue("tools/lyric-python", "unavailable-python");
        QCOMPARE(queue.count(), 3);
        --failure;
        --c;
        QTRY_COMPARE_WITH_TIMEOUT(queue.taskState(a), QString("待应用"), 15000);
        QTRY_COMPARE_WITH_TIMEOUT(queue.taskState(failure), QString("失败"), 15000);
        QTRY_COMPARE_WITH_TIMEOUT(queue.taskState(c), QString("待应用"), 15000);
        QCOMPARE(queue.taskResult(c)["lrc"].toString(), QString("[00:03.00]第二首新歌词"));
        QCOMPARE(library.track(one).value("lrc").toString(), QString("[00:00.00]原歌词"));
        QVERIFY(library.track(two).value("lrc").toString().isEmpty());
        queue.findChild<QListWidget *>("lyricQueueTasks")->setCurrentRow(a);
        QTimer::singleShot(50, &queue, [&] {
            auto dialog = queue.findChild<QDialog *>("queuedLyricReview");
            QVERIFY(dialog);
            dialog->findChild<QPlainTextEdit *>("queuedLyricResult")->setPlainText("[00:01.00]检查后的歌词");
            dialog->findChild<QDialogButtonBox *>()->button(QDialogButtonBox::Apply)->click();
        });
        queue.findChild<QPushButton *>("reviewQueuedLyrics")->click();
        QCOMPARE(queue.taskState(a), QString("已应用"));
        QCOMPARE(library.workbenchTrack(one).value("lrc").toString(), QString("[00:01.00]检查后的歌词"));
        QCOMPARE(library.track(one).value("lrc").toString(), QString("[00:00.00]原歌词"));
        QVERIFY(library.track(two).value("lrc").toString().isEmpty());
    }
    void russianAndItalianSubtitleLanguagesReachQueuedWorker() {
        const auto fixtureRoot = resourceRoot() + "/.cache/language-validation/";
        if (!QFile::exists(fixtureRoot + "ru-subtitles.mp4") ||
            !QFile::exists(fixtureRoot + "it-subtitles.mp4"))
            QSKIP("Language subtitle fixtures unavailable");
        QTemporaryDir dir(QDir::current().filePath("lyrics-language-test-XXXXXX"));
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        LyricQueue queue(&library, &settings);
        const QStringList languages{"ru", "it"};
        for (int index = 0; index < languages.size(); ++index) {
            const auto language = languages[index];
            const auto id = library.addTrack({{"title", language}, {"path", language + ".flac"}});
            LyricWizard wizard(&library, &settings, id);
            wizard.setQueue(&queue);
            auto ocr = wizard.findChild<QComboBox *>("lyricOcrLanguage");
            auto align = wizard.findChild<QComboBox *>("lyricAlignLanguage");
            QVERIFY(align->findData(language) >= 0);
            ocr->setCurrentIndex(ocr->findData(language));
            wizard.findChild<QLineEdit *>("lyricVideoInput")
                ->setText(fixtureRoot + language + "-subtitles.mp4");
            wizard.findChild<QPushButton *>("extractVideoLyrics")->click();
            QCOMPARE(wizard.result(), int(QDialog::Accepted));
            QTRY_COMPARE_WITH_TIMEOUT(queue.taskState(index), QString("待应用"), 30000);
            const auto result = queue.taskResult(index);
            QVERIFY2(result["ok"].toBool(), qPrintable(result["error"].toString()));
            QCOMPARE(result["backend"].toObject()["ocr_language"].toString(), language);
            QCOMPARE(result["events"].toArray().size(), 2);
            QCOMPARE(result["stats"].toObject()["ocr_calls"].toInt(), 2);
            QCOMPARE(settings.value("lyrics/ocrLanguage").toString(), language);
            QVERIFY(library.track(id).value("lrc").toString().isEmpty());
        }
    }
    void subtitleSubmissionReturnsToWorkbench() {
        const auto fixture = resourceRoot() + "/.cache/lyric-validation/ocr-fixture.mp4";
        if (!QFile::exists(fixture))
            QSKIP("Controlled video fixture unavailable");
        QTemporaryDir dir(QDir::current().filePath("lyrics-transfer-test-XXXXXX"));
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        const auto id = library.addUrl(
            {{"title", "URL 字幕任务"}, {"url", "https://www.bilibili.com/video/BV13x41117TL?p=2"}});
        LyricQueue queue(&library, &settings);
        {
            LyricWizard wizard(&library, &settings, id);
            wizard.setQueue(&queue);
            QCOMPARE(wizard.findChild<QComboBox *>("lyricMethod")->currentIndex(), 1);
            wizard.findChild<QLineEdit *>("lyricVideoInput")->setText(fixture);
            wizard.show();
            QTRY_VERIFY_WITH_TIMEOUT(wizard.findChild<LyricCropPreview *>("lyricCropPreview")->hasImage(),
                                     20000);
            wizard.findChild<QPushButton *>("extractVideoLyrics")->click();
            QCOMPARE(wizard.result(), int(QDialog::Accepted));
            QVERIFY(!wizard.isVisible());
            QCOMPARE(queue.count(), 1);
            QVERIFY(queue.running());
        }
        QTRY_COMPARE_WITH_TIMEOUT(queue.taskState(0), QString("待应用"), 30000);
        const auto result = queue.taskResult(0);
        QVERIFY2(result["ok"].toBool(), qPrintable(result["error"].toString()));
        QCOMPARE(result["text"].toString(), QString("听见微光\n落在窗前"));
        QCOMPARE(result["events"].toArray().at(0).toObject()["end"].toDouble(), 3.);
        QCOMPARE(result["stats"].toObject()["sampled_frames"].toInt(), 12);
        QCOMPARE(result["stats"].toObject()["ocr_calls"].toInt(), 2);
        QCOMPARE(result["events"].toArray().at(0).toObject()["representative_time"].toDouble(), 1.);
        QCOMPARE(result["events"].toArray().at(1).toObject()["representative_time"].toDouble(), 4.);
        QVERIFY(!result["backend"].toObject()["provider"].toString().isEmpty());
        QVERIFY(library.track(id).value("lrc").toString().isEmpty());
    }
    void queueProgressFollowsRunningTask() {
        QTemporaryDir dir(QDir::current().filePath("lyrics-progress-test-XXXXXX"));
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        const auto one = library.addTrack({{"title", "正在处理"}, {"path", "one.flac"}});
        const auto two = library.addTrack({{"title", "等待处理"}, {"path", "two.flac"}});
        LyricQueue queue(&library, &settings);
        auto activeJob = new LyricJob(&settings);
        queue.enqueue(one, {{"operation", "ocr"}}, activeJob);
        activeJob->progress("识别字幕", 37);
        auto bar = queue.findChild<QProgressBar *>("lyricQueueProgress");
        QCOMPARE(bar->value(), 37);
        QCOMPARE(bar->property("trackId").toLongLong(), one);
        const auto waiting = queue.enqueue(two, {{"operation", "lrc"}, {"lrc", "[00:01.00]歌词"}});
        QCOMPARE(queue.findChild<QListWidget *>("lyricQueueTasks")->currentRow(), waiting);
        QCOMPARE(bar->value(), 37);
        queue.cancelTask(waiting);
        QCOMPARE(bar->value(), 37);
        activeJob->finished({{"ok", true}, {"lrc", "[00:01.00]完成"}, {"source", "测试"}});
        queue.findChild<QListWidget *>("lyricQueueTasks")->setCurrentRow(0);
        QCOMPARE(bar->value(), 100);
        QVERIFY(library.track(one).value("lrc").toString().isEmpty());
    }
    void otherMethodsDoNotReadVideo() {
        QTemporaryDir dir(QDir::current().filePath("lyrics-lazy-test-XXXXXX"));
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        const auto id = library.addUrl(
            {{"title", "延迟读取视频"}, {"url", "https://www.bilibili.com/video/BV13x41117TL?p=2"}});
        LyricWizard wizard(&library, &settings, id);
        wizard.findChild<QComboBox *>("lyricMethod")->setCurrentIndex(0);
        auto job = wizard.findChild<LyricJob *>();
        QSignalSpy progress(job, &LyricJob::progress);
        wizard.show();
        QTest::qWait(600);
        QVERIFY(!job->busy());
        QCOMPARE(progress.size(), 0);
        QCOMPARE(wizard.findChild<QComboBox *>("lyricMethod")->count(), 3);
        wizard.findChild<QComboBox *>("lyricMethod")->setCurrentIndex(2);
        QTest::qWait(100);
        QVERIFY(!job->busy());
        QCOMPARE(progress.size(), 0);
    }
    void audioAnalysisIsUnavailable() {
        QTemporaryDir dir(QDir::current().filePath("lyrics-no-analysis-XXXXXX"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        LyricJob job(&settings);
        QSignalSpy done(&job, &LyricJob::finished);
        QVERIFY(job.start({{"operation", "analyze"}}));
        QVERIFY(done.wait(10000));
        QVERIFY(!done.first().first().toJsonObject()["ok"].toBool());
    }
    void deletingQueuedTasksKeepsRunningTaskCallbacksCorrect() {
        QTemporaryDir dir(QDir::current().filePath("lyrics-remove-XXXXXX"));
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        const auto id = library.addTrack({{"title", "任务"}, {"path", "tone.flac"}});
        LyricQueue queue(&library, &settings);
        queue.show();
        auto first = new LyricJob(&settings);
        auto second = new LyricJob(&settings);
        queue.enqueue(id, {{"operation", "ocr"}}, first);
        queue.enqueue(id, {{"operation", "ocr"}}, second);
        queue.cancelTask(0);
        QCOMPARE(queue.count(), 1);
        second->progress("识别中", 64);
        QCOMPARE(queue.findChild<QProgressBar *>("lyricQueueProgress")->value(), 64);
        second->finished({{"ok", true}, {"lrc", "[00:01.00]剩余任务"}});
        QCOMPARE(queue.taskState(0), QString("待应用"));
        auto list = queue.findChild<QListWidget *>("lyricQueueTasks");
        list->setCurrentRow(0);
        list->setFocus();
        QTest::qWait(50);
        QTest::keyClick(list, Qt::Key_Delete);
        QCOMPARE(queue.count(), 0);
        auto third = new LyricJob(&settings);
        queue.enqueue(id, {{"operation", "ocr"}}, third);
        auto remove = list->itemWidget(list->item(0))->findChild<QPushButton *>("removeQueuedTask");
        QVERIFY(remove);
        remove->click();
        QCOMPARE(queue.count(), 0);
        QVERIFY(library.track(id).value("lrc").toString().isEmpty());
    }
    void cancellingKeepsResultOut() {
        QTemporaryDir dir(QDir::current().filePath("lyrics-test-XXXXXX"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        LyricJob job(&settings);
        QSignalSpy done(&job, &LyricJob::finished);
        QVERIFY(job.start(
            {{"operation", "ocr"}, {"video", "does-not-exist"}, {"text", "test"}, {"language", "en"}}));
        job.cancel();
        QVERIFY(done.wait(10000));
        const auto result = done.first().first().toJsonObject();
        QVERIFY(!result["ok"].toBool());
        QVERIFY(result["error"].toString().contains("取消"));
    }
    void onlineLookup() {
        if (!qEnvironmentVariableIsSet("BILIWAVE_LIVE_LYRICS"))
            QSKIP("Live network check is opt-in");
        QTemporaryDir dir(QDir::current().filePath("lyrics-test-XXXXXX"));
        Library library(dir.filePath("data"));
        QSettings settings(dir.filePath("settings.ini"), QSettings::IniFormat);
        const auto id =
            library.addTrack({{"title", "Yellow"}, {"artist", "Coldplay"}, {"path", "test.flac"}});
        LyricWizard wizard(&library, &settings, id);
        wizard.findChild<QComboBox *>("lyricMethod")->setCurrentIndex(2);
        wizard.show();
        wizard.findChild<QPushButton *>("searchPlainLyrics")->click();
        auto list = wizard.findChild<QListWidget *>("plainLyricCandidates");
        QTRY_VERIFY_WITH_TIMEOUT(list->count() > 0, 30000);
        const auto item = list->item(0)->data(Qt::UserRole).toMap();
        QVERIFY(!item.value("plainLyrics").toString().isEmpty() ||
                !item.value("syncedLyrics").toString().isEmpty());
        wizard.findChild<QPushButton *>("lyricWizardNext")->click();
        QVERIFY(!wizard.findChild<QPlainTextEdit *>("lyricTextEditor")->toPlainText().isEmpty());
        QCOMPARE(library.track(id).value("lrc").toString(), QString());
        QDir screenshots(QDir(resourceRoot()).filePath("docs/screenshots"));
        QVERIFY(wizard.grab().save(screenshots.filePath("lyric-online-review.png")));
    }
};
QTEST_MAIN(LyricWorkflowTests)
#include "lyric_workflow_tests.moc"
