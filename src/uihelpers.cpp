#include "uihelpers.h"
#include <QApplication>
#include <QPainter>
namespace Ui {
QPushButton *action(const QString &text, const QString &name) {
    auto b = new QPushButton(text);
    b->setObjectName(name);
    if (name == "primary")
        b->setProperty("primary", true);
    b->setCursor(Qt::PointingHandCursor);
    return b;
}
QLabel *label(const QString &text, const QString &name) {
    auto l = new QLabel(text);
    l->setObjectName(name);
    l->setTextFormat(Qt::PlainText);
    return l;
}
QIcon navigationIcon(int kind, const QPalette &palette) {
    QIcon icon;
    for (bool active : {false, true}) {
        QPixmap pixmap(QSize(20, 20) * qApp->devicePixelRatio());
        pixmap.setDevicePixelRatio(qApp->devicePixelRatio());
        pixmap.fill(Qt::transparent);
        QPainter p(&pixmap);
        p.setRenderHint(QPainter::Antialiasing);
        const QColor color(active ? palette.color(QPalette::HighlightedText)
                                  : palette.color(QPalette::WindowText));
        p.setPen(QPen(color, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        if (kind == 0) {
            p.drawEllipse(QRectF(3, 3, 10, 10));
            p.drawLine(QPointF(12, 12), QPointF(17, 17));
        } else if (kind == 1) {
            p.drawLine(QPointF(7, 14), QPointF(7, 5));
            p.drawLine(QPointF(7, 5), QPointF(15, 3));
            p.drawLine(QPointF(15, 3), QPointF(15, 12));
            p.setBrush(color);
            p.drawEllipse(QRectF(3, 13, 4, 3));
            p.drawEllipse(QRectF(11, 11, 4, 3));
        } else if (kind == 2) {
            p.drawPolygon(QPolygonF{QPointF(5, 3), QPointF(16, 10), QPointF(5, 17)});
        } else if (kind == 3) {
            p.drawRoundedRect(QRectF(2, 3, 16, 14), 1, 1);
            p.drawLine(QPointF(5, 7), QPointF(15, 7));
            p.drawLine(QPointF(5, 10), QPointF(12, 10));
            p.drawLine(QPointF(5, 13), QPointF(14, 13));
        } else {
            p.drawEllipse(QRectF(5, 5, 10, 10));
            p.drawEllipse(QRectF(8, 8, 4, 4));
            p.translate(10, 10);
            for (int i = 0; i < 8; ++i) {
                p.drawLine(QPointF(0, 6), QPointF(0, 8));
                p.rotate(45);
            }
        }
        p.end();
        icon.addPixmap(pixmap, QIcon::Normal, active ? QIcon::On : QIcon::Off);
    }
    return icon;
}
QIcon downloadActionIcon(int type, const QPalette &palette, qreal ratio) {
    if (type == 2)
        return navigationIcon(3, palette);
    QPixmap pixmap(QSize(20, 20) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(palette.color(QPalette::ButtonText), 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    p.setBrush(Qt::NoBrush);
    if (type == 0) {
        p.drawLine(QPointF(5, 5), QPointF(15, 15));
        p.drawLine(QPointF(5, 15), QPointF(15, 5));
    } else {
        p.drawRoundedRect(QRectF(2, 2, 16, 16), 1.5, 1.5);
        p.drawEllipse(QRectF(4, 7, 10, 10));
        p.drawEllipse(QRectF(8, 11, 2, 2));
        p.drawPolyline(QPolygonF{QPointF(16, 5), QPointF(16, 10), QPointF(13, 14)});
    }
    return QIcon(pixmap);
}
QIcon transportIcon(int kind, const QColor &color, qreal ratio) {
    QPixmap pixmap(QSize(24, 24) * ratio);
    pixmap.setDevicePixelRatio(ratio);
    pixmap.fill(Qt::transparent);
    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    if (kind == 3) {
        p.drawRoundedRect(QRectF(6, 5, 4, 14), 1, 1);
        p.drawRoundedRect(QRectF(14, 5, 4, 14), 1, 1);
    } else if (kind == 1) {
        p.drawPolygon(QPolygonF{QPointF(8, 4), QPointF(20, 12), QPointF(8, 20)});
    } else {
        if (kind == 0) {
            p.translate(24, 0);
            p.scale(-1, 1);
        }
        p.drawPolygon(QPolygonF{QPointF(5, 5), QPointF(16, 12), QPointF(5, 19)});
        p.drawRoundedRect(QRectF(17, 5, 3, 14), 1, 1);
    }
    return QIcon(pixmap);
}
QIcon emptyCover(const QPalette &palette) {
    QPixmap cover(96, 54);
    cover.fill(palette.color(QPalette::Button));
    QPainter p(&cover);
    p.setPen(palette.color(QPalette::PlaceholderText));
    auto font = p.font();
    font.setPixelSize(24);
    p.setFont(font);
    p.drawText(cover.rect(), Qt::AlignCenter, "♫");
    return QIcon(cover);
}
QString colorSwatchStyle(const QColor &color) {
    return "background:" + color.name() + ";color:" + (color.lightness() > 145 ? "#182334;" : "#ffffff;");
}

QString trackTitle(const QVariantMap &track) {
    return track.value("title").toString() + (track.value("storage").toString() == "url" ? "  [URL]" : "");
}
} // namespace Ui
