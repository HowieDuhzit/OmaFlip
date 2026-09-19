#include "model.h"
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <cerrno>
#include <cstring>

namespace omaflip {
QString stateName(State state) {
    switch(state) {
    case State::Disconnected: return "Disconnected";
    case State::Detecting: return "Detecting";
    case State::Connecting: return "Connecting";
    case State::Connected: return "Connected";
    case State::Busy: return "Busy";
    case State::Updating: return "Updating";
    case State::Rebooting: return "Rebooting";
    case State::Bootloader: return "Bootloader";
    case State::Error: return "Error";
    }
    Q_UNREACHABLE();
}
bool allowedTransition(State from, State to) {
    if(from == to || to == State::Disconnected || to == State::Error) return true;
    switch(from) {
    case State::Disconnected: return to == State::Detecting;
    case State::Detecting: return to == State::Connecting || to == State::Bootloader;
    case State::Connecting: return to == State::Connected || to == State::Busy;
    case State::Connected: return to == State::Connecting || to == State::Busy || to == State::Updating || to == State::Rebooting;
    case State::Busy: return to == State::Connecting || to == State::Connected;
    case State::Updating: return to == State::Rebooting;
    case State::Rebooting: return to == State::Detecting || to == State::Bootloader;
    case State::Bootloader: return to == State::Detecting;
    case State::Error: return to == State::Connecting || to == State::Detecting;
    }
    return false;
}
UsbKind classify(const UsbIdentity& usb) {
    if(usb.vendor.toLower() != "0483") return UsbKind::Other;
    // Custom firmware may expose the configured device name as the USB product
    // string. VID/PID plus Flipper's manufacturer descriptor stays stable.
    if(usb.product.toLower() == "5740" && usb.manufacturer == "Flipper Devices Inc.")
        return UsbKind::FlipperSerial;
    // STM32 ROM bootloader identifiers are shared; never claim this proves a Flipper.
    if(usb.product.toLower() == "df11") return UsbKind::DfuCandidate;
    return UsbKind::Other;
}
QString stableId(const UsbIdentity& usb) {
    const auto prefix = classify(usb) == UsbKind::DfuCandidate ? "stm32-dfu:" : "flipper:";
    return prefix + (usb.serial.isEmpty() ? "port:" + usb.syspath : usb.serial);
}
QJsonObject systemError(const QString& operation, const QString& path, int error) {
    QString code = "io", fix = "Reconnect the device, then choose Retry. Inspect device diagnostics if this persists.";
    if(error == EACCES || error == EPERM) {
        code = "permission_denied";
        fix = "Install the OmaFlip udev rule using Setup Device Access, then reconnect the device.";
    } else if(error == EBUSY || error == EWOULDBLOCK) {
        code = "port_busy";
        fix = "Close qFlipper or the other serial client, then choose Retry.";
    }
    return {{"code", code}, {"operation", operation}, {"path", path},
            {"reason", QString::fromLocal8Bit(std::strerror(error))}, {"suggestion", fix}};
}
QString cleanTerminal(const QByteArray& bytes) {
    QString text = QString::fromUtf8(bytes);
    static const QRegularExpression ansi("\x1b\\[[0-?]*[ -/]*[@-~]");
    text.remove(ansi);
    text.remove('\r');
    return text;
}
QJsonObject parseInfo(const QByteArray& bytes) {
    QJsonObject out;
    static const QRegularExpression linePattern("^([a-zA-Z][a-zA-Z0-9_.]*)\\s*:\\s*(.*?)\\s*$");
    const auto lines = cleanTerminal(bytes).split('\n');
    for(const auto& line : lines) {
        auto match = linePattern.match(line);
        if(match.hasMatch()) {
            QString key = match.captured(1);
            key.replace('.', '_');
            out.insert(key, match.captured(2));
        }
    }
    return out;
}
std::optional<QByteArray> takeCliResponse(QByteArray& buffer) {
    // Current and legacy firmware both terminate the main CLI prompt with >: .
    // Require a complete prompt at the end, never a substring in a value.
    const QString clean = cleanTerminal(buffer);
    if(!clean.endsWith(">: ")) return std::nullopt;
    const auto line = clean.mid(clean.lastIndexOf('\n') + 1);
    if(line != ">: ") return std::nullopt;
    QByteArray result = buffer;
    buffer.clear();
    return result;
}

bool desktopNotifyCategoryAllowed(const QString& category) {
    return category == "device.added" || category == "device.removed" || category == "device.error";
}

QStringList desktopNotifyArgs(const QString& category, const QString& urgency, const QString& title, const QString& body) {
    if(!desktopNotifyCategoryAllowed(category) || title.isEmpty()) return {};
    const auto level = urgency == "critical" || urgency == "low" ? urgency : QString("normal");
    return {"--app-name=OmaFlip", "--urgency=" + level, "--category=" + category,
        "--expire-time=5000", "--", title, body};
}

void desktopNotify(const QString& category, const QString& urgency, const QString& title, const QString& body) {
    const auto args = desktopNotifyArgs(category, urgency, title, body);
    if(args.isEmpty()) return;
    const auto binary = QStandardPaths::findExecutable("notify-send");
    if(binary.isEmpty()) return;
    QProcess::startDetached(binary, args);
}
}
