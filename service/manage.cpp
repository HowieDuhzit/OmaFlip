#include "manage.h"
#include "model.h"
#include <QDateTime>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSocketNotifier>
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace omaflip {
ManageSession::ManageSession(QObject* parent) : QObject(parent) {
    timeout_.setSingleShot(true);
    dtrTimer_.setSingleShot(true);
    connect(&timeout_, &QTimer::timeout, this, [this] {
        if(phase_ == Phase::Ready && op_ == Op::None) return;
        if(phase_ == Phase::Stopping) { closePort(); emit stopped(); return; }
        if(phase_ == Phase::Ready) {
            opError("cli_timeout", "The Flipper did not finish the backup or update operation in time.",
                "Retry. A large backup or firmware apply can take longer.");
            return;
        }
        fail({{"code", "cli_timeout"}, {"operation", "Start device management"}, {"path", port_},
            {"reason", "The Flipper did not enter an RPC management session in time."},
            {"suggestion", "Unlock the Flipper, close other serial clients, and retry."}});
    });
    connect(&dtrTimer_, &QTimer::timeout, this, [this] {
        if(fd_ < 0) return;
        int flags = TIOCM_DTR | TIOCM_RTS;
        if(ioctl(fd_, TIOCMBIS, &flags) < 0 && errno != ENOTTY)
            fail(systemError("Enable serial session", port_, errno));
    });
}
ManageSession::~ManageSession() { closePort(); }

bool ManageSession::busy() const {
    return op_ != Op::None || phase_ == Phase::Banner || phase_ == Phase::StartRpc || phase_ == Phase::Ping;
}

QJsonObject ManageSession::snapshot() const {
    return {{"open", phase_ != Phase::Done}, {"ready", phase_ == Phase::Ready}, {"busy", busy()},
        {"error", error_}, {"errorCode", errorCode_}, {"backups", backups_}, {"packs", packs_}};
}

void ManageSession::setDevice(const QJsonObject& info, const QJsonObject& rpc) {
    info_ = info; rpc_ = rpc;
}

void ManageSession::closePort() {
    timeout_.stop(); dtrTimer_.stop();
    delete reader_; reader_ = nullptr;
    delete writer_; writer_ = nullptr;
    if(fd_ >= 0) {
        int flags = TIOCM_DTR | TIOCM_RTS;
        if(exclusive_) ioctl(fd_, TIOCMBIC, &flags);
        if(saved_) tcsetattr(fd_, TCSANOW, &previous_);
        if(exclusive_) ioctl(fd_, TIOCNXCL);
        close(fd_);
        fd_ = -1;
    }
    saved_ = false; exclusive_ = false; phase_ = Phase::Done; op_ = Op::None;
}

void ManageSession::start(const QString& port) {
    closePort();
    port_ = port; input_.clear(); output_.clear(); commandId_ = 0;
    error_.clear(); errorCode_.clear(); backups_ = {}; packs_ = {}; pendingJson_ = {};
    fd_ = open(port.toLocal8Bit().constData(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if(fd_ < 0) { fail(systemError("Open Flipper serial port", port, errno)); return; }
    if(flock(fd_, LOCK_EX | LOCK_NB) < 0 || ioctl(fd_, TIOCEXCL) < 0) {
        fail(systemError("Acquire exclusive serial access", port, errno)); return;
    }
    exclusive_ = true;
    if(tcgetattr(fd_, &previous_) < 0) { fail(systemError("Read serial settings", port, errno)); return; }
    saved_ = true;
    termios config = previous_;
    cfmakeraw(&config);
    cfsetispeed(&config, B115200); cfsetospeed(&config, B115200);
    config.c_cflag |= CLOCAL | CREAD;
    config.c_cflag &= ~CRTSCTS;
    config.c_cc[VMIN] = 1; config.c_cc[VTIME] = 0;
    if(tcsetattr(fd_, TCSANOW, &config) < 0 || tcflush(fd_, TCIOFLUSH) < 0) {
        fail(systemError("Configure serial session", port, errno)); return;
    }
    phase_ = Phase::Banner;
    reader_ = new QSocketNotifier(fd_, QSocketNotifier::Read, this);
    writer_ = new QSocketNotifier(fd_, QSocketNotifier::Write, this);
    writer_->setEnabled(false);
    connect(reader_, &QSocketNotifier::activated, this, &ManageSession::readReady);
    connect(writer_, &QSocketNotifier::activated, this, &ManageSession::drainWrite);
    int flags = TIOCM_DTR | TIOCM_RTS;
    if(ioctl(fd_, TIOCMBIC, &flags) < 0 && errno != ENOTTY) {
        fail(systemError("Reset serial session", port, errno)); return;
    }
    dtrTimer_.start(50);
    timeout_.start(5000);
    emit changed();
}

void ManageSession::stop() {
    if(fd_ < 0 || phase_ == Phase::Done) { closePort(); emit stopped(); return; }
    if(phase_ == Phase::Ready) {
        phase_ = Phase::Stopping; op_ = Op::None;
        sendRpc(stopSessionRequest(++commandId_));
        timeout_.start(1500);
        return;
    }
    closePort();
    emit stopped();
}

void ManageSession::send(const QByteArray& bytes) {
    output_.append(bytes);
    if(phase_ != Phase::Ready && phase_ != Phase::Stopping) timeout_.start(5000);
    writer_->setEnabled(true);
    drainWrite();
}
void ManageSession::sendRpc(const PB::Main& message) { send(encodeDelimited(message)); }
void ManageSession::drainWrite() {
    while(!output_.isEmpty()) {
        const auto n = ::write(fd_, output_.constData(), static_cast<size_t>(output_.size()));
        if(n < 0) {
            if(errno == EINTR) continue;
            if(errno == EAGAIN) return;
            fail(systemError("Write management command", port_, errno)); return;
        }
        if(n == 0) return;
        output_.remove(0, n);
    }
    if(op_ == Op::Write) fillWrite();
    if(writer_) writer_->setEnabled(!output_.isEmpty());
}

void ManageSession::readReady() {
    char bytes[4096];
    for(;;) {
        const auto count = ::read(fd_, bytes, sizeof(bytes));
        if(count < 0) {
            if(errno == EINTR) continue;
            if(errno == EAGAIN) break;
            fail(systemError("Read management session", port_, errno)); return;
        }
        if(count == 0) { fail(systemError("Serial device disconnected", port_, ENODEV)); return; }
        input_.append(bytes, count);
        if(input_.size() > 1024 * 1024) {
            fail({{"code", "response_too_large"}, {"operation", "Read management session"},
                {"path", port_}, {"reason", "Management RPC input exceeded the 1 MiB bound."},
                {"suggestion", "Close Device and retry."}}); return;
        }
    }
    if(phase_ == Phase::Banner) {
        if(!takeCliResponse(input_)) return;
        phase_ = Phase::StartRpc;
        send("start_rpc_session\r");
        return;
    }
    if(phase_ == Phase::StartRpc) {
        if(!input_.contains("start_rpc_session\r\n")) return;
        input_.clear();
        phase_ = Phase::Ping;
        sendRpc(pingRequest(++commandId_, "omaflip"));
        return;
    }
    for(;;) {
        PB::Main message;
        const auto status = takeDelimited(input_, message);
        if(status == FrameStatus::NeedMore) return;
        if(status != FrameStatus::Ok) {
            fail({{"code", "rpc_frame"}, {"operation", "Read management session"}, {"path", port_},
                {"reason", "RPC frame was corrupt or too large."},
                {"suggestion", "Close Device and retry."}}); return;
        }
        handleRpc(message);
        if(phase_ == Phase::Done) return;
    }
}

void ManageSession::beginOp(Op op, quint32 timeoutMs) {
    op_ = op; error_.clear(); errorCode_.clear();
    timeout_.start(static_cast<int>(timeoutMs));
    emit changed();
}
void ManageSession::finishOp() {
    op_ = Op::None; timeout_.stop(); emit changed();
}
void ManageSession::opError(const QString& code, const QString& reason, const QString& suggestion) {
    errorCode_ = code;
    error_ = suggestion.isEmpty() ? reason : reason + " " + suggestion;
    op_ = Op::None; pendingJson_ = {}; timeout_.stop();
    emit changed();
}
void ManageSession::fail(const QJsonObject& error) { closePort(); emit failed(error); }

void ManageSession::beginList() {
    backups_ = {}; pendingJson_ = {};
    after_ = "list";
    beginOp(Op::List);
    sendRpc(storageListRequest(++commandId_, kBackupDir));
}

void ManageSession::beginPackList() {
    packs_ = {};
    after_ = "packs";
    beginOp(Op::Mkdir);
    sendRpc(storageMkdirRequest(++commandId_, kAssetPackDir));
}

void ManageSession::refresh() {
    if(phase_ != Phase::Ready || op_ != Op::None) {
        opError("busy", "A device-management operation is already running.", "Wait for it to finish.");
        return;
    }
    beginOp(Op::Mkdir);
    after_ = "list";
    sendRpc(storageMkdirRequest(++commandId_, kBackupDir));
}

void ManageSession::createBackup() {
    if(phase_ != Phase::Ready || op_ != Op::None) {
        opError("busy", "A device-management operation is already running.", "Wait for it to finish.");
        return;
    }
    stamp_ = QDateTime::currentDateTimeUtc().toString("yyyyMMdd-HHmmss");
    tarPath_ = QString(kBackupDir) + "/omaflip-" + stamp_ + ".tar";
    jsonPath_ = QString(kBackupDir) + "/omaflip-" + stamp_ + ".json";
    currentMeta_ = backupMetadata(info_, rpc_, "omaflip-" + stamp_ + ".tar");
    currentMeta_.insert("created", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    beginOp(Op::Mkdir);
    after_ = "backup";
    sendRpc(storageMkdirRequest(++commandId_, kBackupDir));
}

void ManageSession::restore(const QString& archive, bool confirmOrigin, bool confirmVersion) {
    const auto path = normalizeDevicePath(archive);
    if(!path.startsWith(QString(kBackupDir) + "/") || !path.endsWith(".tar")) {
        opError("invalid_path", "Choose a backup archive under /ext/omaflip_backup.", "Create a backup first.");
        return;
    }
    if(phase_ != Phase::Ready || op_ != Op::None) {
        opError("busy", "A device-management operation is already running.", "Wait for it to finish.");
        return;
    }
    QJsonObject meta;
    for(const auto& value : backups_) {
        const auto item = value.toObject();
        if(item.value("archive").toString() == path) { meta = item; break; }
    }
    const auto compat = backupCompat(meta, info_);
    if(compat == "target_mismatch") {
        opError("incompatible", "That backup is for a different hardware target.", "Do not restore it on this Flipper.");
        return;
    }
    if(compat == "origin_mismatch" && !confirmOrigin) {
        opError("origin_mismatch", "That backup was made on a different firmware origin.",
            "Confirm restore to replace the current origin.");
        return;
    }
    if(compat == "version_mismatch" && !confirmVersion) {
        opError("version_mismatch", "That backup was made on a different firmware version.",
            "Confirm restore if you still want internal storage from that version.");
        return;
    }
    tarPath_ = path;
    beginOp(Op::Restore, 60000);
    sendRpc(storageBackupRestoreRequest(++commandId_, path));
}

void ManageSession::applyFirmware(const QString& hostPath, const QJsonObject& package, bool replaceOrigin) {
    if(phase_ != Phase::Ready || op_ != Op::None) {
        opError("busy", "A device-management operation is already running.", "Wait for it to finish.");
        return;
    }
    const auto provider = package.value("provider").toString() == "momentum" ? QString("momentum") : QString("official");
    const auto label = providerLabel(provider);
    const auto expected = provider == "momentum" ? QString("Momentum") : QString("Official");
    const auto origin = info_.value("firmware_origin_fork").toString();
    if(!origin.isEmpty() && origin.compare(expected, Qt::CaseInsensitive) != 0 && !replaceOrigin) {
        opError("origin_mismatch", "This Flipper is not running " + label + " firmware.",
            "Confirm replace origin to install the " + label + " package. This replaces the current firmware.");
        return;
    }
    const auto target = hardwareTargetName(info_.value("hardware_target").toString());
    if(!target.isEmpty() && package.value("target").toString() != target) {
        opError("incompatible", "The " + label + " package target does not match this Flipper.",
            "Do not install it.");
        return;
    }
    QFile file(hostPath);
    if(!file.open(QIODevice::ReadOnly)) {
        opError("io", "Could not read the verified update package.", "Download it again.");
        return;
    }
    const auto data = file.readAll();
    file.close();
    if(!verifySha256(data, package.value("sha256").toString())) {
        opError("firmware_hash", "The local package SHA-256 no longer matches the " + label + " index.",
            "Download it again. Do not install an unverified file.");
        return;
    }
    if(data.size() > kMaxTransferBytes) {
        opError("too_large", "Update packages are limited to 32 MiB.", "The " + label + " file is unexpectedly large.");
        return;
    }
    hostPath_ = hostPath;
    writeBuffer_ = data;
    extractDir_ = "/ext/update";
    tarPath_ = provider == "momentum" ? QString("/ext/update/omaflip-momentum.tgz")
                                      : QString("/ext/update/omaflip-official.tgz");
    manifestPath_ = updateManifestPath(package);
    beginOp(Op::Mkdir, 30000);
    after_ = "upload";
    sendRpc(storageMkdirRequest(++commandId_, extractDir_));
}

void ManageSession::installPack(const QString& hostPath, const QJsonObject& pack) {
    if(phase_ != Phase::Ready || op_ != Op::None) {
        opError("busy", "A device-management operation is already running.", "Wait for it to finish.");
        return;
    }
    if(originKind(info_.value("firmware_origin_fork").toString()) != "Momentum") {
        opError("origin_mismatch", "Asset packs are a Momentum firmware feature.",
            "Install Momentum firmware first. Packs are selected in Momentum Settings on the Flipper.");
        return;
    }
    QFile file(hostPath);
    if(!file.open(QIODevice::ReadOnly)) {
        opError("io", "Could not read the verified asset pack.", "Download it again.");
        return;
    }
    const auto data = file.readAll();
    file.close();
    if(!verifySha256(data, pack.value("sha256").toString())) {
        opError("pack_hash", "The local pack SHA-256 no longer matches the Momentum index.",
            "Download it again. Do not install an unverified file.");
        return;
    }
    if(data.size() > kMaxTransferBytes) {
        opError("too_large", "Asset packs are limited to 32 MiB.", "Choose a smaller pack.");
        return;
    }
    hostPath_ = hostPath;
    writeBuffer_ = data;
    extractDir_ = kAssetPackDir;
    tarPath_ = QString(kAssetPackDir) + "/omaflip-pack.tgz";
    beginOp(Op::Mkdir, 30000);
    after_ = "pack";
    sendRpc(storageMkdirRequest(++commandId_, extractDir_));
}

void ManageSession::removePack(const QString& name) {
    const auto path = joinDevicePath(kAssetPackDir, name);
    if(path.isEmpty() || path == kAssetPackDir || !path.startsWith(QString(kAssetPackDir) + "/")) {
        opError("invalid_path", "Choose an asset pack folder under /ext/asset_packs.",
            "List installed packs first.");
        return;
    }
    if(phase_ != Phase::Ready || op_ != Op::None) {
        opError("busy", "A device-management operation is already running.", "Wait for it to finish.");
        return;
    }
    tarPath_ = path;
    after_ = "remove";
    beginOp(Op::Delete, 30000);
    sendRpc(storageDeleteRequest(++commandId_, path, true));
}

void ManageSession::beginWriteJson() {
    writeBuffer_ = QJsonDocument(currentMeta_).toJson(QJsonDocument::Compact);
    jsonPath_ = jsonPath_.isEmpty() ? tarPath_ : jsonPath_;
    beginOp(Op::Write);
    ++commandId_;
    fillWrite();
    if(writer_) writer_->setEnabled(true);
    drainWrite();
}

void ManageSession::fillWrite() {
    if(op_ != Op::Write) return;
    const auto dest = (after_ == "upload" || after_ == "pack") ? tarPath_ : jsonPath_;
    while(output_.size() < 4096) {
        const auto n = qMin(kStorageChunk, writeBuffer_.size());
        const auto chunk = writeBuffer_.left(n);
        writeBuffer_.remove(0, n);
        const bool hasNext = !writeBuffer_.isEmpty();
        output_.append(encodeDelimited(storageWriteRequest(commandId_, dest, chunk, hasNext)));
        if(!hasNext) return;
    }
}

void ManageSession::nextRead() {
    if(pendingJson_.isEmpty()) { beginPackList(); return; }
    jsonPath_ = pendingJson_.takeAt(0).toString();
    readBuffer_.clear();
    beginOp(Op::Read);
    sendRpc(storageReadRequest(++commandId_, jsonPath_));
}

void ManageSession::handleRpc(const PB::Main& message) {
    if(phase_ == Phase::Ping) {
        if(message.command_id() != commandId_ || !message.has_system_ping_response()) {
            fail({{"code", "rpc_ping"}, {"operation", "Start device management"}, {"path", port_},
                {"reason", "RPC ping failed while starting device management."},
                {"suggestion", "Unlock the Flipper and retry Device."}}); return;
        }
        phase_ = Phase::Ready;
        timeout_.stop();
        emit ready();
        refresh();
        return;
    }
    if(phase_ == Phase::Stopping) { closePort(); emit stopped(); return; }
    if(phase_ != Phase::Ready || op_ == Op::None) return;
    if(message.command_id() != commandId_) return;

    if(op_ == Op::Mkdir) {
        if(message.command_status() != PB::CommandStatus::OK
            && message.command_status() != PB::CommandStatus::ERROR_STORAGE_EXIST) {
            opError("rpc", "Could not create the working folder (" + commandStatusName(message.command_status()) + ").",
                "The SD card may be missing.");
            return;
        }
        if(after_ == "list") { beginList(); return; }
        if(after_ == "backup") {
            beginOp(Op::Backup, 60000);
            sendRpc(storageBackupCreateRequest(++commandId_, tarPath_));
            return;
        }
        if(after_ == "packs") {
            beginOp(Op::List);
            sendRpc(storageListRequest(++commandId_, kAssetPackDir));
            return;
        }
        if(after_ == "upload" || after_ == "pack") {
            beginOp(Op::Write, 120000);
            ++commandId_;
            fillWrite();
            if(writer_) writer_->setEnabled(true);
            drainWrite();
            return;
        }
        finishOp();
        return;
    }

    if(op_ == Op::List) {
        if(after_ == "packs") {
            if(message.command_status() != PB::CommandStatus::OK) {
                if(message.command_status() == PB::CommandStatus::ERROR_STORAGE_NOT_EXIST) { finishOp(); return; }
                opError("rpc", "Could not list asset packs (" + commandStatusName(message.command_status()) + ").",
                    "Create /ext/asset_packs on the SD card.");
                return;
            }
            if(message.has_storage_list_response()) {
                const auto& list = message.storage_list_response();
                for(int i = 0; i < list.file_size(); ++i) {
                    const auto& file = list.file(i);
                    const auto name = QString::fromStdString(file.name());
                    if(file.type() != PB_Storage::File_FileType_DIR || name.isEmpty() || name.startsWith('.'))
                        continue;
                    const auto path = joinDevicePath(kAssetPackDir, name);
                    if(path.isEmpty()) continue;
                    packs_.append(QJsonObject{{"name", name}, {"path", path},
                        {"size", static_cast<qint64>(file.size())}});
                }
            }
            if(message.has_next()) return;
            finishOp();
            return;
        }
        if(message.command_status() != PB::CommandStatus::OK) {
            if(message.command_status() == PB::CommandStatus::ERROR_STORAGE_NOT_EXIST) { beginPackList(); return; }
            opError("rpc", "Could not list backups (" + commandStatusName(message.command_status()) + ").",
                "Create a backup folder on the SD card.");
            return;
        }
        if(message.has_storage_list_response()) {
            const auto& list = message.storage_list_response();
            for(int i = 0; i < list.file_size(); ++i) {
                const auto& file = list.file(i);
                const auto name = QString::fromStdString(file.name());
                if(!name.endsWith(".tar") && !name.endsWith(".json")) continue;
                const auto path = joinDevicePath(kBackupDir, name);
                if(name.endsWith(".json")) pendingJson_.append(path);
                else backups_.append(QJsonObject{{"archive", path}, {"name", name},
                    {"size", static_cast<qint64>(file.size())}});
            }
        }
        if(message.has_next()) return;
        nextRead();
        return;
    }

    if(op_ == Op::Read) {
        if(message.command_status() == PB::CommandStatus::OK
            && message.has_storage_read_response() && message.storage_read_response().has_file()) {
            readBuffer_.append(QByteArray::fromStdString(message.storage_read_response().file().data()));
        }
        if(message.has_next() && message.command_status() == PB::CommandStatus::OK) return;
        QJsonParseError parse;
        const auto doc = QJsonDocument::fromJson(readBuffer_, &parse);
        if(parse.error == QJsonParseError::NoError && doc.isObject()) {
            auto meta = doc.object();
            const auto archive = joinDevicePath(kBackupDir, meta.value("archive").toString());
            for(int i = 0; i < backups_.size(); ++i) {
                auto item = backups_[i].toObject();
                if(item.value("archive").toString() == archive) {
                    for(auto it = meta.begin(); it != meta.end(); ++it) item.insert(it.key(), it.value());
                    item.insert("compat", backupCompat(item, info_));
                    backups_[i] = item;
                    break;
                }
            }
        }
        nextRead();
        return;
    }

    if(op_ == Op::Backup) {
        if(message.command_status() != PB::CommandStatus::OK) {
            opError("rpc", "Backup create failed (" + commandStatusName(message.command_status()) + ").",
                "Unlock the Flipper and check free SD space.");
            return;
        }
        after_ = "list";
        beginWriteJson();
        return;
    }

    if(op_ == Op::Write) {
        if(message.command_status() != PB::CommandStatus::OK) {
            opError("rpc", "Could not write management data (" + commandStatusName(message.command_status()) + ").",
                "The SD card may be full.");
            return;
        }
        if(after_ == "list") { beginList(); return; }
        if(after_ == "upload" || after_ == "pack") {
            beginOp(Op::Extract, 120000);
            sendRpc(storageTarExtractRequest(++commandId_, tarPath_, extractDir_));
            return;
        }
        finishOp();
        return;
    }

    if(op_ == Op::Restore) {
        if(message.command_status() != PB::CommandStatus::OK) {
            opError("rpc", "Restore failed (" + commandStatusName(message.command_status()) + ").",
                "The archive may be damaged. Keep the current internal storage.");
            return;
        }
        finishOp();
        return;
    }

    if(op_ == Op::Extract) {
        if(message.command_status() != PB::CommandStatus::OK) {
            const auto what = after_ == "pack" ? QString("asset pack") : QString("update package");
            opError("rpc", "Could not extract the " + what + " (" + commandStatusName(message.command_status()) + ").",
                after_ == "pack" ? QString("The archive may be damaged.")
                                 : QString("The archive may be damaged. Do not reboot yet."));
            return;
        }
        if(after_ == "pack") {
            beginOp(Op::Delete, 15000);
            sendRpc(storageDeleteRequest(++commandId_, tarPath_, false));
            return;
        }
        beginOp(Op::Update, 30000);
        sendRpc(systemUpdateRequest(++commandId_, manifestPath_));
        return;
    }

    if(op_ == Op::Delete) {
        if(message.command_status() != PB::CommandStatus::OK
            && message.command_status() != PB::CommandStatus::ERROR_STORAGE_NOT_EXIST) {
            opError("rpc", "Could not remove the file (" + commandStatusName(message.command_status()) + ").",
                "Retry after the SD card is idle.");
            return;
        }
        beginPackList();
        return;
    }

    if(op_ == Op::Update) {
        if(message.has_system_update_response()
            && message.system_update_response().code() != PB_System::UpdateResponse_UpdateResultCode_OK) {
            opError("rpc", "The firmware refused the update package.",
                "Recovery: keep the backup, do not reboot, and retry from a verified package.");
            return;
        }
        if(message.command_status() != PB::CommandStatus::OK) {
            opError("rpc", "Update prepare failed (" + commandStatusName(message.command_status()) + ").",
                "Do not reboot. Restore from a backup if the device is unhealthy.");
            return;
        }
        beginOp(Op::Reboot, 5000);
        sendRpc(systemRebootRequest(++commandId_, PB_System::RebootRequest_RebootMode_UPDATE));
        return;
    }

    if(op_ == Op::Reboot) {
        finishOp();
    }
}
}
