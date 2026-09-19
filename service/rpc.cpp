#include "rpc.h"
#include <QVector>

namespace omaflip {
namespace {
bool readVarint32(const QByteArray& buffer, int& offset, quint32& value) {
    value = 0;
    for(int i = 0; i < 5; ++i) {
        if(offset + i >= buffer.size()) return false;
        const auto byte = static_cast<quint8>(buffer[offset + i]);
        value |= static_cast<quint32>(byte & 0x7f) << (7 * i);
        if((byte & 0x80) == 0) {
            offset += i + 1;
            return true;
        }
    }
    offset += 5;
    return false;
}
}

QByteArray encodeDelimited(const PB::Main& message) {
    const int size = static_cast<int>(message.ByteSizeLong());
    QByteArray payload(size, Qt::Uninitialized);
    if(size > 0 && !message.SerializeToArray(payload.data(), size)) return {};
    QByteArray header;
    quint32 value = static_cast<quint32>(size);
    do {
        quint8 byte = value & 0x7f;
        value >>= 7;
        if(value) byte |= 0x80;
        header.append(static_cast<char>(byte));
    } while(value);
    return header + payload;
}

FrameStatus takeDelimited(QByteArray& buffer, PB::Main& message) {
    if(buffer.isEmpty()) return FrameStatus::NeedMore;
    int offset = 0;
    quint32 size = 0;
    const int before = offset;
    if(!readVarint32(buffer, offset, size)) {
        if(offset - before >= 5 && buffer.size() >= 5) return FrameStatus::Corrupt;
        return FrameStatus::NeedMore;
    }
    if(size > static_cast<quint32>(kMaxRpcFrame)) return FrameStatus::TooLarge;
    if(buffer.size() - offset < static_cast<int>(size)) return FrameStatus::NeedMore;
    if(!message.ParseFromArray(buffer.constData() + offset, static_cast<int>(size)))
        return FrameStatus::Corrupt;
    buffer.remove(0, offset + static_cast<int>(size));
    return FrameStatus::Ok;
}

PB::Main pingRequest(quint32 id, const QByteArray& data) {
    PB::Main message;
    message.set_command_id(id);
    message.mutable_system_ping_request()->set_data(data.toStdString());
    return message;
}

PB::Main protobufVersionRequest(quint32 id) {
    PB::Main message;
    message.set_command_id(id);
    message.mutable_system_protobuf_version_request();
    return message;
}

PB::Main storageInfoRequest(quint32 id, const QString& path) {
    PB::Main message;
    message.set_command_id(id);
    message.mutable_storage_info_request()->set_path(path.toStdString());
    return message;
}

PB::Main storageListRequest(quint32 id, const QString& path) {
    PB::Main message;
    message.set_command_id(id);
    message.mutable_storage_list_request()->set_path(path.toStdString());
    return message;
}

PB::Main storageStatRequest(quint32 id, const QString& path) {
    PB::Main message;
    message.set_command_id(id);
    message.mutable_storage_stat_request()->set_path(path.toStdString());
    return message;
}

PB::Main storageReadRequest(quint32 id, const QString& path) {
    PB::Main message;
    message.set_command_id(id);
    message.mutable_storage_read_request()->set_path(path.toStdString());
    return message;
}

PB::Main storageWriteRequest(quint32 id, const QString& path, const QByteArray& data, bool hasNext) {
    PB::Main message;
    message.set_command_id(id);
    message.set_has_next(hasNext);
    auto* write = message.mutable_storage_write_request();
    write->set_path(path.toStdString());
    write->mutable_file()->set_data(data.toStdString());
    return message;
}

PB::Main storageMkdirRequest(quint32 id, const QString& path) {
    PB::Main message;
    message.set_command_id(id);
    message.mutable_storage_mkdir_request()->set_path(path.toStdString());
    return message;
}

PB::Main storageRenameRequest(quint32 id, const QString& from, const QString& to) {
    PB::Main message;
    message.set_command_id(id);
    auto* rename = message.mutable_storage_rename_request();
    rename->set_old_path(from.toStdString());
    rename->set_new_path(to.toStdString());
    return message;
}

PB::Main storageDeleteRequest(quint32 id, const QString& path, bool recursive) {
    PB::Main message;
    message.set_command_id(id);
    auto* request = message.mutable_storage_delete_request();
    request->set_path(path.toStdString());
    request->set_recursive(recursive);
    return message;
}

QVector<PB::Main> storageWriteChunks(quint32 id, const QString& path, const QByteArray& data, int chunkSize) {
    QVector<PB::Main> out;
    if(chunkSize <= 0) chunkSize = kStorageChunk;
    if(data.isEmpty()) {
        out.append(storageWriteRequest(id, path, {}, false));
        return out;
    }
    for(int offset = 0; offset < data.size();) {
        const int n = qMin(chunkSize, data.size() - offset);
        out.append(storageWriteRequest(id, path, data.mid(offset, n), offset + n < data.size()));
        offset += n;
    }
    return out;
}

PB::Main startStreamRequest(quint32 id) {
    PB::Main message;
    message.set_command_id(id);
    message.mutable_gui_start_screen_stream_request();
    return message;
}

PB::Main stopStreamRequest(quint32 id) {
    PB::Main message;
    message.set_command_id(id);
    message.mutable_gui_stop_screen_stream_request();
    return message;
}

PB::Main stopSessionRequest(quint32 id) {
    PB::Main message;
    message.set_command_id(id);
    message.mutable_stop_session();
    return message;
}

PB::Main inputRequest(quint32 id, PB_Gui::InputKey key, PB_Gui::InputType type) {
    PB::Main message;
    message.set_command_id(id);
    auto* event = message.mutable_gui_send_input_event_request();
    event->set_key(key);
    event->set_type(type);
    return message;
}

PB::Main appStartRequest(quint32 id, const QString& name, const QString& args) {
    PB::Main message;
    message.set_command_id(id);
    auto* request = message.mutable_app_start_request();
    request->set_name(name.toStdString());
    if(!args.isEmpty()) request->set_args(args.toStdString());
    return message;
}

PB::Main appExitRequest(quint32 id) {
    PB::Main message;
    message.set_command_id(id);
    message.mutable_app_exit_request();
    return message;
}

PB::Main appLockStatusRequest(quint32 id) {
    PB::Main message;
    message.set_command_id(id);
    message.mutable_app_lock_status_request();
    return message;
}

PB::Main propertyGetRequest(quint32 id, const QString& key) {
    PB::Main message;
    message.set_command_id(id);
    message.mutable_property_get_request()->set_key(key.toStdString());
    return message;
}

PB::Main systemDeviceInfoRequest(quint32 id) {
    PB::Main message;
    message.set_command_id(id);
    message.mutable_system_device_info_request();
    return message;
}

PB::Main systemPowerInfoRequest(quint32 id) {
    PB::Main message;
    message.set_command_id(id);
    message.mutable_system_power_info_request();
    return message;
}

PB::Main systemGetDateTimeRequest(quint32 id) {
    PB::Main message;
    message.set_command_id(id);
    message.mutable_system_get_datetime_request();
    return message;
}

PB::Main systemPlayAudiovisualAlertRequest(quint32 id) {
    PB::Main message;
    message.set_command_id(id);
    message.mutable_system_play_audiovisual_alert_request();
    return message;
}

PB::Main desktopIsLockedRequest(quint32 id) {
    PB::Main message;
    message.set_command_id(id);
    message.mutable_desktop_is_locked_request();
    return message;
}

PB::Main storageBackupCreateRequest(quint32 id, const QString& path) {
    PB::Main message;
    message.set_command_id(id);
    message.mutable_storage_backup_create_request()->set_archive_path(path.toStdString());
    return message;
}

PB::Main storageBackupRestoreRequest(quint32 id, const QString& path) {
    PB::Main message;
    message.set_command_id(id);
    message.mutable_storage_backup_restore_request()->set_archive_path(path.toStdString());
    return message;
}

PB::Main storageTarExtractRequest(quint32 id, const QString& tar, const QString& out) {
    PB::Main message;
    message.set_command_id(id);
    auto* request = message.mutable_storage_tar_extract_request();
    request->set_tar_path(tar.toStdString());
    request->set_out_path(out.toStdString());
    return message;
}

PB::Main systemUpdateRequest(quint32 id, const QString& manifest) {
    PB::Main message;
    message.set_command_id(id);
    message.mutable_system_update_request()->set_update_manifest(manifest.toStdString());
    return message;
}

PB::Main systemRebootRequest(quint32 id, PB_System::RebootRequest_RebootMode mode) {
    PB::Main message;
    message.set_command_id(id);
    message.mutable_system_reboot_request()->set_mode(mode);
    return message;
}

QString commandStatusName(PB::CommandStatus status) {
    const auto name = PB::CommandStatus_Name(status);
    return name.empty() ? QString::number(static_cast<int>(status)) : QString::fromStdString(name);
}

QString bytesText(quint64 bytes) {
    if(bytes < 1024) return QString::number(bytes) + " B";
    if(bytes < 1024ull * 1024) return QString::number(bytes / 1024.0, 'f', 1) + " KiB";
    if(bytes < 1024ull * 1024 * 1024) return QString::number(bytes / (1024.0 * 1024.0), 'f', 1) + " MiB";
    return QString::number(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 1) + " GiB";
}

QJsonObject rpcSummary(bool ping, quint32 major, quint32 minor,
    bool extPresent, quint64 extTotal, quint64 extFree,
    bool intPresent, quint64 intTotal, quint64 intFree,
    const QString& error) {
    QJsonObject out{{"available", error.isEmpty() && ping},
        {"ping", ping},
        {"protobuf", QString::number(major) + "." + QString::number(minor)}};
    if(extPresent) {
        out.insert("storage_ext", QJsonObject{{"total", static_cast<qint64>(extTotal)}, {"free", static_cast<qint64>(extFree)}});
        out.insert("storage", bytesText(extFree) + " free of " + bytesText(extTotal));
    }
    if(intPresent) out.insert("storage_int", QJsonObject{{"total", static_cast<qint64>(intTotal)}, {"free", static_cast<qint64>(intFree)}});
    if(!error.isEmpty()) out.insert("reason", error);
    QString summary;
    if(!error.isEmpty() && !ping) summary = error;
    else {
        summary = "Protobuf " + out["protobuf"].toString();
        if(extPresent) summary += " · SD " + bytesText(extFree) + " free of " + bytesText(extTotal);
        else if(ping) summary += " · SD unavailable";
        if(!error.isEmpty()) summary += " · " + error;
    }
    out.insert("summary", summary);
    return out;
}
}
