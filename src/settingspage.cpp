#include "desktopintegration.h"
#include "lyricworkflow.h"
#include "theme.h"
#include "window.h"
#include <QBoxLayout>
#include <QButtonGroup>
#include <QColorDialog>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFormLayout>
#include <QScrollArea>
#include <QSignalBlocker>
#include <algorithm>
#include <memory>

#include "uihelpers.h"
using namespace Ui;

QWidget *MainWindow::createSettingsPage() {
    auto page = new QWidget;
    page->setObjectName("settingsPage");
    auto rootLayout = new QVBoxLayout(page);
    rootLayout->setContentsMargins(0, 0, 0, 0);
    rootLayout->setSpacing(16);
    auto categories = new QHBoxLayout;
    auto categoryGroup = new QButtonGroup(page);
    categoryGroup->setObjectName("settingsCategories");
    auto sections = new QStackedWidget;
    sections->setObjectName("settingsSections");
    QList<QVBoxLayout *> sectionLayouts;
    const QStringList captions{"通用设置", "进度条", "歌词显示", "资源与工具保存", "工作台设置"};
    for (int i = 0; i < captions.size(); ++i) {
        auto button = action(captions[i], QString("settingsCategory%1").arg(i));
        button->setCheckable(true);
        button->setProperty("settingsTab", true);
        categoryGroup->addButton(button, i);
        if (i == 4) categories->insertWidget(1, button);
        else categories->addWidget(button);
        auto content = new QWidget;
        content->setObjectName(QString("settingsSection%1").arg(i));
        if (i == 2 || i == 3 || i == 4) {
            content->setMaximumWidth(i == 4 ? 1000 : 820);
            content->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
        }
        auto contentLayout = new QVBoxLayout(content);
        contentLayout->setContentsMargins(0, 0, 18, 0);
        contentLayout->setSpacing(16);
        sectionLayouts << contentLayout;
        auto scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setAlignment(Qt::AlignLeft | Qt::AlignTop);
        scroll->setWidget(content);
        sections->addWidget(scroll);
        if (i == 2 && !lyricFeaturesEnabled())
            button->setEnabled(false);
    }
    categories->addStretch();
    rootLayout->addLayout(categories);
    rootLayout->addWidget(sections, 1);
    connect(categoryGroup, &QButtonGroup::idClicked, sections, &QStackedWidget::setCurrentIndex);
    categoryGroup->button(0)->setChecked(true);
    auto generalLayout = sectionLayouts[0];
    auto lyricLayout = sectionLayouts[2];
    auto resourceLayout = sectionLayouts[3];
    auto workbenchLayout = sectionLayouts[4];
    auto layout = sectionLayouts[1];
    workbenchLayout->addWidget(label("工作台设置", "section"));
    const auto divider = [](QVBoxLayout *target, const QString &name) {
        auto line = new QFrame;
        line->setObjectName(name);
        line->setFrameShape(QFrame::HLine);
        line->setProperty("separator", true);
        line->setMaximumWidth(900);
        target->addWidget(line);
    };
    generalLayout->addWidget(label("通用设置", "section"));
    auto simplify = new QCheckBox("繁体中文自动转简体");
    simplify->setObjectName("settingsAutoSimplifyChinese");
    simplify->setChecked(m_settings->value("lyrics/autoSimplifyChinese", true).toBool());
    simplify->setToolTip("对在线匹配、字幕识别、导入及已有歌词生效；保留原文和时间轴，关闭后可查看原文。");
    generalLayout->addWidget(simplify);
    connect(simplify, &QCheckBox::toggled, this, [this](bool enabled) {
        m_settings->setValue("lyrics/autoSimplifyChinese", enabled);
        m_settings->sync();
        m_library->setAutoSimplifyLyrics(enabled);
        if (lyricFeaturesEnabled())
            m_lyricQueue->reloadTextSettings();
        if (lyricFeaturesEnabled() && m_candidates->currentItem()) {
            const auto candidate = m_candidates->currentItem()->data(0, Qt::UserRole).toMap();
            const auto timed = candidate.value("syncedLyrics").toString();
            m_candidatePreview->setPlainText(Lrc::simplifiedChinese(
                timed.isEmpty() ? candidate.value("plainLyrics").toString() : timed, enabled));
        }
    });

    lyricLayout->addWidget(label("歌词显示", "section"));
    auto choiceRow = [page](QFormLayout *form, const QString &caption, const QString &name,
                            const QList<QPair<QString, QString>> &options, const QVariant &selected) {
        auto widget = new QWidget;
        auto row = new QHBoxLayout(widget);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(14);
        auto group = new QButtonGroup(page);
        group->setObjectName(name);
        group->setExclusive(true);
        for (int i = 0; i < options.size(); ++i) {
            auto box = new QCheckBox(options[i].first);
            box->setObjectName(name + "_" + options[i].second);
            box->setProperty("value", options[i].second);
            group->addButton(box, i);
            row->addWidget(box);
            if (options[i].second == selected.toString())
                box->setChecked(true);
        }
        if (!group->checkedButton())
            group->button(0)->setChecked(true);
        row->addStretch();
        form->addRow(caption, widget);
        return group;
    };
    auto appearanceForm = new QFormLayout;
    auto themeChoices =
        choiceRow(appearanceForm, "界面主题", "settingsTheme", UiTheme::choices(),
                  UiTheme::normalized(m_settings->value("ui/theme", "dark").toString()));
    connect(themeChoices, &QButtonGroup::idClicked, this, [this, themeChoices] {
        applyTheme(themeChoices->checkedButton()->property("value").toString());
    });
    if (lyricFeaturesEnabled()) {
        auto onlineOptions = new QWidget;
        onlineOptions->setMaximumWidth(900);
        auto onlineForm = new QFormLayout(onlineOptions);
        onlineForm->setContentsMargins(0, 0, 0, 0);
        QList<QPair<QString, QString>> providerChoices;
        for (const auto &entry : LyricsSourcesSearch::sources()) providerChoices << qMakePair(entry.second, entry.first);
        auto primary = choiceRow(onlineForm, "优先歌词源", "settingsPrimaryOnlineSource",
                                 providerChoices,
                                 m_settings->value("lyrics/primaryOnlineSource", "msr"));
        auto enabledRow = new QHBoxLayout;
        const auto selected = LyricsSourcesSearch::selectedSources(m_settings);
        for (const auto &entry : LyricsSourcesSearch::sources()) {
            auto box = new QCheckBox(entry.second);
            box->setObjectName("onlineSource_" + entry.first);
            box->setChecked(selected.contains(entry.first));
            enabledRow->addWidget(box);
            connect(box, &QCheckBox::toggled, this, [this, entry](bool checked) {
                auto values = LyricsSourcesSearch::selectedSources(m_settings);
                values.removeAll(entry.first);
                if (checked) values << entry.first;
                m_settings->setValue("lyrics/onlineSources", values);
                m_settings->sync();
            });
        }
        enabledRow->addStretch();
        onlineForm->addRow("启用备用源", enabledRow);
        connect(primary, &QButtonGroup::idClicked, this, [this, primary] {
            const auto value = primary->checkedButton()->property("value").toString();
            m_settings->setValue("lyrics/primaryOnlineSource", value);
            findChild<QCheckBox *>("onlineSource_" + value)->setChecked(true);
            m_settings->sync();
        });
        auto policy = label("按优先源、备用源依次查询，命中即停；每批最多 5 个结果。请求间隔至少 1 秒，遇到限流停止。", "muted");
        policy->setWordWrap(true);
        onlineForm->addRow(policy);
        workbenchLayout->addWidget(onlineOptions);
    }
    auto audioOutput = new QWidget;
    audioOutput->setMaximumWidth(820);
    auto audioForm = new QFormLayout(audioOutput);
    audioForm->setContentsMargins(0, 0, 0, 0);
    auto deviceRow = new QHBoxLayout;
    auto outputs = new QComboBox;
    outputs->setObjectName("settingsAudioOutput");
    outputs->setMinimumWidth(220);
    outputs->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    outputs->setToolTip("默认跟随系统输出；也可固定耳机或扬声器。设备断开时临时跟随系统，重新接入后自动恢复。");
    outputs->installEventFilter(this);
    auto detect = action("重新检测", "detectAudioOutputs");
    deviceRow->addWidget(outputs, 1);
    deviceRow->addWidget(detect);
    audioForm->addRow("音频输出", deviceRow);
    auto outputStatus = label({}, "muted");
    outputStatus->setObjectName("audioOutputStatus");
    outputStatus->setWordWrap(true);
    audioForm->addRow(QString(), outputStatus);
    auto refreshOutputs = [this, outputs, outputStatus] {
        const QSignalBlocker guard(outputs);
        outputs->clear();
        const auto preferred = m_player.outputDeviceId();
        const auto system = QMediaDevices::defaultAudioOutput();
        outputs->addItem(system.isNull() ? "跟随系统默认（暂无输出设备）"
                                        : "跟随系统默认 · " + system.description(), QByteArray());
        const auto devices = QMediaDevices::audioOutputs();
        for (const auto &device : devices)
            outputs->addItem(device.description() + (device.id() == system.id() ? "（系统默认）" : ""),
                             device.id());
        int selected = outputs->findData(preferred);
        if (selected < 0) {
            outputs->addItem(m_settings->value("audio/outputDeviceName", "已选择的设备").toString() +
                                 "（未连接，暂用系统默认）", preferred);
            selected = outputs->count() - 1;
        }
        outputs->setCurrentIndex(selected);
        const auto actual = m_player.audio()->device();
        outputStatus->setText(actual.isNull() ? "未检测到音频输出设备，接入后自动恢复。"
                                              : "当前输出：" + actual.description());
    };
    connect(&m_player, &Player::audioOutputsChanged, page, refreshOutputs);
    connect(outputs, &QComboBox::activated, page, [this, outputs](int index) {
        const auto id = outputs->itemData(index).toByteArray();
        m_settings->setValue("audio/outputDevice", id.toBase64());
        m_settings->setValue("audio/outputDeviceName", outputs->itemText(index));
        m_settings->sync();
        m_player.setOutputDeviceId(id);
    });
    connect(detect, &QPushButton::clicked, page, [this] { m_player.refreshAudioOutputs(true); });
    refreshOutputs();
    auto closeToTray = new QCheckBox("关闭主窗口时最小化到托盘");
    closeToTray->setObjectName("settingsCloseToTray");
    closeToTray->setChecked(m_settings->value("application/closeToTray", true).toBool());
    closeToTray->setToolTip("关闭按钮隐藏主窗口，播放、下载和识别继续；从托盘菜单选择退出可结束程序。");
    generalLayout->addWidget(closeToTray);
    connect(closeToTray, &QCheckBox::toggled, this, [this](bool enabled) {
        m_settings->setValue("application/closeToTray", enabled);
        m_settings->sync();
    });
    auto autoStart = new QCheckBox("开机自启动（启动到托盘）");
    autoStart->setObjectName("settingsAutoStart");
    autoStart->setChecked(DesktopIntegration::startupEnabled());
    autoStart->setToolTip(
        "为当前 Windows 用户启用开机启动，使用当前程序和资料库路径；取消勾选即移除启动项。");
#ifndef Q_OS_WIN
    autoStart->setEnabled(false);
#endif
    generalLayout->addWidget(autoStart);
    auto startupStatus = label("", "muted");
    startupStatus->setObjectName("startupSettingStatus");
    startupStatus->setWordWrap(true);
    startupStatus->hide();
    generalLayout->addWidget(startupStatus);
    connect(autoStart, &QCheckBox::toggled, this, [this, autoStart, startupStatus](bool enabled) {
        QString error;
        if (!DesktopIntegration::setStartupEnabled(enabled, m_library->directory(), &error)) {
            const QSignalBlocker guard(autoStart);
            autoStart->setChecked(DesktopIntegration::startupEnabled());
            startupStatus->setText(error);
            startupStatus->show();
            return;
        }
        m_settings->setValue("application/autoStart", enabled);
        m_settings->sync();
        startupStatus->clear();
        startupStatus->hide();
        notify(enabled ? "已开启开机自启动" : "已关闭开机自启动");
    });
    divider(generalLayout, "generalDuckingDivider");
    generalLayout->addWidget(label("自动消声", "section"));
    auto duckEnabled = new QCheckBox("自动降低音乐音量");
    duckEnabled->setObjectName("settingsDuckingEnabled");
    duckEnabled->setChecked(m_settings->value("audioDucking/enabled", false).toBool());
    generalLayout->addWidget(duckEnabled);
    auto duckOptions = new QWidget;
    duckOptions->setMaximumWidth(820);
    auto duckForm = new QFormLayout(duckOptions);
    duckForm->setContentsMargins(0, 0, 0, 0);
    auto triggers = new QHBoxLayout;
    auto onSpeech = new QCheckBox("麦克风检测到说话");
    auto onMic = new QCheckBox("其他应用正在使用麦克风");
    auto onOtherAudio = new QCheckBox("其他应用正在发声");
    onSpeech->setObjectName("settingsDuckingOnSpeech");
    onMic->setObjectName("settingsDuckingOnMicrophone");
    onOtherAudio->setObjectName("settingsDuckingOnOtherAudio");
    onSpeech->setChecked(m_settings->value("audioDucking/onSpeech", true).toBool());
    onMic->setChecked(m_settings->value("audioDucking/onMicrophone", true).toBool());
    onOtherAudio->setChecked(m_settings->value("audioDucking/onOtherAudio", true).toBool());
    onOtherAudio->setToolTip(
        "检测其他应用的实际播放音量，排除 BiliWave 自身；静音或无声的播放不会触发，不录制其他应用音频。");
    onSpeech->setToolTip("读取默认麦克风的音量判断说话，不保存或上传音频。较响的环境声音也可能触发。");
    onMic->setToolTip(
        "检测 Windows 麦克风使用状态，不读取其他应用录音。应用内静音但仍占用麦克风时也会触发。");
    triggers->addWidget(onSpeech);
    triggers->addWidget(onMic);
    triggers->addWidget(onOtherAudio);
    triggers->addStretch();
    duckForm->addRow("触发条件", triggers);
    auto reduction = new QSpinBox;
    reduction->setObjectName("settingsDuckingReduction");
    reduction->setRange(0, 100);
    reduction->setSuffix(" %");
    reduction->setMaximumWidth(140);
    reduction->setValue(qBound(0, m_settings->value("audioDucking/reduction", 70).toInt(), 100));
    reduction->setToolTip("相对当前音乐音量降低的比例。70% 表示保留原音量的 30%；100% 为静音。");
    duckForm->addRow("降低幅度", reduction);
    generalLayout->addWidget(duckOptions, 0, Qt::AlignLeft);
    auto duckStatus = label("已关闭", "muted");
    duckStatus->setObjectName("settingsDuckingStatus");
    duckStatus->setWordWrap(true);
    duckStatus->setMaximumWidth(820);
    generalLayout->addWidget(duckStatus);
    duckOptions->setEnabled(duckEnabled->isChecked());
    connect(&m_ducking, &AutoDucking::statusChanged, duckStatus, &QLabel::setText);
    auto applyDucking = [this, duckEnabled, duckOptions, onSpeech, onMic, onOtherAudio, reduction] {
        m_settings->setValue("audioDucking/enabled", duckEnabled->isChecked());
        m_settings->setValue("audioDucking/onSpeech", onSpeech->isChecked());
        m_settings->setValue("audioDucking/onMicrophone", onMic->isChecked());
        m_settings->setValue("audioDucking/onOtherAudio", onOtherAudio->isChecked());
        m_settings->setValue("audioDucking/reduction", reduction->value());
        m_settings->sync();
        duckOptions->setEnabled(duckEnabled->isChecked());
        m_ducking.reloadSettings();
    };
    for (auto box : {duckEnabled, onSpeech, onMic, onOtherAudio})
        connect(box, &QCheckBox::toggled, this, applyDucking);
    connect(reduction, &QSpinBox::valueChanged, this, applyDucking);
    divider(generalLayout, "generalThemeDivider");
    generalLayout->addLayout(appearanceForm);
    divider(generalLayout, "generalAudioOutputDivider");
    generalLayout->addWidget(audioOutput);
    auto showFloat = new QCheckBox("显示顶部进度条"), showLyrics = new QCheckBox("显示桌面歌词");
    showFloat->setObjectName("settingsShowFloat");
    showLyrics->setObjectName("settingsShowLyrics");
    showFloat->setChecked(m_settings->value("windows/showFloat", true).toBool());
    showLyrics->setChecked(m_settings->value("windows/showLyrics", true).toBool());
    layout->addWidget(showFloat);
    if (lyricFeaturesEnabled())
        lyricLayout->addWidget(showLyrics);
    else {
        showLyrics->setParent(page);
        showLyrics->hide();
    }
    connect(showFloat, &QCheckBox::toggled, m_floatButton, &QPushButton::setChecked);
    connect(showLyrics, &QCheckBox::toggled, m_lyricButton, &QPushButton::setChecked);
    connect(m_floatButton, &QPushButton::toggled, showFloat, &QCheckBox::setChecked);
    connect(m_lyricButton, &QPushButton::toggled, showLyrics, &QCheckBox::setChecked);
    layout->addWidget(label("顶部进度条", "section"));
    auto progressForm = new QFormLayout;
    auto colorControl = [this, page, progressForm](const QString &caption, const QString &key,
                                                   const QString &fallback, const QString &name) {
        auto row = new QHBoxLayout;
        auto input = new QLineEdit(m_settings->value(key, fallback).toString());
        input->setObjectName(name);
        input->setMaximumWidth(150);
        auto choose = action("选择颜色");
        row->addWidget(input);
        row->addWidget(choose);
        row->addStretch();
        auto apply = [this, input, choose, key, fallback] {
            const QColor color(input->text().trimmed());
            if (!color.isValid()) {
                input->setText(m_settings->value(key, fallback).toString());
                notify("请输入有效颜色，例如 #FF3355");
                return;
            }
            input->setText(color.name());
            choose->setStyleSheet(colorSwatchStyle(color));
            m_settings->setValue(key, color.name());
            m_settings->sync();
            m_float->applySettings();
        };
        choose->setStyleSheet(colorSwatchStyle(QColor(input->text())));
        connect(input, &QLineEdit::editingFinished, page, apply);
        connect(choose, &QPushButton::clicked, page, [page, input, apply] {
            const auto color = QColorDialog::getColor(QColor(input->text()), page, "进度条颜色");
            if (color.isValid()) {
                input->setText(color.name());
                apply();
            }
        });
        progressForm->addRow(caption, row);
    };
    colorControl("已播放颜色", "progress/leftColor", "#ff3355", "progressLeftColor");
    colorControl("未播放颜色", "progress/rightColor", "#327dff", "progressRightColor");
    colorControl("待播短点颜色", "progress/queueMarkerColor", "#ffffff", "progressQueueMarkerColor");
    auto markerLength = new QSpinBox, markerLimit = new QSpinBox;
    markerLength->setObjectName("progressQueueMarkerLength");
    markerLength->setRange(1, 128);
    markerLength->setSuffix(" px");
    markerLength->setValue(qBound(1, m_settings->value("progress/queueMarkerLength", 10).toInt(), 128));
    markerLength->setMaximumWidth(180);
    markerLimit->setObjectName("progressQueueMarkerLimit");
    markerLimit->setRange(0, 10);
    markerLimit->setSuffix(" 个");
    markerLimit->setValue(qBound(0, m_settings->value("progress/queueMarkerLimit", 10).toInt(), 10));
    markerLimit->setMaximumWidth(180);
    progressForm->addRow("待播短点长度", markerLength);
    progressForm->addRow("待播短点上限", markerLimit);
    connect(markerLength, &QSpinBox::valueChanged, page, [this](int value) {
        m_settings->setValue("progress/queueMarkerLength", value);
        m_settings->sync();
        m_float->applySettings();
    });
    connect(markerLimit, &QSpinBox::valueChanged, page, [this](int value) {
        m_settings->setValue("progress/queueMarkerLimit", value);
        m_settings->sync();
        m_float->applySettings();
    });
    auto fadeHeight = new QSpinBox;
    fadeHeight->setObjectName("progressFadeHeight");
    fadeHeight->setRange(1, 512);
    fadeHeight->setSuffix(" px");
    fadeHeight->setValue(qBound(1, m_settings->value("progress/fadeHeight", 24).toInt(), 512));
    fadeHeight->setMaximumWidth(180);
    fadeHeight->setToolTip("实际屏幕像素高度，向下逐渐透明；修改后立即生效。");
    progressForm->addRow("向下渐隐高度", fadeHeight);
    connect(fadeHeight, &QSpinBox::valueChanged, page, [this](int value) {
        m_settings->setValue("progress/fadeHeight", value);
        m_settings->sync();
        m_float->applySettings();
    });
    auto gradientWidth = new QSpinBox;
    gradientWidth->setObjectName("progressGradientWidth");
    gradientWidth->setRange(0, 4096);
    gradientWidth->setSuffix(" px");
    gradientWidth->setValue(qBound(0, m_settings->value("progress/gradientWidth", 80).toInt(), 4096));
    gradientWidth->setMaximumWidth(180);
    gradientWidth->setToolTip("红蓝交界处的横向渐变宽度，单位为实际屏幕像素；0 表示直接分界。");
    progressForm->addRow("渐变宽度", gradientWidth);
    connect(gradientWidth, &QSpinBox::valueChanged, page, [this](int value) {
        m_settings->setValue("progress/gradientWidth", value);
        m_settings->sync();
        m_float->applySettings();
    });
    layout->addLayout(progressForm);
    auto lyricPane = lyricLayout->parentWidget();
    if (!lyricFeaturesEnabled())
        lyricPane->hide();
    layout = workbenchLayout;
    auto form = new QFormLayout;
    form->setVerticalSpacing(14);
    auto source = choiceRow(form, "默认获取来源", "settingsLyricSource",
                            {{"线上匹配", "online"}, {"本机识别", "offline"}, {"导入 LRC", "local"}},
                            m_settings->value("lyrics/source", "online"));
    auto ocrDevice = choiceRow(form, "字幕识别计算", "settingsOcrDevice",
                               {{"自动（优先 GPU）", "auto"}, {"GPU", "gpu"}, {"CPU", "cpu"}},
                               m_settings->value("lyrics/ocrDevice", "auto"));
    auto ocrLanguage = choiceRow(form, "默认字幕语言", "settingsOcrLanguage",
                                 {{"中英", "zh"},
                                  {"俄语", "ru"},
                                  {"意大利语", "it"},
                                  {"英汉双语", "en_zh"},
                                  {"俄汉双语", "ru_zh"},
                                  {"意汉双语", "it_zh"}},
                                 m_settings->value("lyrics/ocrLanguage", "zh"));
    auto autoFetch = new QCheckBox("切歌后自动获取歌词");
    autoFetch->setChecked(m_settings->value("lyrics/autoFetch", true).toBool());
    form->addRow(autoFetch);
    auto language = choiceRow(form, "歌词配时语言", "settingsAlignLanguage",
                              {{"自动检测", "auto"},
                               {"中文", "zh"},
                               {"英文", "en"},
                               {"日文", "ja"},
                               {"韩文", "ko"},
                               {"俄语", "ru"},
                               {"意大利语", "it"}},
                              m_settings->value("lyrics/language", "auto"));
    if (!alignmentFeaturesEnabled()) {
        form->labelForField(language->buttons().first()->parentWidget())->setVisible(false);
        language->buttons().first()->parentWidget()->hide();
    }
    if (!recognitionFeaturesEnabled()) {
        for (auto button : source->buttons())
            if (button != source->button(0)) { button->setEnabled(false); button->hide(); }
        for (auto group : {ocrDevice, ocrLanguage, language}) {
            auto field = group->buttons().first()->parentWidget();
            form->labelForField(field)->hide();
            field->hide();
        }
        source->button(0)->setChecked(true);
    }
    auto generalPane = new QWidget;
    generalPane->setLayout(form);
    generalPane->setObjectName("workbenchDefaultOptions");
    generalPane->setMaximumWidth(900);
    workbenchLayout->addWidget(generalPane);
    layout = lyricLayout;
    form = new QFormLayout;
    form->setVerticalSpacing(14);
    auto opacity = new QSlider(Qt::Horizontal);
    opacity->setObjectName("settingsLyricOpacity");
    opacity->setRange(0, 100);
    opacity->setValue(m_settings->value("lyrics/opacity", 100).toInt());
    auto opacityText = label("", "muted");
    auto opacityRow = new QHBoxLayout;
    opacityRow->addWidget(opacity, 1);
    opacityRow->addWidget(opacityText);
    auto showOpacity = [opacityText](int value) { opacityText->setText(QString("%1%").arg(value)); };
    showOpacity(opacity->value());
    connect(opacity, &QSlider::valueChanged, page, showOpacity);
    form->addRow("左上不透明度", opacityRow);
    auto size = new QSpinBox;
    size->setObjectName("settingsLyricPrimarySize");
    size->setRange(10, 96);
    size->setValue(m_settings->value("lyrics/fontSize", 30).toInt());
    form->addRow("左上字号", size);
    auto color = action("选择歌词颜色");
    auto colorValue = std::make_shared<QString>(m_settings->value("lyrics/color", "#a4f0d1").toString());
    color->setStyleSheet(colorSwatchStyle(QColor(*colorValue)));
    connect(color, &QPushButton::clicked, page, [page, color, colorValue] {
        auto chosen = QColorDialog::getColor(QColor(*colorValue), page);
        if (chosen.isValid()) {
            *colorValue = chosen.name();
            color->setStyleSheet(colorSwatchStyle(QColor(*colorValue)));
        }
    });
    color->setObjectName("settingsLyricPrimaryColor");
    form->addRow("左上颜色", color);
    auto secondarySize = new QSpinBox;
    secondarySize->setObjectName("settingsLyricSecondarySize");
    secondarySize->setRange(10, 96);
    secondarySize->setValue(
        m_settings->value("lyrics/secondaryFontSize", qMax(12, int(size->value() * .65))).toInt());
    form->addRow("右下字号", secondarySize);
    auto secondaryOpacity = new QSlider(Qt::Horizontal);
    secondaryOpacity->setObjectName("settingsLyricSecondaryOpacity");
    secondaryOpacity->setRange(0, 100);
    secondaryOpacity->setValue(m_settings->value("lyrics/secondaryOpacity", opacity->value() / 2).toInt());
    auto secondaryOpacityRow = new QHBoxLayout;
    auto secondaryOpacityText = label(QString::number(secondaryOpacity->value()) + "%", "muted");
    secondaryOpacityRow->addWidget(secondaryOpacity, 1);
    secondaryOpacityRow->addWidget(secondaryOpacityText);
    connect(secondaryOpacity, &QSlider::valueChanged, page, [secondaryOpacityText](int value) {
        secondaryOpacityText->setText(QString::number(value) + "%");
    });
    form->addRow("右下不透明度", secondaryOpacityRow);
    auto secondaryColor = action("选择歌词颜色", "settingsLyricSecondaryColor");
    auto secondaryColorValue =
        std::make_shared<QString>(m_settings->value("lyrics/secondaryColor", *colorValue).toString());
    secondaryColor->setStyleSheet(colorSwatchStyle(QColor(*secondaryColorValue)));
    connect(secondaryColor, &QPushButton::clicked, page, [page, secondaryColor, secondaryColorValue] {
        const auto chosen = QColorDialog::getColor(QColor(*secondaryColorValue), page);
        if (chosen.isValid()) {
            *secondaryColorValue = chosen.name();
            secondaryColor->setStyleSheet(colorSwatchStyle(QColor(*secondaryColorValue)));
        }
    });
    form->addRow("右下颜色", secondaryColor);
    auto horizontalOffset = new QSpinBox, verticalOffset = new QSpinBox;
    horizontalOffset->setObjectName("settingsLyricHorizontalOffset");
    verticalOffset->setObjectName("settingsLyricVerticalOffset");
    horizontalOffset->setRange(-600, 1200);
    verticalOffset->setRange(0, 400);
    horizontalOffset->setSuffix(" px");
    verticalOffset->setSuffix(" px");
    horizontalOffset->setValue(m_settings->value("lyrics/horizontalOffset", 180).toInt());
    verticalOffset->setValue(m_settings->value("lyrics/verticalOffset", 80).toInt());
    form->addRow("右下水平位差", horizontalOffset);
    form->addRow("右下垂直位差", verticalOffset);
    auto locked = new QCheckBox("锁定桌面歌词位置"),
         through = new QCheckBox("鼠标穿透（在此取消可恢复拖动）");
    locked->setChecked(m_settings->value("lyrics/locked", false).toBool());
    through->setChecked(m_settings->value("lyrics/clickThrough", false).toBool());
    form->addRow(locked);
    form->addRow(through);
    layout->addLayout(form);
    auto explanation = label("双语歌词：外语左上、中文右下；单语歌词：当前句左上、下一句右下。位差以左上文字"
                             "起点为基准，单位为像素。",
                             "muted");
    explanation->setWordWrap(true);
    layout->addWidget(explanation);
    layout = resourceLayout;
    layout->addWidget(label("资源与本机工具", "section"));
    auto paths = new QFormLayout;
    paths->setVerticalSpacing(12);
    QMap<QString, QLineEdit *> edits;
    auto pathRow = [&, page](const QString &key, const QString &name, const QString &value, bool directory) {
        auto row = new QHBoxLayout;
        auto edit = new QLineEdit(value);
        auto nameKey = key;
        nameKey.replace('/', '_');
        edit->setObjectName("settingsPath_" + nameKey);
        auto browse = action("选择");
        row->addWidget(edit, 1);
        row->addWidget(browse);
        edits[key] = edit;
        paths->addRow(name, row);
        connect(browse, &QPushButton::clicked, page, [page, edit, directory] {
            auto path = directory ? QFileDialog::getExistingDirectory(page, {}, edit->text())
                                  : QFileDialog::getOpenFileName(page, {}, edit->text());
            if (!path.isEmpty())
                edit->setText(path);
        });
    };
    pathRow(
        "download/directory", "音频保存目录",
        m_settings->value("download/directory", QDir(m_library->directory()).filePath("media")).toString(),
        true);
    pathRow("download/cookies", "可选 Cookie 文件", m_settings->value("download/cookies").toString(), false);
    QStringList tools{"yt-dlp", "ffmpeg", "ffprobe"};
    for (const auto &tool : tools)
        pathRow("tools/" + tool, tool, toolPath(m_settings, tool), false);
    if (recognitionFeaturesEnabled()) {
        if (alignmentFeaturesEnabled())
            pathRow("lyrics/alignmentModel", "已有歌词对齐模型",
                m_settings->value("lyrics/alignmentModel", QDir(resourceRoot()).filePath("models/small.pt"))
                    .toString(),
                false);
        pathRow("tools/lyric-python", "歌词工具运行环境",
                m_settings
                    ->value("tools/lyric-python",
                            QDir(resourceRoot()).filePath("tools/lyrics-runtime/python.exe"))
                    .toString(),
                false);
    }
    layout->addLayout(paths);
    auto note = label(lyricFeaturesEnabled()
                          ? "Cookie 使用本机 Netscape 格式文件。在线歌词仅发送检索信息，本机识别不上传音频。"
                          : "Cookie 使用本机 Netscape 格式文件。",
                      "muted");
    note->setWordWrap(true);
    layout->addWidget(note);
    auto actions = new QHBoxLayout;
    auto save = action("保存设置", "primary"), data = action("打开资料库目录");
    save->setObjectName("saveSettings");
    actions->addWidget(save);
    actions->addWidget(data);
    actions->addStretch();
    rootLayout->addLayout(actions);
    for (auto sectionLayout : sectionLayouts)
        sectionLayout->addStretch();
    connect(data, &QPushButton::clicked, this,
            [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(m_library->directory())); });
    connect(save, &QPushButton::clicked, page, [=, this] {
        if (lyricFeaturesEnabled()) {
            m_settings->setValue("lyrics/source", source->checkedButton()->property("value"));
            m_settings->setValue("lyrics/ocrDevice", ocrDevice->checkedButton()->property("value"));
            m_settings->setValue("lyrics/ocrLanguage", ocrLanguage->checkedButton()->property("value"));
            m_settings->setValue("lyrics/autoFetch", autoFetch->isChecked());
            m_settings->setValue("lyrics/opacity", opacity->value());
            m_settings->setValue("lyrics/fontSize", size->value());
            m_settings->setValue("lyrics/color", *colorValue);
            m_settings->setValue("lyrics/secondaryFontSize", secondarySize->value());
            m_settings->setValue("lyrics/secondaryOpacity", secondaryOpacity->value());
            m_settings->setValue("lyrics/secondaryColor", *secondaryColorValue);
            m_settings->setValue("lyrics/horizontalOffset", horizontalOffset->value());
            m_settings->setValue("lyrics/verticalOffset", verticalOffset->value());
            m_settings->setValue("lyrics/locked", locked->isChecked());
            m_settings->setValue("lyrics/clickThrough", through->isChecked());
            m_settings->setValue("lyrics/language", language->checkedButton()->property("value"));
            m_lyricSource->setCurrentIndex(
                qMax(0, m_lyricSource->findData(source->checkedButton()->property("value"))));
        }
        for (auto it = edits.cbegin(); it != edits.cend(); ++it)
            m_settings->setValue(it.key(), it.value()->text().trimmed());
        m_settings->sync();
        m_overlay->applySettings();
        notify("设置已保存");
    });
    return page;
}
