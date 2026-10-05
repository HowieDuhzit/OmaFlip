#include "companion_protocol.h"

namespace omaflip {
namespace {
quint16 readU16(const char* data) {
    return static_cast<quint8>(data[0]) | (static_cast<quint16>(static_cast<quint8>(data[1])) << 8);
}
void appendU16(QByteArray& bytes, quint16 value) {
    bytes.append(static_cast<char>(value & 0xff));
    bytes.append(static_cast<char>((value >> 8) & 0xff));
}
}

QByteArray encodeCompanionFrame(
    CompanionMessage type, quint8 flags, quint16 requestId, const QByteArray& payload) {
    if(payload.size() > kCompanionFrameMax - kCompanionHeaderSize) return {};
    QByteArray out;
    out.reserve(kCompanionHeaderSize + payload.size());
    out.append("FA", 2);
    out.append(char(1));
    out.append(char(0));
    out.append(static_cast<char>(type));
    out.append(static_cast<char>(flags));
    appendU16(out, requestId);
    appendU16(out, static_cast<quint16>(payload.size()));
    out.append(payload);
    return out;
}

bool decodeCompanionFrame(const QByteArray& bytes, CompanionFrame& frame, QString& error) {
    if(bytes.size() < kCompanionHeaderSize) { error = "Frame is shorter than its header."; return false; }
    if(bytes.size() > kCompanionFrameMax) { error = "Frame exceeds 256 bytes."; return false; }
    if(bytes[0] != 'F' || bytes[1] != 'A') { error = "Frame magic is invalid."; return false; }
    if(static_cast<quint8>(bytes[2]) != 1) { error = "Protocol major version is unsupported."; return false; }
    const auto payloadSize = readU16(bytes.constData() + 8);
    if(bytes.size() != kCompanionHeaderSize + payloadSize) {
        error = "Frame payload length does not match its header."; return false;
    }
    frame.type = static_cast<CompanionMessage>(static_cast<quint8>(bytes[4]));
    frame.flags = static_cast<quint8>(bytes[5]);
    frame.requestId = readU16(bytes.constData() + 6);
    frame.payload = bytes.mid(kCompanionHeaderSize, payloadSize);
    error.clear();
    return true;
}

QByteArray companionText(const QString& text, int maxBytes) {
    auto bytes = text.toUtf8();
    if(bytes.size() <= maxBytes) return bytes;
    bytes.truncate(maxBytes);
    int start = bytes.size() - 1;
    while(start > 0 && (static_cast<quint8>(bytes[start]) & 0xc0) == 0x80) --start;
    const auto lead = static_cast<quint8>(bytes[start]);
    const int needed = (lead & 0x80) == 0 ? 1 : (lead & 0xe0) == 0xc0 ? 2 :
        (lead & 0xf0) == 0xe0 ? 3 : (lead & 0xf8) == 0xf0 ? 4 : 1;
    if(bytes.size() - start < needed) bytes.truncate(start);
    return bytes;
}
}
