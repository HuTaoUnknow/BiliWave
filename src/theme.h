#pragma once
#include <QPalette>
#include <QString>
#include <QList>
#include <QPair>
class QWidget;
namespace UiTheme {
QString normalized(const QString &name);
QList<QPair<QString, QString>> choices();
QPalette palette(const QString &name);
QPalette forWidget(const QWidget *widget);
QString styleSheet(const QString &name);
void apply(QWidget *root, const QString &name);
} // namespace UiTheme
