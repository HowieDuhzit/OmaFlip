#include "files.h"
#include "model.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QVector>
#include <algorithm>
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace omaflip {
namespace {
bool printableAscii(const QString& text) {
    for(const QChar c : text) {
        const auto u = c.unicode();
        if(u < 0x20 || u > 0x7e) return false;
    }
    return true;
}

void sortEntries(QJsonArray& entries) {
    QVector<QJsonObject> items;
    items.reserve(entries.size());
    for(const auto& value : entries) items.append(value.toObject());
    std::sort(items.begin(), items.end(), [](const QJsonObject& a, const QJsonObject& b) {
        const bool aDir = a["type"].toString() == "dir";
        const bool bDir = b["type"].toString() == "dir";
        if(aDir != bDir) return aDir;
        return a["name"].toString().compare(b["name"].toString(), Qt::CaseInsensitive) < 0;
    });
    QJsonArray sorted;
    for(const auto& item : items) sorted.append(item);
    entries = sorted;
}

const QStringList kTextExtensions{
    "txt", "md", "json", "csv", "log", "ir", "sub", "nfc", "rfid", "ibtn",
    "fmf", "js", "py", "sh", "ini", "cfg", "xml", "html", "css", "nsh",
    "conf", "playlist", "seq", "txts"
};
}

QString normalizeDevicePath(const QString& path) {
    QString text = path;
    text.replace('\\', '/');
    if(!text.startsWith('/')) return {};
    const auto parts = text.split('/', Qt::SkipEmptyParts);
    QStringList out;
    for(const auto& part : parts) {
        if(part == ".") continue;
        if(part == "..") {
            if(out.isEmpty()) return {};
            out.removeLast();
            continue;
        }
        if(part.size() > kMaxNameLength || !printableAscii(part)) return {};
        out.append(part);
    }
    if(out.isEmpty()) return "/";
    if(out[0] != "ext" && out[0] != "int" && out[0] != "any") return {};
    const auto joined = "/" + out.join('/');
    if(joined.size() > kMaxPathLength) return {};
    return joined;
}

QString parentDevicePath(const QString& path) {
    const auto normalized = normalizeDevicePath(path);
    if(normalized.isEmpty() || normalized == "/") return {};
    const int slash = normalized.lastIndexOf('/');
    if(slash <= 0) return "/";
    return normalized.left(slash);
}

QString joinDevicePath(const QString& dir, const QString& name) {
    if(name.isEmpty() || name == "." || name == ".." || name.contains('/') || name.contains('\\'))
        return {};
    if(dir == "/" || dir.isEmpty()) return normalizeDevicePath("/" + name);
    return normalizeDevicePath(dir + "/" + name);
}

QString deviceBasename(const QString& path) {
    const auto normalized = normalizeDevicePath(path);
    if(normalized.isEmpty() || normalized == "/") return {};
    return normalized.section('/', -1);
}

QString sanitizeHostName(const QString& name) {
    QString out = QFileInfo(name).fileName();
    out.replace('/', '_');
    out.replace('\\', '_');
    out.remove('\0');
    if(out.isEmpty() || out == "." || out == "..") return {};
    return out;
}

bool looksLikeText(const QByteArray& data) {
    if(data.isEmpty()) return true;
    int odd = 0;
    for(const auto byte : data) {
        const auto c = static_cast<unsigned char>(byte);
        if(c == 0) return false;
        if(c < 0x09) return false;
        if(c < 0x20 && c != '\t' && c != '\n' && c != '\r') ++odd;
    }
    return odd * 20 < data.size();
}

QString previewKindFor(const QString& name, const QByteArray& data) {
    const auto ext = QFileInfo(name).suffix().toLower();
    if(kTextExtensions.contains(ext) || looksLikeText(data)) return "text";
    return "binary";
}

QString hostDownloadDir(QString& error) {
    const auto downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if(downloads.isEmpty()) { error = "No Downloads directory is available."; return {}; }
    QDir dir(downloads + "/OmaFlip");
    if(!dir.exists() && !dir.mkpath(".")) { error = "Could not create the OmaFlip download folder."; return {}; }
    return dir.absolutePath();
}

FileSession::FileSession(QObject* parent) : QObject(parent) {
    timeout_.setSingleShot(true);
    dtrTimer_.setSingleShot(true);
    connect(&timeout_, &QTimer::timeout, this, [this] {
        if(phase_ == Phase::Ready && op_ == Op::None) return;
        if(phase_ == Phase::Stopping) { closePort(); emit stopped(); return; }
        if(phase_ == Phase::Ready) {
            opError("cli_timeout", "The Flipper did not finish the file operation in time.",
                "Retry the operation. Large files may need another attempt.");
            return;
        }
        fail({{"code", "cli_timeout"}, {"operation", "Start files session"}, {"path", port_},
            {"reason", "The Flipper did not enter an RPC files session in time."},
            {"suggestion", "Unlock the Flipper, close other serial clients, and retry."}});
    });
    connect(&dtrTimer_, &QTimer::timeout, this, [this] {
        if(fd_ < 0) return;
        int flags = TIOCM_DTR | TIOCM_RTS;
        if(ioctl(fd_, TIOCMBIS, &flags) < 0 && errno != ENOTTY)
            fail(systemError("Enable serial session", port_, errno));
    });
}
FileSession::~FileSession() { closePort(); }

bool FileSession::busy() const { return op_ != Op::None || phase_ == Phase::Banner || phase_ == Phase::StartRpc || phase_ == Phase::Ping; }

QJsonObject FileSession::snapshot() const {
    return {{"open", phase_ != Phase::Done}, {"path", path_}, {"parent", parentDevicePath(path_)},
        {"entries", entries_}, {"busy", busy()}, {"error", error_}, {"errorCode", errorCode_},
        {"preview", preview_}, {"transfer", transfer_}, {"target", targetPath_},
        {"hostPath", hostPath_}, {"lastOp", lastOp_}};
}

void FileSession::closePort() {
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

void FileSession::start(const QString& port) {
    closePort();
    port_ = port; input_.clear(); output_.clear(); commandId_ = 0;
    path_ = "/ext"; pendingList_ = "/ext"; error_.clear(); entries_ = {}; preview_ = {}; transfer_ = {};
    listedExt_ = false;
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
    connect(reader_, &QSocketNotifier::activated, this, &FileSession::readReady);
    connect(writer_, &QSocketNotifier::activated, this, &FileSession::drainWrite);
    int flags = TIOCM_DTR | TIOCM_RTS;
    if(ioctl(fd_, TIOCMBIC, &flags) < 0 && errno != ENOTTY) {
        fail(systemError("Reset serial session", port, errno)); return;
    }
    dtrTimer_.start(50);
    timeout_.start(5000);
    emit changed();
}

void FileSession::stop() {
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

void FileSession::send(const QByteArray& bytes) {
    output_.append(bytes);
    if(phase_ != Phase::Ready && phase_ != Phase::Stopping) timeout_.start(5000);
    writer_->setEnabled(true);
    drainWrite();
}
void FileSession::sendRpc(const PB::Main& message) { send(encodeDelimited(message)); }
void FileSession::drainWrite() {
    while(!output_.isEmpty()) {
        const auto n = ::write(fd_, output_.constData(), static_cast<size_t>(output_.size()));
        if(n < 0) {
            if(errno == EINTR) continue;
            if(errno == EAGAIN) return;
            fail(systemError("Write files command", port_, errno)); return;
        }
        if(n == 0) return;
        output_.remove(0, n);
    }
    if(op_ == Op::Write && file_.isOpen()) fillWrite();
    if(writer_) writer_->setEnabled(!output_.isEmpty());
}

void FileSession::readReady() {
    char bytes[4096];
    for(;;) {
        const auto count = ::read(fd_, bytes, sizeof(bytes));
        if(count < 0) {
            if(errno == EINTR) continue;
            if(errno == EAGAIN) break;
            fail(systemError("Read files session", port_, errno)); return;
        }
        if(count == 0) { fail(systemError("Serial device disconnected", port_, ENODEV)); return; }
        input_.append(bytes, count);
        if(input_.size() > 1024 * 1024) {
            fail({{"code", "response_too_large"}, {"operation", "Read files session"},
                {"path", port_}, {"reason", "Files RPC input exceeded the 1 MiB bound."},
                {"suggestion", "Close Files and retry."}}); return;
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
            fail({{"code", "rpc_frame"}, {"operation", "Read files session"}, {"path", port_},
                {"reason", "RPC frame was corrupt or too large."},
                {"suggestion", "Close Files and retry."}}); return;
        }
        handleRpc(message);
        if(phase_ == Phase::Done) return;
    }
}

void FileSession::beginOp(Op op, quint32 timeoutMs) {
    op_ = op;
    error_.clear();
    timeout_.start(static_cast<int>(timeoutMs));
    emit changed();
}

void FileSession::finishOp() {
    op_ = Op::None;
    afterStat_ = AfterStat::None;
    previewOnly_ = false;
    error_.clear();
    errorCode_.clear();
    timeout_.stop();
    if(file_.isOpen()) file_.close();
    emit changed();
}

void FileSession::opError(const QString& code, const QString& reason, const QString& suggestion) {
    errorCode_ = code;
    error_ = reason;
    if(!suggestion.isEmpty() && !reason.contains(suggestion)) error_ = reason + " " + suggestion;
    if(file_.isOpen()) {
        const auto path = file_.fileName();
        file_.close();
        if(path.endsWith(".part")) QFile::remove(path);
    }
    transfer_ = {};
    op_ = Op::None;
    afterStat_ = AfterStat::None;
    previewOnly_ = false;
    timeout_.stop();
    emit changed();
}

void FileSession::fail(const QJsonObject& error) {
    closePort();
    emit failed(error);
}

void FileSession::beginList(const QString& path) {
    path_ = path;
    entries_ = {};
    preview_ = {};
    beginOp(Op::List);
    sendRpc(storageListRequest(++commandId_, path));
}

void FileSession::list(const QString& path) {
    const auto normalized = normalizeDevicePath(path);
    if(normalized.isEmpty()) {
        opError("invalid_path", "That device path is not allowed.", "Stay under /ext, /int, or /any.");
        return;
    }
    if(phase_ != Phase::Ready) { pendingList_ = normalized; return; }
    if(op_ != Op::None) {
        opError("busy", "A file operation is already running.", "Wait for it to finish.");
        return;
    }
    beginList(normalized);
}

void FileSession::preview(const QString& path) {
    const auto normalized = normalizeDevicePath(path);
    if(normalized.isEmpty() || normalized == "/") {
        opError("invalid_path", "Choose a file to preview.", "Open a file under /ext or /int.");
        return;
    }
    if(phase_ != Phase::Ready || op_ != Op::None) {
        opError("busy", "A file operation is already running.", "Wait for it to finish.");
        return;
    }
    targetPath_ = normalized;
    hostPath_.clear();
    overwrite_ = false;
    previewOnly_ = true;
    lastOp_ = "preview";
    afterStat_ = AfterStat::Download;
    beginOp(Op::Stat);
    sendRpc(storageStatRequest(++commandId_, normalized));
}

void FileSession::download(const QString& path, const QString& hostPath, bool overwrite) {
    const auto normalized = normalizeDevicePath(path);
    if(normalized.isEmpty() || normalized == "/") {
        opError("invalid_path", "Choose a file to download.", "Open a file under /ext or /int.");
        return;
    }
    if(phase_ != Phase::Ready || op_ != Op::None) {
        opError("busy", "A file operation is already running.", "Wait for it to finish.");
        return;
    }
    targetPath_ = normalized;
    hostPath_ = hostPath;
    overwrite_ = overwrite;
    previewOnly_ = false;
    lastOp_ = "download";
    afterStat_ = AfterStat::Download;
    beginOp(Op::Stat);
    sendRpc(storageStatRequest(++commandId_, normalized));
}

void FileSession::upload(const QString& path, const QString& hostPath, bool overwrite) {
    const auto normalized = normalizeDevicePath(path);
    if(normalized.isEmpty() || normalized == "/") {
        opError("invalid_path", "Choose a destination file on the Flipper.", "Drop onto a folder under /ext or /int.");
        return;
    }
    if(hostPath.isEmpty() || !QFileInfo::exists(hostPath) || !QFileInfo(hostPath).isFile()) {
        opError("invalid_host", "The host file is missing or not a regular file.", "Drop a real file, not a folder.");
        return;
    }
    const auto size = QFileInfo(hostPath).size();
    if(size > kMaxTransferBytes) {
        opError("too_large", "Uploads are limited to 32 MiB.", "Copy a smaller file.");
        return;
    }
    if(phase_ != Phase::Ready || op_ != Op::None) {
        opError("busy", "A file operation is already running.", "Wait for it to finish.");
        return;
    }
    targetPath_ = normalized;
    hostPath_ = hostPath;
    overwrite_ = overwrite;
    transferTotal_ = size;
    lastOp_ = "upload";
    afterStat_ = AfterStat::Upload;
    beginOp(Op::Stat);
    sendRpc(storageStatRequest(++commandId_, normalized));
}

void FileSession::mkdir(const QString& path) {
    const auto normalized = normalizeDevicePath(path);
    if(normalized.isEmpty() || normalized == "/") {
        opError("invalid_path", "Choose a folder name.", "Use a name under /ext or /int.");
        return;
    }
    if(phase_ != Phase::Ready || op_ != Op::None) {
        opError("busy", "A file operation is already running.", "Wait for it to finish.");
        return;
    }
    targetPath_ = normalized;
    beginOp(Op::Mkdir);
    sendRpc(storageMkdirRequest(++commandId_, normalized));
}

void FileSession::rename(const QString& oldPath, const QString& newPath) {
    const auto from = normalizeDevicePath(oldPath);
    const auto to = normalizeDevicePath(newPath);
    if(from.isEmpty() || to.isEmpty() || from == "/" || to == "/") {
        opError("invalid_path", "Choose a valid rename destination.", "Stay under /ext, /int, or /any.");
        return;
    }
    if(phase_ != Phase::Ready || op_ != Op::None) {
        opError("busy", "A file operation is already running.", "Wait for it to finish.");
        return;
    }
    targetPath_ = to;
    beginOp(Op::Rename);
    sendRpc(storageRenameRequest(++commandId_, from, to));
}

void FileSession::remove(const QString& path, bool recursive) {
    const auto normalized = normalizeDevicePath(path);
    if(normalized.isEmpty() || normalized == "/") {
        opError("invalid_path", "Choose an item to delete.", "The storage root cannot be deleted.");
        return;
    }
    if(phase_ != Phase::Ready || op_ != Op::None) {
        opError("busy", "A file operation is already running.", "Wait for it to finish.");
        return;
    }
    targetPath_ = normalized;
    beginOp(Op::Delete);
    sendRpc(storageDeleteRequest(++commandId_, normalized, recursive));
}

void FileSession::beginRead() {
    readBuffer_.clear();
    transferBytes_ = 0;
    lastPublishedBytes_ = 0;
    beginOp(Op::Read, previewOnly_ ? 8000 : 30000);
    sendRpc(storageReadRequest(++commandId_, targetPath_));
}

void FileSession::beginWrite() {
    file_.setFileName(hostPath_);
    if(!file_.open(QIODevice::ReadOnly)) {
        opError("io", "Could not read the host file.", "Check the file still exists and retry.");
        return;
    }
    transferBytes_ = 0;
    lastPublishedBytes_ = 0;
    transfer_ = {{"direction", "upload"}, {"path", targetPath_}, {"bytes", 0},
        {"total", transferTotal_}, {"done", false}};
    beginOp(Op::Write, 30000);
    ++commandId_;
    fillWrite();
    if(writer_) writer_->setEnabled(true);
    drainWrite();
}

void FileSession::fillWrite() {
    if(op_ != Op::Write || !file_.isOpen()) return;
    while(output_.size() < 4096) {
        QByteArray chunk(kStorageChunk, Qt::Uninitialized);
        const auto n = file_.read(chunk.data(), kStorageChunk);
        if(n < 0) {
            opError("io", "Could not read the host file during upload.", "Retry the upload.");
            return;
        }
        chunk.resize(static_cast<int>(n));
        const bool hasNext = !file_.atEnd() && n > 0;
        output_.append(encodeDelimited(storageWriteRequest(commandId_, targetPath_, chunk, hasNext)));
        transferBytes_ += n;
        publishTransfer(false);
        if(!hasNext) {
            file_.close();
            return;
        }
    }
}

void FileSession::publishTransfer(bool force) {
    if(!force && transferBytes_ - lastPublishedBytes_ < 4096) return;
    lastPublishedBytes_ = transferBytes_;
    transfer_.insert("bytes", transferBytes_);
    transfer_.insert("total", transferTotal_);
    timeout_.start(op_ == Op::Write || op_ == Op::Read ? 30000 : 8000);
    emit changed();
}

void FileSession::handleRpc(const PB::Main& message) {
    if(phase_ == Phase::Ping) {
        if(message.command_id() != commandId_ || !message.has_system_ping_response()) {
            fail({{"code", "rpc_ping"}, {"operation", "Start files session"}, {"path", port_},
                {"reason", "RPC ping failed while starting the files session."},
                {"suggestion", "Unlock the Flipper and retry Files."}}); return;
        }
        phase_ = Phase::Ready;
        timeout_.stop();
        emit ready();
        beginList(pendingList_.isEmpty() ? "/ext" : pendingList_);
        pendingList_.clear();
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
            if(!listedExt_ && path_ == "/ext") {
                listedExt_ = true;
                beginList("/");
                return;
            }
            opError("rpc", "Could not list " + path_ + " (" + commandStatusName(message.command_status()) + ").",
                "The SD card may be missing, or the path does not exist.");
            return;
        }
        listedExt_ = true;
        if(message.has_storage_list_response()) {
            const auto& list = message.storage_list_response();
            for(int i = 0; i < list.file_size(); ++i) {
                if(entries_.size() >= kMaxListingEntries) break;
                const auto& file = list.file(i);
                const auto name = QString::fromStdString(file.name());
                if(name.isEmpty() || name == "." || name == "..") continue;
                entries_.append(QJsonObject{{"name", name},
                    {"type", file.type() == PB_Storage::File_FileType_DIR ? "dir" : "file"},
                    {"size", static_cast<qint64>(file.size())}});
            }
        }
        if(message.has_next()) return;
        sortEntries(entries_);
        finishOp();
        return;
    }

    if(op_ == Op::Stat) {
        const bool missing = message.command_status() == PB::CommandStatus::ERROR_STORAGE_NOT_EXIST;
        const bool ok = message.command_status() == PB::CommandStatus::OK;
        if(afterStat_ == AfterStat::Upload) {
            if(ok && message.has_storage_stat_response() && message.storage_stat_response().has_file()
                && message.storage_stat_response().file().type() == PB_Storage::File_FileType_DIR) {
                opError("is_dir", "That destination is a folder.", "Drop onto the folder, not over it.");
                return;
            }
            if(ok && !overwrite_) {
                opError("exists", "A file with that name already exists on the Flipper.",
                    "Confirm overwrite to replace it.");
                return;
            }
            if(ok || missing) { beginWrite(); return; }
            opError("rpc", "Could not inspect the destination (" + commandStatusName(message.command_status()) + ").",
                "Retry the upload.");
            return;
        }
        if(!ok || !message.has_storage_stat_response() || !message.storage_stat_response().has_file()) {
            opError("rpc", "Could not inspect " + targetPath_ + " (" + commandStatusName(message.command_status()) + ").",
                "The file may have been removed.");
            return;
        }
        const auto& file = message.storage_stat_response().file();
        if(file.type() == PB_Storage::File_FileType_DIR) {
            opError("is_dir", "That item is a folder.", "Open the folder instead of downloading it.");
            return;
        }
        transferTotal_ = file.size();
        if(transferTotal_ > kMaxTransferBytes) {
            opError("too_large", "Transfers are limited to 32 MiB.", "Copy a smaller file.");
            return;
        }
        if(previewOnly_) {
            if(transferTotal_ > kMaxPreviewBytes) {
                preview_ = {{"path", targetPath_}, {"kind", "too_large"}, {"bytes", transferTotal_},
                    {"text", "File is " + bytesText(static_cast<quint64>(transferTotal_)) + ". Download it to open."}};
                finishOp();
                return;
            }
            beginRead();
            return;
        }
        QString dest = hostPath_;
        if(dest.isEmpty()) {
            QString dirError;
            const auto dir = hostDownloadDir(dirError);
            const auto name = sanitizeHostName(deviceBasename(targetPath_));
            if(dir.isEmpty() || name.isEmpty()) {
                opError("io", dirError.isEmpty() ? "Could not choose a download name." : dirError,
                    "Check that Downloads exists.");
                return;
            }
            dest = dir + "/" + name;
            hostPath_ = dest;
        }
        if(QFileInfo::exists(dest) && !overwrite_) {
            opError("exists", "A local file with that name already exists.",
                "Confirm overwrite to replace " + dest + ".");
            return;
        }
        file_.setFileName(dest + ".part");
        if(!file_.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            opError("io", "Could not create the download file.", "Check Downloads/OmaFlip is writable.");
            return;
        }
        transfer_ = {{"direction", "download"}, {"path", targetPath_}, {"hostPath", dest},
            {"bytes", 0}, {"total", transferTotal_}, {"done", false}};
        beginRead();
        return;
    }

    if(op_ == Op::Read) {
        if(message.command_status() != PB::CommandStatus::OK) {
            opError("rpc", "Read failed (" + commandStatusName(message.command_status()) + ").",
                "Retry the transfer.");
            return;
        }
        if(message.has_storage_read_response() && message.storage_read_response().has_file()) {
            const auto chunk = QByteArray::fromStdString(message.storage_read_response().file().data());
            transferBytes_ += chunk.size();
            if(previewOnly_) {
                if(readBuffer_.size() + chunk.size() > kMaxPreviewBytes)
                    readBuffer_ = readBuffer_.left(kMaxPreviewBytes);
                else readBuffer_.append(chunk);
            } else if(file_.isOpen()) {
                if(file_.write(chunk) != chunk.size()) {
                    opError("io", "Could not write the downloaded file.", "Check free disk space and retry.");
                    return;
                }
            }
            publishTransfer(false);
        }
        if(message.has_next()) return;
        if(previewOnly_) {
            preview_ = {{"path", targetPath_}, {"kind", previewKindFor(targetPath_, readBuffer_)},
                {"bytes", static_cast<qint64>(readBuffer_.size())},
                {"text", previewKindFor(targetPath_, readBuffer_) == "text"
                    ? QString::fromUtf8(readBuffer_) : "Binary file · " + bytesText(static_cast<quint64>(readBuffer_.size()))}};
            readBuffer_.clear();
            transfer_ = {};
            finishOp();
            return;
        }
        const auto finalPath = hostPath_;
        const auto partPath = file_.fileName();
        file_.close();
        QFile::remove(finalPath);
        if(!QFile::rename(partPath, finalPath)) {
            opError("io", "Could not finalize the download.", "Retry the download.");
            return;
        }
        transfer_.insert("bytes", transferBytes_);
        transfer_.insert("done", true);
        transfer_.insert("hostPath", finalPath);
        lastPublishedBytes_ = transferBytes_;
        finishOp();
        return;
    }

    if(op_ == Op::Write) {
        if(message.command_status() != PB::CommandStatus::OK) {
            opError("rpc", "Upload failed (" + commandStatusName(message.command_status()) + ").",
                "The SD card may be full or the path is invalid.");
            return;
        }
        transfer_.insert("bytes", transferBytes_);
        transfer_.insert("done", true);
        const auto refresh = parentDevicePath(targetPath_);
        finishOp();
        beginList(refresh.isEmpty() ? path_ : refresh);
        return;
    }

    if(op_ == Op::Mkdir || op_ == Op::Rename || op_ == Op::Delete) {
        if(message.command_status() != PB::CommandStatus::OK) {
            opError("rpc", "The file operation failed (" + commandStatusName(message.command_status()) + ").",
                "Check the name and whether the item is in use on the Flipper.");
            return;
        }
        const auto refresh = parentDevicePath(targetPath_);
        finishOp();
        beginList(refresh.isEmpty() ? path_ : refresh);
    }
}
}
