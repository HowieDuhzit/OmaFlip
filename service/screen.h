#pragma once
#include <QByteArray>
#include <QImage>
#include <QString>

namespace omaflip {
constexpr int kScreenWidth = 128;
constexpr int kScreenHeight = 64;
constexpr int kScreenBytes = 1024;

QImage decodeScreen(const QByteArray& frame, int orientation);
QByteArray encodePng(const QImage& image);
QString saveScreenshot(const QImage& image, QString& error);
}
