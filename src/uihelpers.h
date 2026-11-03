#pragma once
#include <QColor>
#include <QIcon>
#include <QLabel>
#include <QPalette>
#include <QPushButton>
#include <QVariantMap>
namespace Ui {
QPushButton *action(const QString &text, const QString &name = {});
QLabel *label(const QString &text, const QString &name = {});
QString trackTitle(const QVariantMap &track);
QIcon navigationIcon(int kind, const QPalette &palette);
QIcon downloadActionIcon(int type, const QPalette &palette, qreal ratio);
QIcon transportIcon(int kind, const QColor &color, qreal ratio);
QIcon emptyCover(const QPalette &palette);
QString colorSwatchStyle(const QColor &color);
}
