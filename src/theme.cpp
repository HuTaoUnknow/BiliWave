#include "theme.h"
#include <QWidget>
#include <QMap>

namespace UiTheme {
QString normalized(const QString &name) {
    for (const auto &choice : choices()) if (choice.second == name) return name;
    return "dark";
}
QList<QPair<QString, QString>> choices() {
    static const QList<QPair<QString, QString>> themes{
        {"大肥鱼", "dark"}, {"ChatGPT", "chatgpt"}, {"Grok", "grok"}, {"Claude", "claude"}, {"浅粉玫瑰", "pink"}};
    return themes;
}
struct Colors {
    QString background, surface, input, text, muted, border, accent, hover, selection, disabled, onAccent;
};
static Colors colors(const QString &name) {
    if (normalized(name) == "pink")
        return {"#fff8fb", "#f8e5ed", "#fffafd", "#54283c", "#916477",
                "#eacdd9", "#d94f8a", "#f5d8e5", "#f7d6e5", "#b395a3", "#ffffff"};
    if (name == "chatgpt")
        return {"#111917", "#19241f", "#15201c", "#edf7f2", "#a1b6ab",
                "#31473c", "#41b996", "#233c30", "#214d3d", "#6b8075", "#05251b"};
    if (name == "grok")
        return {"#09090b", "#141416", "#101013", "#f4f4f5", "#a6a6ae",
                "#333338", "#f2f2f3", "#242429", "#33333a", "#696972", "#141416"};
    if (name == "claude")
        return {"#f7f3eb", "#eee7da", "#fffcf5", "#332c25", "#796f64",
                "#d9cfbf", "#b45432", "#eae0d0", "#f0dfcf", "#a99e90", "#ffffff"};
    return {"#0d141d", "#151f2b", "#111c28", "#eef3fc", "#9baabd",
            "#293748", "#287eff", "#203247", "#19365b", "#637184", "#ffffff"};
}
static QPalette makePalette(const QString &name) {
    const auto c = colors(name);
    QPalette p;
    p.setColor(QPalette::Window, QColor(c.background));
    p.setColor(QPalette::WindowText, QColor(c.text));
    p.setColor(QPalette::Base, QColor(c.background));
    p.setColor(QPalette::AlternateBase, QColor(c.input));
    p.setColor(QPalette::Text, QColor(c.text));
    p.setColor(QPalette::Button, QColor(c.surface));
    p.setColor(QPalette::ButtonText, QColor(c.text));
    p.setColor(QPalette::Highlight, QColor(c.accent));
    p.setColor(QPalette::HighlightedText, QColor(c.onAccent));
    p.setColor(QPalette::Accent, QColor(c.accent));
    p.setColor(QPalette::Link, QColor(c.accent));
    p.setColor(QPalette::Mid, QColor(c.border));
    p.setColor(QPalette::PlaceholderText, QColor(c.muted));
    p.setColor(QPalette::ToolTipBase, QColor(c.surface));
    p.setColor(QPalette::ToolTipText, QColor(c.text));
    for (auto role : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, QColor(c.disabled));
    return p;
}
QPalette palette(const QString &name) {
    static const auto palettes = [] {
        QMap<QString, QPalette> result;
        for (const auto &choice : choices()) result.insert(choice.second, makePalette(choice.second));
        return result;
    }();
    return palettes.value(normalized(name));
}
QPalette forWidget(const QWidget *widget) {
    // Local widget styles can resolve Accent to the Windows accent instead of
    // inheriting it. Custom artwork reads the owning BiliWave theme explicitly.
    for (auto owner = widget; owner; owner = owner->parentWidget())
        if (owner->property("uiTheme").isValid())
            return palette(owner->property("uiTheme").toString());
    return widget ? widget->palette() : QPalette();
}
QString styleSheet(const QString &name) {
    const auto c = colors(name);
    QString css = R"(
QWidget {background:@background;color:@text;font-family:'Microsoft YaHei UI';font-size:13px;}
QLabel {background:transparent;} QLabel#brand {font-size:21px;font-weight:700;letter-spacing:1px;color:@accent;}
QLabel#pageTitle {font-size:28px;font-weight:700;} QLabel#section {font-size:16px;font-weight:600;}
QLabel#muted {color:@muted;} QLabel#songTitle {font-size:14px;font-weight:600;} QLabel#currentQueueSong {color:@accent;}
QWidget#navigation {background:@surface;border-right:1px solid @border;}
QWidget#footer {background:@surface;border-top:1px solid @border;border-radius:8px;}
QWidget#downloadPanel,QWidget#lyricTaskQueue {border-left:1px solid @border;}
QWidget#queueEntry,QWidget#downloadEntry,QWidget#lyricQueueCell {background:transparent;}
QPushButton {background:@surface;border:1px solid @border;border-radius:7px;padding:8px 12px;}
QPushButton:hover {background:@hover;border-color:@accent;}
QPushButton:pressed {background:@selection;}
QPushButton:disabled {color:@disabled;border-color:@border;background:@background;}
QPushButton:checked {background:@selection;border-color:@accent;color:@accent;}
QPushButton#primary,QPushButton[primary="true"] {background:@accent;border:1px solid @accent;color:@onAccent;font-weight:600;}
QPushButton#primary:hover,QPushButton[primary="true"]:hover {background:@accent;border-color:@text;}
QPushButton#primary:disabled,QPushButton[primary="true"]:disabled {background:@surface;border-color:@border;color:@disabled;}
QPushButton[nav="true"] {text-align:left;padding:12px 15px;border:0;background:transparent;font-size:15px;}
QPushButton[nav="true"]:hover {background:@hover;}
QPushButton[nav="true"]:checked {background:@accent;color:@onAccent;}
QPushButton[transport="true"] {padding:0;border:0;background:transparent;}
QPushButton#transportToggle {padding:0;border-radius:22px;background:@accent;border:0;}
QPushButton#transportToggle:focus {border:2px solid @text;}
QPushButton#discardDownloadedTrack:focus,QPushButton#collectDownloadedTrack:focus,QPushButton#openDownloadedTrack:focus,QPushButton[searchChoice="true"]:focus {border:2px solid @accent;}
QPushButton[settingsTab="true"] {background:transparent;border:0;border-bottom:2px solid @border;border-radius:0;padding:12px 16px;color:@muted;}
QPushButton[settingsTab="true"]:checked {border-bottom:2px solid @accent;color:@accent;font-weight:600;}
QLineEdit,QPlainTextEdit,QTextEdit,QSpinBox,QDoubleSpinBox,QComboBox {background:@input;border:1px solid @border;border-radius:6px;padding:7px;selection-background-color:@accent;selection-color:@onAccent;}
QLineEdit:focus,QPlainTextEdit:focus,QTextEdit:focus,QSpinBox:focus,QDoubleSpinBox:focus,QComboBox:focus {border-color:@accent;}
QSpinBox,QDoubleSpinBox {padding-right:20px;}
QSpinBox::up-button,QDoubleSpinBox::up-button {subcontrol-origin:border;subcontrol-position:top right;width:16px;height:16px;border-left:1px solid @border;}
QSpinBox::down-button,QDoubleSpinBox::down-button {subcontrol-origin:border;subcontrol-position:bottom right;width:16px;height:16px;border-left:1px solid @border;}
QSpinBox::up-arrow,QDoubleSpinBox::up-arrow {image:url(:/biliwave/@upIcon);width:8px;height:5px;}
QSpinBox::down-arrow,QDoubleSpinBox::down-arrow {image:url(:/biliwave/@downIcon);width:8px;height:5px;}
QSpinBox::up-button:hover,QDoubleSpinBox::up-button:hover,QSpinBox::down-button:hover,QDoubleSpinBox::down-button:hover {background:@hover;}
QComboBox::drop-down {border:0;width:22px;} QComboBox QAbstractItemView {background:@surface;selection-background-color:@accent;selection-color:white;}
QTreeWidget,QListWidget,QTableWidget {background:@background;border:1px solid @border;border-radius:8px;outline:0;alternate-background-color:@input;selection-color:@text;}
QTreeWidget::item {padding:10px 8px;border-bottom:1px solid @border;} QListWidget::item {padding:13px 10px;border-bottom:1px solid @border;}
QTreeWidget::item:hover,QListWidget::item:hover {background:@hover;}
QTreeWidget::item:selected,QListWidget::item:selected {background:@selection;color:@text;}
QHeaderView::section {background:@surface;color:@muted;border:0;border-bottom:1px solid @border;padding:10px 8px;}
QSlider::groove:horizontal {height:4px;background:@border;border-radius:2px;} QSlider::sub-page:horizontal {background:@accent;}
QSlider {background:transparent;}
QSlider::handle:horizontal {background:@accent;border:2px solid @background;width:10px;margin:-5px 0;border-radius:7px;}
QCheckBox,QRadioButton {spacing:8px;background:transparent;padding:5px 0;}
QCheckBox::indicator {width:16px;height:16px;border:1px solid @muted;border-radius:4px;background:@input;}
QCheckBox::indicator:checked {background:@accent;border:1px solid @accent;image:url(:/biliwave/@checkIcon);}
QCheckBox::indicator:hover {border-color:@accent;}
QProgressBar {background:@input;border:1px solid @border;border-radius:5px;min-height:12px;text-align:center;color:@text;}
QProgressBar::chunk {background:@accent;border-radius:4px;}
QScrollArea {border:0;} QScrollBar:vertical {background:@background;width:8px;margin:0;}
QScrollBar::handle:vertical {background:@border;min-height:30px;border-radius:4px;}
QScrollBar:horizontal {background:@background;height:8px;margin:0;}
QScrollBar::handle:horizontal {background:@border;min-width:30px;border-radius:4px;}
QScrollBar::add-line,QScrollBar::sub-line {width:0;height:0;} QScrollBar::add-page,QScrollBar::sub-page {background:transparent;}
QMenu {background:@surface;border:1px solid @border;padding:6px;border-radius:6px;}
QMenu::item {padding:9px 20px;border-radius:4px;} QMenu::item:selected {background:@selection;color:@accent;}
QMenu::separator {height:1px;background:@border;margin:4px;}
QToolTip {background:@surface;color:@text;border:1px solid @border;padding:5px;}
QSplitter::handle {background:@background;} QSplitter::handle:hover {background:@border;}
QFrame[separator="true"] {background:@border;max-height:1px;border:0;}
QWizard {background:@background;} QTabWidget::pane {border:1px solid @border;}
QTabBar::tab {background:@surface;padding:10px 18px;border-bottom:2px solid @border;}
QTabBar::tab:selected {color:@accent;border-bottom-color:@accent;}
QGroupBox {border:1px solid @border;border-radius:8px;margin-top:16px;padding:14px;}
QGroupBox::title {subcontrol-origin:margin;left:12px;padding:0 5px;color:@muted;}
)";
    const QList<QPair<QString, QString>> tokens{
        {"background", c.background}, {"surface", c.surface},  {"input", c.input},   {"text", c.text},
        {"muted", c.muted},           {"border", c.border},    {"accent", c.accent}, {"hover", c.hover},
        {"selection", c.selection},   {"disabled", c.disabled}, {"onAccent", c.onAccent}};
    for (const auto &token : tokens)
        css.replace("@" + token.first, token.second);
    const bool light = QColor(c.background).lightness() > 128;
    css.replace("@checkIcon", QColor(c.onAccent).lightness() < 128 ? "check-dark.svg" : "check.svg");
    css.replace("@upIcon", light ? "up-pink.svg" : "up-dark.svg");
    css.replace("@downIcon", light ? "down-pink.svg" : "down-dark.svg");
    return css;
}
void apply(QWidget *root, const QString &name) {
    root->setProperty("uiTheme", normalized(name));
    root->setPalette(palette(name));
    root->setStyleSheet(styleSheet(name));
    root->update();
}
} // namespace UiTheme
