#include "screen.h"
#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QStandardPaths>
#include <QTransform>

namespace omaflip {
QImage decodeScreen(const QByteArray& frame, int orientation) {
    if(frame.size() < kScreenBytes) return {};
    QImage image(kScreenWidth, kScreenHeight, QImage::Format_RGB32);
    const auto* data = reinterpret_cast<const quint8*>(frame.constData());
    const QRgb on = qRgb(220, 220, 220);
    const QRgb off = qRgb(12, 12, 12);
    for(int y = 0; y < kScreenHeight; ++y) {
        auto* line = reinterpret_cast<QRgb*>(image.scanLine(y));
        for(int x = 0; x < kScreenWidth; ++x) {
            const int index = (y / 8) * kScreenWidth + x;
            line[x] = (data[index] & (1 << (y & 7))) ? on : off;
        }
    }
    if(orientation == 1) image = image.transformed(QTransform().rotate(180));
    else if(orientation == 2) image = image.transformed(QTransform().rotate(90));
    else if(orientation == 3) image = image.transformed(QTransform().rotate(270));
    return image;
}

QByteArray encodePng(const QImage& image) {
    if(image.isNull()) return {};
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return png;
}

QString saveScreenshot(const QImage& image, QString& error) {
    if(image.isNull()) { error = "No screen frame has been received yet."; return {}; }
    const auto pictures = QStandardPaths::writableLocation(QStandardPaths::PicturesLocation);
    if(pictures.isEmpty()) { error = "No Pictures directory is available."; return {}; }
    QDir dir(pictures + "/OmaFlip");
    if(!dir.exists() && !dir.mkpath(".")) { error = "Could not create the OmaFlip screenshot folder."; return {}; }
    const auto name = "omaflip-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".png";
    const auto path = dir.filePath(name);
    if(!image.save(path, "PNG")) { error = "Could not write the PNG screenshot."; return {}; }
    return path;
}
}
