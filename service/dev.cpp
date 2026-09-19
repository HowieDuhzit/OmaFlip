#include "dev.h"
#include "firmware.h"
#include "model.h"
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace omaflip {
QString sanitizeAppId(const QString& id) {
    const auto text = id.trimmed().toLower();
    if(text.size() < 1 || text.size() > 32) return {};
    static const QRegularExpression valid("^[a-z][a-z0-9_]*$");
    if(!valid.match(text).hasMatch()) return {};
    return text;
}

QJsonObject parseApplicationFam(const QByteArray& text) {
    const auto source = QString::fromUtf8(text);
    const auto start = source.indexOf("App(");
    if(start < 0) return {};
    const auto block = source.mid(start, 2048);
    QJsonObject fam;
    const auto capture = [&](const QString& key) {
        const QRegularExpression re(key + "\\s*=\\s*\"([^\"]+)\"");
        const auto match = re.match(block);
        if(match.hasMatch()) fam.insert(key, match.captured(1));
    };
    capture("appid");
    capture("name");
    capture("fap_category");
    if(fam.value("appid").toString().isEmpty()) return {};
    if(fam.value("fap_category").toString().isEmpty()) fam.insert("fap_category", "Misc");
    return fam;
}

QStringList ufbtUpdateArgs(const QString& origin, const QString& version) {
    const auto channel = version.contains("dev", Qt::CaseInsensitive) ? QString("dev") : QString("release");
    QStringList args{"update", "--channel=" + channel};
    if(originKind(origin) == "Momentum")
        args.append("--index-url=https://up.momentum-fw.dev/firmware/directory.json");
    return args;
}

QString findUfbt(const QStringList& extraDirs) {
    if(!extraDirs.isEmpty()) {
        const auto extra = QStandardPaths::findExecutable("ufbt", extraDirs);
        if(!extra.isEmpty()) return extra;
    }
    auto found = QStandardPaths::findExecutable("ufbt");
    if(!found.isEmpty()) return found;
    return QStandardPaths::findExecutable("ufbt", {QDir::home().filePath(".local/bin")});
}

QStringList ufbtCommand(const QStringList& extraDirs) {
    const auto found = findUfbt(extraDirs);
    if(!found.isEmpty()) return {found};
    const auto python = QStandardPaths::findExecutable("python3");
    if(!python.isEmpty()) return {python, "-m", "ufbt"};
    return {};
}

QString defaultDevProject() {
    const auto documents = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    if(documents.isEmpty()) return {};
    return documents + "/OmaFlip/projects";
}

QString findBuiltFap(const QString& projectDir) {
    QDir dist(projectDir + "/dist");
    if(!dist.exists()) return {};
    QString fallback;
    for(const auto& info : dist.entryInfoList({"*.fap"}, QDir::Files, QDir::Name)) {
        if(info.fileName().endsWith("_d.fap", Qt::CaseInsensitive)) {
            if(fallback.isEmpty()) fallback = info.absoluteFilePath();
            continue;
        }
        return info.absoluteFilePath();
    }
    return fallback;
}

bool inspectKindAllowed(const QString& kind) {
    return kind == "ping" || kind == "protobuf" || kind == "storage" || kind == "lock"
        || kind == "datetime" || kind == "device" || kind == "power" || kind == "property"
        || kind == "alert" || kind == "desktop";
}

DevSession::DevSession(QObject* parent) : QObject(parent) {
    timeout_.setSingleShot(true);
    dtrTimer_.setSingleShot(true);
    process_.setProcessChannelMode(QProcess::MergedChannels);
    connect(&timeout_, &QTimer::timeout, this, [this] {
        if(phase_ == Phase::Ready && op_ == Op::None) return;
        if(phase_ == Phase::Stopping) { closePort(); emit stopped(); return; }
        if(phase_ == Phase::Ready) {
            opError("cli_timeout", "The Flipper did not finish the developer operation in time.",
                "Retry. A large FAP upload can take longer.");
            return;
        }
        fail({{"code", "cli_timeout"}, {"operation", "Start developer session"}, {"path", port_},
            {"reason", "The Flipper did not enter an RPC developer session in time."},
            {"suggestion", "Unlock the Flipper, close other serial clients, and retry."}});
    });
    connect(&dtrTimer_, &QTimer::timeout, this, [this] {
        if(fd_ < 0) return;
        int flags = TIOCM_DTR | TIOCM_RTS;
        if(ioctl(fd_, TIOCMBIS, &flags) < 0 && errno != ENOTTY)
            fail(systemError("Enable serial session", port_, errno));
    });
    connect(&process_, &QProcess::readyRead, this, [this] {
        appendLog(QString::fromUtf8(process_.readAll()));
        emit changed();
    });
    connect(&process_, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        appendLog(QString::fromUtf8(process_.readAll()));
        refreshProject();
        if(status != QProcess::NormalExit || code != 0)
            opError("ufbt", "ufbt exited with status " + QString::number(code) + ".",
                "Read the log. Install ufbt with pip if the tool is missing.");
        else {
            error_.clear(); errorCode_.clear();
            emit changed();
        }
    });
    connect(&process_, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if(error == QProcess::FailedToStart)
            opError("ufbt", "Could not start ufbt.",
                "Install it with python3 -m pip install --upgrade ufbt and ensure it is on PATH.");
    });
}

DevSession::~DevSession() {
    if(process_.state() != QProcess::NotRunning) {
        process_.terminate();
        process_.waitForFinished(1000);
        if(process_.state() != QProcess::NotRunning) process_.kill();
    }
    closePort();
}

bool DevSession::busy() const {
    return op_ != Op::None || process_.state() != QProcess::NotRunning
        || phase_ == Phase::Banner || phase_ == Phase::StartRpc || phase_ == Phase::Ping;
}

QJsonObject DevSession::snapshot() const {
    const auto command = ufbtCommand();
    return {{"open", phase_ != Phase::Done}, {"ready", phase_ == Phase::Ready}, {"busy", busy()},
        {"error", error_}, {"errorCode", errorCode_},
        {"ufbt", findUfbt()}, {"ufbtCommand", command.join(' ')},
        {"defaultProject", defaultDevProject()},
        {"project", project_}, {"fam", fam_}, {"fap", fap_}, {"log", log_}, {"inspect", inspect_}};
}

void DevSession::setOrigin(const QString& origin, const QString& version) {
    origin_ = origin; version_ = version;
}

void DevSession::appendLog(const QString& text) {
    if(text.isEmpty()) return;
    log_.append(text);
    if(!log_.endsWith('\n') && text.contains('\n')) { /* keep raw */ }
    if(log_.size() > kMaxDevLog) log_ = log_.right(kMaxDevLog);
}

void DevSession::refreshProject() {
    fam_ = {}; fap_.clear();
    if(project_.isEmpty()) return;
    QFile fam(project_ + "/application.fam");
    if(fam.open(QIODevice::ReadOnly)) fam_ = parseApplicationFam(fam.readAll());
    fap_ = findBuiltFap(project_);
}

void DevSession::setProject(const QString& path) {
    const auto info = QFileInfo(path);
    if(path.trimmed().isEmpty()) {
        project_.clear(); fam_ = {}; fap_.clear();
        emit changed();
        return;
    }
    if(!info.isAbsolute() || path.contains("..")) {
        opError("invalid_path", "Choose an absolute project folder.", "Pick a directory you own.");
        return;
    }
    project_ = info.absoluteFilePath();
    refreshProject();
    emit changed();
}

void DevSession::closePort() {
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

void DevSession::start(const QString& port) {
    closePort();
    port_ = port; input_.clear(); output_.clear(); commandId_ = 0;
    error_.clear(); errorCode_.clear(); inspect_.clear();
    refreshProject();
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
    connect(reader_, &QSocketNotifier::activated, this, &DevSession::readReady);
    connect(writer_, &QSocketNotifier::activated, this, &DevSession::drainWrite);
    int flags = TIOCM_DTR | TIOCM_RTS;
    if(ioctl(fd_, TIOCMBIC, &flags) < 0 && errno != ENOTTY) {
        fail(systemError("Reset serial session", port, errno)); return;
    }
    dtrTimer_.start(50);
    timeout_.start(5000);
    emit changed();
}

void DevSession::stop() {
    if(process_.state() != QProcess::NotRunning) {
        process_.terminate();
        process_.waitForFinished(1000);
        if(process_.state() != QProcess::NotRunning) process_.kill();
    }
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

void DevSession::send(const QByteArray& bytes) {
    output_.append(bytes);
    if(phase_ != Phase::Ready && phase_ != Phase::Stopping) timeout_.start(5000);
    writer_->setEnabled(true);
    drainWrite();
}
void DevSession::sendRpc(const PB::Main& message) { send(encodeDelimited(message)); }
void DevSession::drainWrite() {
    while(!output_.isEmpty()) {
        const auto n = ::write(fd_, output_.constData(), static_cast<size_t>(output_.size()));
        if(n < 0) {
            if(errno == EINTR) continue;
            if(errno == EAGAIN) return;
            fail(systemError("Write developer command", port_, errno)); return;
        }
        if(n == 0) return;
        output_.remove(0, n);
    }
    if(op_ == Op::Write) fillWrite();
    if(writer_) writer_->setEnabled(!output_.isEmpty());
}

void DevSession::readReady() {
    char bytes[4096];
    for(;;) {
        const auto count = ::read(fd_, bytes, sizeof(bytes));
        if(count < 0) {
            if(errno == EINTR) continue;
            if(errno == EAGAIN) break;
            fail(systemError("Read developer session", port_, errno)); return;
        }
        if(count == 0) { fail(systemError("Serial device disconnected", port_, ENODEV)); return; }
        input_.append(bytes, count);
        if(input_.size() > 1024 * 1024) {
            fail({{"code", "response_too_large"}, {"operation", "Read developer session"},
                {"path", port_}, {"reason", "Developer RPC input exceeded the 1 MiB bound."},
                {"suggestion", "Close Dev and retry."}}); return;
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
            fail({{"code", "rpc_frame"}, {"operation", "Read developer session"}, {"path", port_},
                {"reason", "RPC frame was corrupt or too large."},
                {"suggestion", "Close Dev and retry."}}); return;
        }
        handleRpc(message);
        if(phase_ == Phase::Done) return;
    }
}

void DevSession::beginOp(Op op, quint32 timeoutMs) {
    op_ = op; error_.clear(); errorCode_.clear();
    timeout_.start(static_cast<int>(timeoutMs));
    emit changed();
}
void DevSession::finishOp() {
    op_ = Op::None; timeout_.stop();
    if(file_.isOpen()) file_.close();
    emit changed();
}
void DevSession::opError(const QString& code, const QString& reason, const QString& suggestion) {
    errorCode_ = code;
    error_ = suggestion.isEmpty() ? reason : reason + " " + suggestion;
    if(file_.isOpen()) file_.close();
    op_ = Op::None; timeout_.stop();
    emit changed();
}
void DevSession::fail(const QJsonObject& error) { closePort(); emit failed(error); }

void DevSession::runUfbt(const QStringList& args, int timeoutMs) {
    if(process_.state() != QProcess::NotRunning) {
        opError("busy", "ufbt is already running.", "Wait for it to finish.");
        return;
    }
    const auto command = ufbtCommand();
    if(command.isEmpty()) {
        opError("ufbt", "ufbt is not installed.",
            "Choose Install ufbt, or add it to PATH.");
        return;
    }
    if(project_.isEmpty()) {
        opError("invalid_path", "Set a project folder first.", "Use an absolute path.");
        return;
    }
    QDir dir(project_);
    if(!dir.exists() && !dir.mkpath(".")) {
        opError("io", "Could not create the project folder.", "Pick a writable directory.");
        return;
    }
    appendLog("$ " + command.join(' ') + (args.isEmpty() ? QString() : " " + args.join(' ')) + "\n");
    process_.setWorkingDirectory(project_);
    process_.setProgram(command.first());
    QStringList all = command.mid(1);
    all.append(args);
    process_.setArguments(all);
    process_.start();
    Q_UNUSED(timeoutMs);
    emit changed();
}

void DevSession::installUfbt() {
    if(process_.state() != QProcess::NotRunning) {
        opError("busy", "A developer command is already running.", "Wait for it to finish.");
        return;
    }
    const auto pipx = QStandardPaths::findExecutable("pipx");
    if(!pipx.isEmpty()) {
        appendLog("$ pipx install ufbt\n");
        process_.setWorkingDirectory(QDir::homePath());
        process_.start(pipx, {"install", "ufbt"});
        emit changed();
        return;
    }
    const auto python = QStandardPaths::findExecutable("python3");
    if(python.isEmpty()) {
        opError("ufbt", "Neither pipx nor python3 is on PATH.",
            "On Arch, install python-pipx, then retry Install ufbt.");
        return;
    }
    appendLog("$ python3 -m pip install --user --upgrade ufbt\n");
    process_.setWorkingDirectory(QDir::homePath());
    process_.start(python, {"-m", "pip", "install", "--user", "--upgrade", "ufbt"});
    emit changed();
}

void DevSession::createApp(const QString& appId) {
    const auto id = sanitizeAppId(appId);
    if(id.isEmpty()) {
        opError("invalid_path", "App IDs are lowercase letters, digits, and underscores.",
            "Example: hello_app");
        return;
    }
    if(QFile::exists(project_ + "/application.fam")) {
        opError("exists", "That folder already has an application.fam.",
            "Choose an empty directory for ufbt create.");
        return;
    }
    runUfbt({"create", "APPID=" + id}, 30000);
}

void DevSession::build() {
    if(!QFile::exists(project_ + "/application.fam")) {
        opError("invalid_path", "No application.fam in the project folder.",
            "Create a template or open an existing app.");
        return;
    }
    runUfbt({}, 600000);
}

void DevSession::lint() {
    if(!QFile::exists(project_ + "/application.fam")) {
        opError("invalid_path", "No application.fam in the project folder.",
            "Create a template first.");
        return;
    }
    runUfbt({"lint"}, 60000);
}

void DevSession::updateSdk() {
    runUfbt(ufbtUpdateArgs(origin_, version_), 900000);
}

void DevSession::fillWrite() {
    if(op_ != Op::Write || !file_.isOpen()) return;
    while(output_.size() < 4096) {
        QByteArray chunk(kStorageChunk, Qt::Uninitialized);
        const auto n = file_.read(chunk.data(), kStorageChunk);
        if(n < 0) {
            opError("io", "Could not read the built FAP.", "Rebuild and retry Deploy.");
            return;
        }
        chunk.resize(static_cast<int>(n));
        const bool hasNext = !file_.atEnd() && n > 0;
        output_.append(encodeDelimited(storageWriteRequest(commandId_, targetPath_, chunk, hasNext)));
        if(!hasNext) {
            file_.close();
            return;
        }
    }
}

void DevSession::deploy(bool overwrite) {
    if(phase_ != Phase::Ready || op_ != Op::None || process_.state() != QProcess::NotRunning) {
        opError("busy", "A developer operation is already running.", "Wait for it to finish.");
        return;
    }
    refreshProject();
    if(fap_.isEmpty()) {
        opError("invalid_path", "No FAP in dist/.", "Build the project first.");
        return;
    }
    const auto category = fam_.value("fap_category").toString("Misc");
    const auto dir = joinDevicePath("/ext/apps", category);
    const auto dest = joinDevicePath(dir.isEmpty() ? "/ext/apps/Misc" : dir, QFileInfo(fap_).fileName());
    if(dest.isEmpty() || !dest.startsWith("/ext/apps/")) {
        opError("invalid_path", "Could not build an install path under /ext/apps.",
            "Use an ASCII FAP name.");
        return;
    }
    QFile probe(fap_);
    if(!probe.open(QIODevice::ReadOnly)) {
        opError("io", "Could not read the built FAP.", "Rebuild and retry.");
        return;
    }
    if(probe.size() > kMaxTransferBytes) {
        opError("too_large", "FAPs are limited to 32 MiB.", "The build output is unexpectedly large.");
        return;
    }
    probe.close();
    if(!overwrite) {
        opError("confirm", "Confirm deploy to upload and start the built FAP.",
            "This replaces any app with the same name on the Flipper.");
        return;
    }
    targetPath_ = dest;
    file_.setFileName(fap_);
    if(!file_.open(QIODevice::ReadOnly)) {
        opError("io", "Could not read the built FAP.", "Rebuild and retry.");
        return;
    }
    beginOp(Op::Mkdir, 30000);
    sendRpc(storageMkdirRequest(++commandId_, parentDevicePath(dest)));
}

void DevSession::beginInspect(const QString& kind, const QString& arg) {
    inspectKind_ = kind;
    inspect_.clear();
    beginOp(Op::Inspect);
    if(kind == "ping") sendRpc(pingRequest(++commandId_, "omaflip"));
    else if(kind == "protobuf") sendRpc(protobufVersionRequest(++commandId_));
    else if(kind == "storage") {
        auto path = normalizeDevicePath(arg.isEmpty() ? QString("/ext") : arg);
        if(path.isEmpty()) path = "/ext";
        sendRpc(storageInfoRequest(++commandId_, path));
    } else if(kind == "lock") sendRpc(appLockStatusRequest(++commandId_));
    else if(kind == "datetime") sendRpc(systemGetDateTimeRequest(++commandId_));
    else if(kind == "device") sendRpc(systemDeviceInfoRequest(++commandId_));
    else if(kind == "power") sendRpc(systemPowerInfoRequest(++commandId_));
    else if(kind == "property") sendRpc(propertyGetRequest(++commandId_, arg.isEmpty() ? QString("firmware") : arg));
    else if(kind == "alert") sendRpc(systemPlayAudiovisualAlertRequest(++commandId_));
    else sendRpc(desktopIsLockedRequest(++commandId_));
}

void DevSession::inspect(const QString& kind, const QString& arg) {
    if(phase_ != Phase::Ready || op_ != Op::None) {
        opError("busy", "A developer operation is already running.", "Wait for it to finish.");
        return;
    }
    if(!inspectKindAllowed(kind)) {
        opError("invalid_request", "That inspector command is not enabled.",
            "GPIO writes, reboot, factory reset, and DFU flash stay disabled.");
        return;
    }
    beginInspect(kind, arg);
}

void DevSession::handleRpc(const PB::Main& message) {
    if(phase_ == Phase::Ping) {
        if(message.command_id() != commandId_ || !message.has_system_ping_response()) {
            fail({{"code", "rpc_ping"}, {"operation", "Start developer session"}, {"path", port_},
                {"reason", "RPC ping failed while starting the developer session."},
                {"suggestion", "Unlock the Flipper and retry Dev."}}); return;
        }
        phase_ = Phase::Ready;
        timeout_.stop();
        emit ready();
        emit changed();
        return;
    }
    if(phase_ == Phase::Stopping) { closePort(); emit stopped(); return; }
    if(phase_ != Phase::Ready || op_ == Op::None) return;
    if(message.command_id() != commandId_) return;

    if(op_ == Op::Mkdir) {
        if(message.command_status() != PB::CommandStatus::OK
            && message.command_status() != PB::CommandStatus::ERROR_STORAGE_EXIST) {
            opError("rpc", "Could not create the app folder (" + commandStatusName(message.command_status()) + ").",
                "Check the SD card.");
            return;
        }
        beginOp(Op::Write, 120000);
        ++commandId_;
        fillWrite();
        if(writer_) writer_->setEnabled(true);
        drainWrite();
        return;
    }

    if(op_ == Op::Write) {
        if(message.command_status() != PB::CommandStatus::OK) {
            opError("rpc", "Could not upload the FAP (" + commandStatusName(message.command_status()) + ").",
                "The SD card may be full.");
            return;
        }
        appendLog("Deployed " + targetPath_ + "\n");
        beginOp(Op::Start);
        sendRpc(appStartRequest(++commandId_, targetPath_, {}));
        return;
    }

    if(op_ == Op::Start) {
        if(message.command_status() != PB::CommandStatus::OK) {
            opError("rpc", "Uploaded but launch failed (" + commandStatusName(message.command_status()) + ").",
                "Unlock the Flipper and retry Deploy, or launch from Apps.");
            return;
        }
        appendLog("Started " + targetPath_ + "\n");
        finishOp();
        return;
    }

    if(op_ == Op::Inspect) {
        if(message.command_status() != PB::CommandStatus::OK) {
            opError("rpc", "Inspector request failed (" + commandStatusName(message.command_status()) + ").",
                "Try another read-only command.");
            return;
        }
        if(message.has_system_ping_response())
            inspect_ += "ping ok\n";
        if(message.has_system_protobuf_version_response()) {
            const auto& v = message.system_protobuf_version_response();
            inspect_ += QString("protobuf %1.%2\n").arg(v.major()).arg(v.minor());
        }
        if(message.has_storage_info_response()) {
            const auto& info = message.storage_info_response();
            inspect_ += QString("total %1\nfree %2\n").arg(bytesText(info.total_space()), bytesText(info.free_space()));
        }
        if(message.has_app_lock_status_response())
            inspect_ += QString("app locked %1\n").arg(message.app_lock_status_response().locked() ? "yes" : "no");
        if(message.has_system_get_datetime_response() && message.system_get_datetime_response().has_datetime()) {
            const auto& dt = message.system_get_datetime_response().datetime();
            inspect_ += QString("datetime %1-%2-%3 %4:%5:%6\n")
                .arg(dt.year(), 4, 10, QChar('0')).arg(dt.month(), 2, 10, QChar('0')).arg(dt.day(), 2, 10, QChar('0'))
                .arg(dt.hour(), 2, 10, QChar('0')).arg(dt.minute(), 2, 10, QChar('0')).arg(dt.second(), 2, 10, QChar('0'));
        }
        if(message.has_system_device_info_response()) {
            const auto& info = message.system_device_info_response();
            inspect_ += QString::fromStdString(info.key()) + "=" + QString::fromStdString(info.value()) + "\n";
        }
        if(message.has_system_power_info_response()) {
            const auto& info = message.system_power_info_response();
            inspect_ += QString::fromStdString(info.key()) + "=" + QString::fromStdString(info.value()) + "\n";
        }
        if(message.has_property_get_response()) {
            const auto& info = message.property_get_response();
            inspect_ += QString::fromStdString(info.key()) + "=" + QString::fromStdString(info.value()) + "\n";
        }
        if(message.has_desktop_status())
            inspect_ += QString("desktop locked %1\n").arg(message.desktop_status().locked() ? "yes" : "no");
        if(inspectKind_ == "alert") inspect_ += "alert sent\n";
        if(inspect_.size() > 8192) inspect_ = inspect_.right(8192);
        if(message.has_next()) return;
        appendLog(inspect_);
        finishOp();
    }
}
}
