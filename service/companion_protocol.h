#pragma once

#include <QByteArray>
#include <QString>

namespace omaflip {
constexpr int kCompanionFrameMax = 256;
constexpr int kCompanionHeaderSize = 10;

enum class CompanionMessage : quint8 {
    Hello = 1,
    HelloAck = 2,
    StateSnapshot = 3,
    ActionRequest = 4,
    ActionResult = 5,
    Error = 6,
    Ping = 7,
    Pong = 8,
};

struct CompanionFrame {
    CompanionMessage type = CompanionMessage::Error;
    quint8 flags = 0;
    quint16 requestId = 0;
    QByteArray payload;
};

QByteArray encodeCompanionFrame(
    CompanionMessage type, quint8 flags, quint16 requestId, const QByteArray& payload = {});
bool decodeCompanionFrame(const QByteArray& bytes, CompanionFrame& frame, QString& error);
QByteArray companionText(const QString& text, int maxBytes);
}
