#include "window.h"
#include <QApplication>
#include <QEvent>
#include <QImage>
#include <QPainter>
#include <QScreen>
#include <QShowEvent>
#include <QtMath>

FloatBar::FloatBar(Player *player, QSettings *settings)
    : QWidget(nullptr, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint |
                           Qt::WindowTransparentForInput | Qt::WindowDoesNotAcceptFocus),
      m_player(player), m_settings(settings) {
    setObjectName("floatBar");
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setFocusPolicy(Qt::NoFocus);
    setAcceptDrops(false);
    connect(player->media(), &QMediaPlayer::positionChanged, this, &FloatBar::refresh);
    connect(player->media(), &QMediaPlayer::durationChanged, this, &FloatBar::refresh);
    connect(player, &Player::currentChanged, this, &FloatBar::refresh);
    connect(player, &Player::queueChanged, this, &FloatBar::refresh);
    connect(qApp, &QGuiApplication::primaryScreenChanged, this, [this] { anchorToPrimaryScreen(); });
    applySettings();
}
void FloatBar::applySettings() {
    m_leftColor = QColor(m_settings->value("progress/leftColor", "#ff3355").toString());
    m_rightColor = QColor(m_settings->value("progress/rightColor", "#327dff").toString());
    if (!m_leftColor.isValid())
        m_leftColor = QColor("#ff3355");
    if (!m_rightColor.isValid())
        m_rightColor = QColor("#327dff");
    m_leftColor.setAlpha(255);
    m_rightColor.setAlpha(255);
    m_fadeHeight = qBound(1, m_settings->value("progress/fadeHeight", 24).toInt(), 512);
    m_gradientWidth = qBound(0, m_settings->value("progress/gradientWidth", 80).toInt(), 4096);
    m_queueMarkerColor = QColor(m_settings->value("progress/queueMarkerColor", "#ffffff").toString());
    if (!m_queueMarkerColor.isValid())
        m_queueMarkerColor = Qt::white;
    m_queueMarkerColor.setAlpha(255);
    m_queueMarkerLength = qBound(1, m_settings->value("progress/queueMarkerLength", 10).toInt(), 128);
    m_queueMarkerLimit = qBound(0, m_settings->value("progress/queueMarkerLimit", 10).toInt(), 10);
    anchorToPrimaryScreen();
    update();
}
void FloatBar::anchorToPrimaryScreen() {
    auto screen = QGuiApplication::primaryScreen();
    if (!screen)
        return;
    if (screen != m_screen) {
        if (m_screen)
            disconnect(m_screen, nullptr, this, nullptr);
        m_screen = screen;
        connect(screen, &QScreen::geometryChanged, this, [this] { anchorToPrimaryScreen(); });
        connect(screen, &QScreen::logicalDotsPerInchChanged, this, [this] { anchorToPrimaryScreen(); });
        connect(screen, &QScreen::physicalDotsPerInchChanged, this, [this] { anchorToPrimaryScreen(); });
    }
    const auto geometry = screen->geometry();
    // The setting describes physical pixels, including on 125% / 150% display scaling.
    const auto dpr = screen->devicePixelRatio();
    setFixedSize(geometry.width(), qMin(geometry.height(), qMax(1, qCeil(m_fadeHeight / dpr))));
    move(geometry.topLeft());
    update();
}
bool FloatBar::event(QEvent *event) {
    if (event->type() == QEvent::DevicePixelRatioChange)
        QTimer::singleShot(0, this, [this] { anchorToPrimaryScreen(); });
    return QWidget::event(event);
}
void FloatBar::showEvent(QShowEvent *event) {
    anchorToPrimaryScreen();
    QWidget::showEvent(event);
}
void FloatBar::refresh() {
    update();
}
void FloatBar::paintEvent(QPaintEvent *) {
    QPainter painter(this);
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    painter.fillRect(rect(), Qt::transparent);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    const auto duration = m_player->media()->duration();
    const qreal ratio = duration > 0 ? qBound(0., double(m_player->media()->position()) / duration, 1.) : 0.;
    const qreal dpr = devicePixelRatioF();
    const int pixelWidth = qMax(1, qRound(width() * dpr));
    const int pixelHeight = qMin(m_fadeHeight, qMax(1, qRound(height() * dpr)));
    const int gap = 4;
    const int markerCount = qMin(qMin(m_queueMarkerLimit, int(m_player->pendingEntries().size())),
                                 pixelWidth / 2 / (m_queueMarkerLength + gap));
    const int musicWidth = pixelWidth - markerCount * (m_queueMarkerLength + gap);
    const int played = qBound(0, qRound(musicWidth * ratio), musicWidth);
    QImage strip(pixelWidth, pixelHeight, QImage::Format_ARGB32_Premultiplied);
    strip.fill(Qt::transparent);
    {
        QPainter layer(&strip);
        if (m_gradientWidth > 0 && ratio > 0 && ratio < 1) {
            QLinearGradient transition(played - m_gradientWidth / 2., 0, played + m_gradientWidth / 2., 0);
            transition.setColorAt(0, m_leftColor);
            transition.setColorAt(1, m_rightColor);
            layer.fillRect(QRect(0, 0, musicWidth, pixelHeight), transition);
        } else {
            layer.fillRect(QRect(0, 0, musicWidth, pixelHeight), m_rightColor);
            layer.fillRect(QRect(0, 0, played, pixelHeight), m_leftColor);
        }
        for (int i = 0; i < markerCount; ++i)
            layer.fillRect(QRect(musicWidth + gap + i * (m_queueMarkerLength + gap), 0, m_queueMarkerLength,
                                 pixelHeight),
                           m_queueMarkerColor);
        if (pixelHeight > 1) {
            QLinearGradient fade(0, 0, 0, pixelHeight - 1);
            fade.setColorAt(0, Qt::white);
            fade.setColorAt(1, Qt::transparent);
            layer.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            layer.fillRect(strip.rect(), fade);
        }
    }
    strip.setDevicePixelRatio(dpr);
    painter.drawImage(QPointF(0, 0), strip);
}
