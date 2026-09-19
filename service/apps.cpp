#include "apps.h"
#include "model.h"
#include <QFileInfo>
#include <QJsonArray>
#include <QSocketNotifier>
#include <QVector>
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace omaflip {
namespace {
void sortApps(QJsonArray& apps) {
    QVector<QJsonObject> items;
    for(const auto& value : apps) items.append(value.toObject());
    std::sort(items.begin(), items.end(), [](const QJsonObject& a, const QJsonObject& b) {
        const auto cat = a["category"].toString().compare(b["category"].toString(), Qt::CaseInsensitive);
        if(cat != 0) return cat < 0;
        return a["name"].toString().compare(b["name"].toString(), Qt::CaseInsensitive) < 0;
    });
    QJsonArray sorted;
    for(const auto& item : items) sorted.append(item);
    apps = sorted;
}
}

QString appKindForName(const QString& name) {
    if(name.endsWith(".fap", Qt::CaseInsensitive)) return "fap";
    if(name.endsWith(".js", Qt::CaseInsensitive)) return "js";
    return {};
}

bool isAppStoragePath(const QString& path) {
    const auto normalized = normalizeDevicePath(path);
    return normalized == "/ext/apps" || normalized.startsWith("/ext/apps/");
}

QString defaultInstallDir(const QString& name) {
    if(appKindForName(name) == "js") return "/ext/apps/Scripts";
    return "/ext/apps/Misc";
}

AppSession::AppSession(QObject* parent) : QObject(parent) {
    timeout_.setSingleShot(true);
    dtrTimer_.setSingleShot(true);
    connect(&timeout_, &QTimer::timeout, this, [this] {
        if(phase_ == Phase::Ready && op_ == Op::None) return;
        if(phase_ == Phase::Stopping) { closePort(); emit stopped(); return; }
        if(phase_ == Phase::Ready) {
            opError("cli_timeout", "The Flipper did not finish the apps operation in time.",
                "Retry. A large install may need another attempt.");
            return;
        }
        fail({{"code", "cli_timeout"}, {"operation", "Start apps session"}, {"path", port_},
            {"reason", "The Flipper did not enter an RPC apps session in time."},
            {"suggestion", "Unlock the Flipper, close other serial clients, and retry."}});
    });
    connect(&dtrTimer_, &QTimer::timeout, this, [this] {
        if(fd_ < 0) return;
        int flags = TIOCM_DTR | TIOCM_RTS;
        if(ioctl(fd_, TIOCMBIS, &flags) < 0 && errno != ENOTTY)
            fail(systemError("Enable serial session", port_, errno));
    });
}
AppSession::~AppSession() { closePort(); }

bool AppSession::busy() const {
    return op_ != Op::None || phase_ == Phase::Banner || phase_ == Phase::StartRpc || phase_ == Phase::Ping;
}

QJsonObject AppSession::snapshot() const {
    return {{"open", phase_ != Phase::Done}, {"ready", phase_ == Phase::Ready}, {"busy", busy()},
        {"error", error_}, {"errorCode", errorCode_}, {"apps", apps_}, {"script", script_},
        {"locked", locked_}};
}

void AppSession::closePort() {
    timeout_.stop(); dtrTimer_.stop();
    if(file_.isOpen()) file_.close();
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

void AppSession::start(const QString& port) {
    closePort();
    port_ = port; input_.clear(); output_.clear(); commandId_ = 0;
    error_.clear(); errorCode_.clear(); apps_ = {}; script_ = {}; pendingDirs_.clear();
    locked_ = false;
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
    connect(reader_, &QSocketNotifier::activated, this, &AppSession::readReady);
    connect(writer_, &QSocketNotifier::activated, this, &AppSession::drainWrite);
    int flags = TIOCM_DTR | TIOCM_RTS;
    if(ioctl(fd_, TIOCMBIC, &flags) < 0 && errno != ENOTTY) {
        fail(systemError("Reset serial session", port, errno)); return;
    }
    dtrTimer_.start(50);
    timeout_.start(5000);
    emit changed();
}

void AppSession::stop() {
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

void AppSession::send(const QByteArray& bytes) {
    output_.append(bytes);
    if(phase_ != Phase::Ready && phase_ != Phase::Stopping) timeout_.start(5000);
    writer_->setEnabled(true);
    drainWrite();
}
void AppSession::sendRpc(const PB::Main& message) { send(encodeDelimited(message)); }
void AppSession::drainWrite() {
    while(!output_.isEmpty()) {
        const auto n = ::write(fd_, output_.constData(), static_cast<size_t>(output_.size()));
        if(n < 0) {
            if(errno == EINTR) continue;
            if(errno == EAGAIN) return;
            fail(systemError("Write apps command", port_, errno)); return;
        }
        if(n == 0) return;
        output_.remove(0, n);
    }
    if(op_ == Op::Write && file_.isOpen()) fillWrite();
    if(writer_) writer_->setEnabled(!output_.isEmpty());
}

void AppSession::readReady() {
    char bytes[4096];
    for(;;) {
        const auto count = ::read(fd_, bytes, sizeof(bytes));
        if(count < 0) {
            if(errno == EINTR) continue;
            if(errno == EAGAIN) break;
            fail(systemError("Read apps session", port_, errno)); return;
        }
        if(count == 0) { fail(systemError("Serial device disconnected", port_, ENODEV)); return; }
        input_.append(bytes, count);
        if(input_.size() > 1024 * 1024) {
            fail({{"code", "response_too_large"}, {"operation", "Read apps session"},
                {"path", port_}, {"reason", "Apps RPC input exceeded the 1 MiB bound."},
                {"suggestion", "Close Apps and retry."}}); return;
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
            fail({{"code", "rpc_frame"}, {"operation", "Read apps session"}, {"path", port_},
                {"reason", "RPC frame was corrupt or too large."},
                {"suggestion", "Close Apps and retry."}}); return;
        }
        handleRpc(message);
        if(phase_ == Phase::Done) return;
    }
}

void AppSession::beginOp(Op op, quint32 timeoutMs) {
    op_ = op;
    error_.clear();
    errorCode_.clear();
    timeout_.start(static_cast<int>(timeoutMs));
    emit changed();
}

void AppSession::finishOp() {
    op_ = Op::None;
    timeout_.stop();
    if(file_.isOpen()) file_.close();
    emit changed();
}

void AppSession::opError(const QString& code, const QString& reason, const QString& suggestion) {
    errorCode_ = code;
    error_ = reason;
    if(!suggestion.isEmpty()) error_ = reason + " " + suggestion;
    if(file_.isOpen()) file_.close();
    op_ = Op::None;
    pendingDirs_.clear();
    timeout_.stop();
    emit changed();
}

void AppSession::fail(const QJsonObject& error) {
    closePort();
    emit failed(error);
}

void AppSession::beginScan() {
    apps_ = {};
    pendingDirs_ = {"/ext/apps"};
    beginList(pendingDirs_.takeFirst());
}

void AppSession::beginList(const QString& path) {
    listPath_ = path;
    beginOp(Op::List);
    sendRpc(storageListRequest(++commandId_, path));
}

void AppSession::refresh() {
    if(phase_ != Phase::Ready || op_ != Op::None) {
        opError("busy", "An Apps operation is already running.", "Wait for it to finish.");
        return;
    }
    beginScan();
}

void AppSession::launch(const QString& path) {
    const auto normalized = normalizeDevicePath(path);
    if(!isAppStoragePath(normalized)) {
        opError("invalid_path", "Choose an app under /ext/apps.", "Open Apps and select a .fap or .js file.");
        return;
    }
    if(phase_ != Phase::Ready || op_ != Op::None) {
        opError("busy", "An Apps operation is already running.", "Wait for it to finish.");
        return;
    }
    const auto kind = appKindForName(deviceBasename(normalized));
    QString name = normalized;
    QString args;
    if(kind == "js") { name = "JS Runner"; args = normalized; }
    else if(kind != "fap") {
        opError("invalid_path", "Only .fap and .js files can be launched.", "Select an app or script.");
        return;
    }
    targetPath_ = normalized;
    beginOp(Op::Start);
    sendRpc(appStartRequest(++commandId_, name, args));
}

void AppSession::exitApp() {
    if(phase_ != Phase::Ready || op_ != Op::None) {
        opError("busy", "An Apps operation is already running.", "Wait for it to finish.");
        return;
    }
    beginOp(Op::Exit);
    sendRpc(appExitRequest(++commandId_));
}

void AppSession::remove(const QString& path) {
    const auto normalized = normalizeDevicePath(path);
    if(!isAppStoragePath(normalized) || normalized == "/ext/apps") {
        opError("invalid_path", "Choose an installed app to remove.", "The apps root cannot be deleted.");
        return;
    }
    if(phase_ != Phase::Ready || op_ != Op::None) {
        opError("busy", "An Apps operation is already running.", "Wait for it to finish.");
        return;
    }
    targetPath_ = normalized;
    afterWrite_.clear();
    beginOp(Op::Delete);
    sendRpc(storageDeleteRequest(++commandId_, normalized, false));
}

void AppSession::install(const QString& hostPath, const QString& destDir, bool overwrite) {
    const auto host = QFileInfo(hostPath);
    if(!host.exists() || !host.isFile()) {
        opError("invalid_host", "The host file is missing or not a regular file.", "Drop a .fap or .js file.");
        return;
    }
    const auto kind = appKindForName(host.fileName());
    if(kind.isEmpty()) {
        opError("invalid_host", "Only .fap and .js files can be installed.", "Drop an app package or script.");
        return;
    }
    auto dir = destDir.isEmpty() ? defaultInstallDir(host.fileName()) : normalizeDevicePath(destDir);
    if(dir == "/ext/apps") dir = defaultInstallDir(host.fileName());
    if(!isAppStoragePath(dir) || dir == "/ext/apps") {
        opError("invalid_path", "Install into a folder under /ext/apps.", "Open a category, then drop the file.");
        return;
    }
    const auto dest = joinDevicePath(dir, sanitizeHostName(host.fileName()));
    if(dest.isEmpty()) {
        opError("invalid_path", "Could not build an install path.", "Rename the file to ASCII and retry.");
        return;
    }
    if(host.size() > kMaxTransferBytes) {
        opError("too_large", "Installs are limited to 32 MiB.", "Copy a smaller file.");
        return;
    }
    if(phase_ != Phase::Ready || op_ != Op::None) {
        opError("busy", "An Apps operation is already running.", "Wait for it to finish.");
        return;
    }
    if(!overwrite) {
        for(const auto& value : apps_) {
            if(value.toObject()["path"].toString() == dest) {
                opError("exists", "An app with that name is already installed.",
                    "Confirm overwrite to replace it.");
                return;
            }
        }
    }
    targetPath_ = dest;
    hostPath_ = host.absoluteFilePath();
    overwrite_ = overwrite;
    transferTotal_ = host.size();
    afterWrite_ = "scan";
    beginOp(Op::Mkdir);
    sendRpc(storageMkdirRequest(++commandId_, dir));
}

void AppSession::readScript(const QString& path) {
    const auto normalized = normalizeDevicePath(path);
    if(!isAppStoragePath(normalized) || appKindForName(deviceBasename(normalized)) != "js") {
        opError("invalid_path", "Choose a .js script under /ext/apps.", "Open Scripts and select a file.");
        return;
    }
    if(phase_ != Phase::Ready || op_ != Op::None) {
        opError("busy", "An Apps operation is already running.", "Wait for it to finish.");
        return;
    }
    targetPath_ = normalized;
    readBuffer_.clear();
    beginOp(Op::Read);
    sendRpc(storageReadRequest(++commandId_, normalized));
}

void AppSession::writeScript(const QString& path, const QString& text) {
    const auto normalized = normalizeDevicePath(path);
    if(!isAppStoragePath(normalized) || appKindForName(deviceBasename(normalized)) != "js") {
        opError("invalid_path", "Choose a .js script under /ext/apps.", "Save into /ext/apps/Scripts.");
        return;
    }
    const auto data = text.toUtf8();
    if(data.size() > kMaxPreviewBytes) {
        opError("too_large", "Scripts are limited to 32 KiB in this editor.", "Trim the script and save again.");
        return;
    }
    if(phase_ != Phase::Ready || op_ != Op::None) {
        opError("busy", "An Apps operation is already running.", "Wait for it to finish.");
        return;
    }
    targetPath_ = normalized;
    hostPath_.clear();
    readBuffer_ = data;
    afterWrite_.clear();
    transferTotal_ = data.size();
    transferBytes_ = 0;
    beginOp(Op::Write, 15000);
    ++commandId_;
    fillWrite();
    if(writer_) writer_->setEnabled(true);
    drainWrite();
}

void AppSession::beginWrite() {
    file_.setFileName(hostPath_);
    if(!file_.open(QIODevice::ReadOnly)) {
        opError("io", "Could not read the host file.", "Check the file still exists and retry.");
        return;
    }
    transferBytes_ = 0;
    beginOp(Op::Write, 30000);
    ++commandId_;
    fillWrite();
    if(writer_) writer_->setEnabled(true);
    drainWrite();
}

void AppSession::fillWrite() {
    if(op_ != Op::Write) return;
    while(output_.size() < 4096) {
        QByteArray chunk;
        bool hasNext = false;
        if(file_.isOpen()) {
            chunk = QByteArray(kStorageChunk, Qt::Uninitialized);
            const auto n = file_.read(chunk.data(), kStorageChunk);
            if(n < 0) {
                opError("io", "Could not read the host file during install.", "Retry the install.");
                return;
            }
            chunk.resize(static_cast<int>(n));
            hasNext = !file_.atEnd() && n > 0;
        } else {
            const auto n = qMin(kStorageChunk, readBuffer_.size());
            chunk = readBuffer_.left(n);
            readBuffer_.remove(0, n);
            hasNext = !readBuffer_.isEmpty();
        }
        output_.append(encodeDelimited(storageWriteRequest(commandId_, targetPath_, chunk, hasNext)));
        transferBytes_ += chunk.size();
        if(!hasNext) {
            if(file_.isOpen()) file_.close();
            return;
        }
    }
}

void AppSession::handleRpc(const PB::Main& message) {
    if(phase_ == Phase::Ping) {
        if(message.command_id() != commandId_ || !message.has_system_ping_response()) {
            fail({{"code", "rpc_ping"}, {"operation", "Start apps session"}, {"path", port_},
                {"reason", "RPC ping failed while starting the apps session."},
                {"suggestion", "Unlock the Flipper and retry Apps."}}); return;
        }
        phase_ = Phase::Ready;
        timeout_.stop();
        emit ready();
        beginScan();
        return;
    }
    if(phase_ == Phase::Stopping) {
        closePort();
        emit stopped();
        return;
    }
    if(phase_ != Phase::Ready || op_ == Op::None) return;
    if(message.command_id() != commandId_) return;

    if(op_ == Op::List) {
        if(message.command_status() != PB::CommandStatus::OK) {
            if(listPath_ == "/ext/apps") {
                opError("rpc", "Could not list /ext/apps (" + commandStatusName(message.command_status()) + ").",
                    "The SD card may be missing.");
                return;
            }
            if(!pendingDirs_.isEmpty()) { beginList(pendingDirs_.takeFirst()); return; }
            sortApps(apps_);
            finishOp();
            return;
        }
        if(message.has_storage_list_response()) {
            const auto& list = message.storage_list_response();
            for(int i = 0; i < list.file_size(); ++i) {
                const auto& file = list.file(i);
                const auto name = QString::fromStdString(file.name());
                if(name.isEmpty() || name == "." || name == "..") continue;
                const auto path = joinDevicePath(listPath_, name);
                if(file.type() == PB_Storage::File_FileType_DIR) {
                    if(listPath_ == "/ext/apps" && pendingDirs_.size() < 64) pendingDirs_.append(path);
                    continue;
                }
                const auto kind = appKindForName(name);
                if(kind.isEmpty() || apps_.size() >= kMaxAppEntries) continue;
                apps_.append(QJsonObject{{"name", name}, {"path", path},
                    {"category", deviceBasename(listPath_)}, {"kind", kind},
                    {"size", static_cast<qint64>(file.size())}});
            }
        }
        if(message.has_next()) return;
        if(!pendingDirs_.isEmpty()) { beginList(pendingDirs_.takeFirst()); return; }
        sortApps(apps_);
        finishOp();
        return;
    }

    if(op_ == Op::Mkdir) {
        // EXIST is fine — the category folder is already there.
        if(message.command_status() != PB::CommandStatus::OK
            && message.command_status() != PB::CommandStatus::ERROR_STORAGE_EXIST) {
            opError("rpc", "Could not create the install folder (" + commandStatusName(message.command_status()) + ").",
                "Check that /ext/apps exists.");
            return;
        }
        beginWrite();
        return;
    }

    if(op_ == Op::Write) {
        if(message.command_status() != PB::CommandStatus::OK) {
            opError("rpc", "Install failed (" + commandStatusName(message.command_status()) + ").",
                "The SD card may be full, or confirm overwrite.");
            return;
        }
        if(!hostPath_.isEmpty()) script_ = {};
        finishOp();
        if(afterWrite_ == "scan") beginScan();
        return;
    }

    if(op_ == Op::Delete) {
        if(message.command_status() != PB::CommandStatus::OK) {
            opError("rpc", "Remove failed (" + commandStatusName(message.command_status()) + ").",
                "The file may be in use on the Flipper.");
            return;
        }
        if(script_["path"].toString() == targetPath_) script_ = {};
        finishOp();
        beginScan();
        return;
    }

    if(op_ == Op::Start) {
        if(message.command_status() != PB::CommandStatus::OK) {
            opError("rpc", "Launch failed (" + commandStatusName(message.command_status()) + ").",
                "Close the running app on the Flipper, then retry.");
            return;
        }
        locked_ = true;
        finishOp();
        return;
    }

    if(op_ == Op::Exit) {
        if(message.command_status() != PB::CommandStatus::OK) {
            opError("rpc", "The running app did not exit (" + commandStatusName(message.command_status()) + ").",
                "Press Back on the Flipper, then retry.");
            return;
        }
        locked_ = false;
        finishOp();
        return;
    }

    if(op_ == Op::Read) {
        if(message.command_status() != PB::CommandStatus::OK) {
            opError("rpc", "Could not read the script (" + commandStatusName(message.command_status()) + ").",
                "The file may have been removed.");
            return;
        }
        if(message.has_storage_read_response() && message.storage_read_response().has_file()) {
            const auto chunk = QByteArray::fromStdString(message.storage_read_response().file().data());
            if(readBuffer_.size() + chunk.size() > kMaxPreviewBytes)
                readBuffer_ = readBuffer_.left(kMaxPreviewBytes);
            else readBuffer_.append(chunk);
        }
        if(message.has_next()) return;
        script_ = {{"path", targetPath_}, {"text", QString::fromUtf8(readBuffer_)},
            {"bytes", static_cast<qint64>(readBuffer_.size())}};
        readBuffer_.clear();
        finishOp();
    }
}
}
