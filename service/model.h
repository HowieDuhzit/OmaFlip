#pragma once
#include <QJsonObject>
#include <QString>
#include <QStringList>
#include <optional>

namespace omaflip {
enum class State { Disconnected, Detecting, Connecting, Connected, Busy, Updating, Rebooting, Bootloader, Error };
QString stateName(State state);
bool allowedTransition(State from, State to);
struct UsbIdentity {
    QString vendor, product, manufacturer, description, serial, syspath, port;
    bool operator==(const UsbIdentity&) const = default;
};
enum class UsbKind { Other, FlipperSerial, DfuCandidate };
UsbKind classify(const UsbIdentity& usb);
QString stableId(const UsbIdentity& usb);
QJsonObject systemError(const QString& operation, const QString& path, int error);
QString cleanTerminal(const QByteArray& bytes);
QJsonObject parseInfo(const QByteArray& bytes);
std::optional<QByteArray> takeCliResponse(QByteArray& buffer);
QStringList desktopNotifyArgs(const QString& category, const QString& urgency, const QString& title, const QString& body);
bool desktopNotifyCategoryAllowed(const QString& category);
void desktopNotify(const QString& category, const QString& urgency, const QString& title, const QString& body);
}
