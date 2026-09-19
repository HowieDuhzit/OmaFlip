#pragma once
#include "flipper.pb.h"
#include <QByteArray>
#include <QJsonObject>
#include <QString>
#include <QVector>

namespace omaflip {
constexpr int kMaxRpcFrame = 16384;

enum class FrameStatus { NeedMore, Ok, TooLarge, Corrupt };

QByteArray encodeDelimited(const PB::Main& message);
FrameStatus takeDelimited(QByteArray& buffer, PB::Main& message);

constexpr int kStorageChunk = 512;

PB::Main pingRequest(quint32 id, const QByteArray& data);
PB::Main protobufVersionRequest(quint32 id);
PB::Main storageInfoRequest(quint32 id, const QString& path);
PB::Main storageListRequest(quint32 id, const QString& path);
PB::Main storageStatRequest(quint32 id, const QString& path);
PB::Main storageReadRequest(quint32 id, const QString& path);
PB::Main storageWriteRequest(quint32 id, const QString& path, const QByteArray& data, bool hasNext);
PB::Main storageMkdirRequest(quint32 id, const QString& path);
PB::Main storageRenameRequest(quint32 id, const QString& from, const QString& to);
PB::Main storageDeleteRequest(quint32 id, const QString& path, bool recursive);
QVector<PB::Main> storageWriteChunks(quint32 id, const QString& path, const QByteArray& data, int chunkSize = kStorageChunk);
PB::Main startStreamRequest(quint32 id);
PB::Main stopStreamRequest(quint32 id);
PB::Main stopSessionRequest(quint32 id);
PB::Main inputRequest(quint32 id, PB_Gui::InputKey key, PB_Gui::InputType type);
PB::Main appStartRequest(quint32 id, const QString& name, const QString& args);
PB::Main appExitRequest(quint32 id);
PB::Main appLockStatusRequest(quint32 id);
PB::Main propertyGetRequest(quint32 id, const QString& key);
PB::Main systemDeviceInfoRequest(quint32 id);
PB::Main systemPowerInfoRequest(quint32 id);
PB::Main systemGetDateTimeRequest(quint32 id);
PB::Main systemPlayAudiovisualAlertRequest(quint32 id);
PB::Main desktopIsLockedRequest(quint32 id);
PB::Main storageBackupCreateRequest(quint32 id, const QString& path);
PB::Main storageBackupRestoreRequest(quint32 id, const QString& path);
PB::Main storageTarExtractRequest(quint32 id, const QString& tar, const QString& out);
PB::Main systemUpdateRequest(quint32 id, const QString& manifest);
PB::Main systemRebootRequest(quint32 id, PB_System::RebootRequest_RebootMode mode);

QString commandStatusName(PB::CommandStatus status);
QString bytesText(quint64 bytes);
QJsonObject rpcSummary(bool ping, quint32 major, quint32 minor,
    bool extPresent, quint64 extTotal, quint64 extFree,
    bool intPresent, quint64 intTotal, quint64 intFree,
    const QString& error);
}
