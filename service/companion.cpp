#include "companion.h"
#include "model.h"
#include <QJsonDocument>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QSysInfo>
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace omaflip {
namespace {
struct CommandResult { bool ok = false; QByteArray out; };

QString executable(const QString& name) {
    const auto found = QStandardPaths::findExecutable(name);
    if(!found.isEmpty()) return found;
    if(name == "omarchy") return "/usr/share/omarchy/bin/omarchy";
    return name;
}

void appendText(QByteArray& payload, const QString& text, int maxBytes) {
    const auto bytes = companionText(text, maxBytes);
    payload.append(static_cast<char>(bytes.size()));
    payload.append(bytes);
}

bool jsonEnabled(const CommandResult& result) {
    if(!result.ok) return false;
    const auto document = QJsonDocument::fromJson(result.out);
    return document.isObject() && document.object().value("enabled").toBool();
}

// Kill without waiting on the backend thread. Detached processes own reap/delete
// callbacks, including cancellation while process startup is still pending.
void retireProcess(QProcess* process, QObject* owner) {
    QObject::disconnect(process, nullptr, owner, nullptr);
    process->setParent(nullptr);
    QObject::connect(process, &QProcess::started, process, &QProcess::kill);
    QObject::connect(process, &QProcess::finished, process, &QObject::deleteLater);
    QObject::connect(process, &QProcess::errorOccurred, process, [process](QProcess::ProcessError error) {
        if(error == QProcess::FailedToStart) process->deleteLater();
    });
    if(process->state() == QProcess::NotRunning) process->deleteLater();
    else process->kill();
}

QStringList actionCommand(quint8 action) {
    switch(action) {
    case 1: return {executable("omarchy"), "system", "lock"};
    case 2: return {executable("omarchy"), "audio", "output", "volume", "raise"};
    case 3: return {executable("omarchy"), "audio", "output", "volume", "lower"};
    case 4: return {executable("omarchy"), "audio", "output", "volume", "mute-toggle"};
    case 5: return {executable("playerctl"), "play-pause"};
    case 6: return {executable("playerctl"), "previous"};
    case 7: return {executable("playerctl"), "next"};
    case 8: return {executable("hyprctl"), "dispatch", "workspace", "-1"};
    case 9: return {executable("hyprctl"), "dispatch", "workspace", "+1"};
    case 11: return {executable("omarchy"), "theme", "bg", "next"};
    case 12: return {executable("omarchy"), "toggle", "nightlight"};
    case 13: return {executable("omarchy"), "toggle", "idle"};
    case 14: return {executable("omarchy"), "toggle", "notification", "silencing"};
    default: return {};
    }
}
}

CompanionSession::CompanionSession(QObject* parent) : QObject(parent) {
    timeout_.setSingleShot(true);
    dtrTimer_.setSingleShot(true);
    actionTimer_.setSingleShot(true);
    telemetryTimer_.setSingleShot(true);
    connect(&telemetryTimer_, &QTimer::timeout, this, [this] {
        const auto generation = telemetryGeneration_;
        for(int i = 0; i < 6; ++i) finishTelemetry(i, generation, false);
    });
    refreshTimer_.setInterval(3000);
    connect(&timeout_, &QTimer::timeout, this, [this] {
        if(phase_ == Phase::Stopping) { closePort(); emit stopped(); return; }
        fail({{"code", "companion_timeout"}, {"operation", "Start Companion"}, {"path", port_},
            {"reason", "The Flipper did not finish the Companion handshake in time."},
            {"suggestion", "Install Fliparchy in /ext/apps/Tools, unlock the Flipper, and retry."}});
    });
    connect(&dtrTimer_, &QTimer::timeout, this, [this] {
        if(fd_ < 0) return;
        int flags = TIOCM_DTR | TIOCM_RTS;
        if(ioctl(fd_, TIOCMBIS, &flags) < 0 && errno != ENOTTY)
            fail(systemError("Enable serial session", port_, errno));
    });
    connect(&refreshTimer_, &QTimer::timeout, this, &CompanionSession::sendState);
    connect(&actionTimer_, &QTimer::timeout, this, [this] {
        if(!actionProcess_) return;
        finishAction(false, "Action timed out");
    });
}

CompanionSession::~CompanionSession() { closePort(); }

QJsonObject CompanionSession::snapshot() const {
    return {{"open", phase_ != Phase::Done}, {"ready", phase_ == Phase::Ready},
        {"status", status_}, {"lastAction", lastAction_},
        {"pingsSent", pingsSent_}, {"pingsReceived", pingsReceived_},
        {"security", "local-selected-device"}, {"protocol", "1.0"}};
}

void CompanionSession::closePort() {
    cancelTelemetry();
    timeout_.stop(); dtrTimer_.stop(); refreshTimer_.stop(); actionTimer_.stop();
    cancelAction();
    delete reader_; reader_ = nullptr;
    delete writer_; writer_ = nullptr;
    if(fd_ >= 0) {
        int flags = TIOCM_DTR | TIOCM_RTS;
        if(exclusive_) ioctl(fd_, TIOCMBIC, &flags);
        if(saved_) tcsetattr(fd_, TCSANOW, &previous_);
        if(exclusive_) ioctl(fd_, TIOCNXCL);
        close(fd_); fd_ = -1;
    }
    input_.clear(); output_.clear();
    saved_ = false; exclusive_ = false; phase_ = Phase::Done;
}

void CompanionSession::start(const QString& port) {
    closePort();
    port_ = port; input_.clear(); output_.clear(); commandId_ = 0;
    nextPingId_ = 0; pendingPingId_ = 0; pingsSent_ = 0; pingsReceived_ = 0;
    status_ = "Connecting"; lastAction_.clear();
    fd_ = open(port.toLocal8Bit().constData(), O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
    if(fd_ < 0) { fail(systemError("Open Flipper serial port", port, errno)); return; }
    if(flock(fd_, LOCK_EX | LOCK_NB) < 0 || ioctl(fd_, TIOCEXCL) < 0) {
        fail(systemError("Acquire exclusive serial access", port, errno)); return;
    }
    exclusive_ = true;
    if(tcgetattr(fd_, &previous_) < 0) { fail(systemError("Read serial settings", port, errno)); return; }
    saved_ = true;
    termios config = previous_;
    cfmakeraw(&config); cfsetispeed(&config, B115200); cfsetospeed(&config, B115200);
    config.c_cflag |= CLOCAL | CREAD; config.c_cflag &= ~CRTSCTS;
    config.c_cc[VMIN] = 1; config.c_cc[VTIME] = 0;
    if(tcsetattr(fd_, TCSANOW, &config) < 0 || tcflush(fd_, TCIOFLUSH) < 0) {
        fail(systemError("Configure serial session", port, errno)); return;
    }
    phase_ = Phase::Banner;
    reader_ = new QSocketNotifier(fd_, QSocketNotifier::Read, this);
    writer_ = new QSocketNotifier(fd_, QSocketNotifier::Write, this); writer_->setEnabled(false);
    connect(reader_, &QSocketNotifier::activated, this, &CompanionSession::readReady);
    connect(writer_, &QSocketNotifier::activated, this, &CompanionSession::drainWrite);
    int flags = TIOCM_DTR | TIOCM_RTS;
    if(ioctl(fd_, TIOCMBIC, &flags) < 0 && errno != ENOTTY) {
        fail(systemError("Reset serial session", port, errno)); return;
    }
    dtrTimer_.start(50); timeout_.start(5000); emit changed();
}

void CompanionSession::stop() {
    refreshTimer_.stop(); cancelTelemetry(); cancelAction();
    if(fd_ < 0 || phase_ == Phase::Done) { closePort(); emit stopped(); return; }
    if(phase_ == Phase::Ready || phase_ == Phase::WaitHello) {
        phase_ = Phase::Stopping;
        sendRpc(appExitRequest(++commandId_));
        if(phase_ != Phase::Stopping) return;
        sendRpc(stopSessionRequest(++commandId_));
        if(phase_ != Phase::Stopping) return;
        timeout_.start(1500); return;
    }
    closePort(); emit stopped();
}

void CompanionSession::ping() {
    if(phase_ != Phase::Ready || pendingPingId_ != 0) return;
    pendingPingId_ = ++nextPingId_;
    if(pendingPingId_ == 0) pendingPingId_ = ++nextPingId_;
    ++pingsSent_;
    sendFrame(CompanionMessage::Ping, pendingPingId_);
    emit changed();
}

void CompanionSession::send(const QByteArray& bytes) {
    if(fd_ < 0 || !writer_ || phase_ == Phase::Done) return;
    output_.append(bytes); writer_->setEnabled(true); drainWrite();
}
void CompanionSession::sendRpc(const PB::Main& message) { send(encodeDelimited(message)); }
void CompanionSession::sendFrame(CompanionMessage type, quint16 requestId, const QByteArray& payload, quint8 flags) {
    sendRpc(appDataExchangeRequest(++commandId_, encodeCompanionFrame(type, flags, requestId, payload)));
}

void CompanionSession::drainWrite() {
    if(fd_ < 0 || !writer_) return;
    while(!output_.isEmpty()) {
        const auto n = ::write(fd_, output_.constData(), static_cast<size_t>(output_.size()));
        if(n < 0) {
            if(errno == EINTR) continue;
            if(errno == EAGAIN) return;
            fail(systemError("Write Companion command", port_, errno)); return;
        }
        if(n == 0) return;
        output_.remove(0, n);
    }
    if(writer_) writer_->setEnabled(false);
}

void CompanionSession::readReady() {
    char bytes[4096];
    for(;;) {
        const auto count = ::read(fd_, bytes, sizeof(bytes));
        if(count < 0) {
            if(errno == EINTR) continue;
            if(errno == EAGAIN) break;
            fail(systemError("Read Companion session", port_, errno)); return;
        }
        if(count == 0) { fail(systemError("Serial device disconnected", port_, ENODEV)); return; }
        input_.append(bytes, count);
        if(input_.size() > 262144) {
            fail({{"code", "response_too_large"}, {"reason", "Companion RPC input exceeded 256 KiB."},
                {"suggestion", "Close Companion and retry."}}); return;
        }
    }
    if(phase_ == Phase::Banner) {
        if(!takeCliResponse(input_)) return;
        phase_ = Phase::StartRpc; send("start_rpc_session\r"); return;
    }
    if(phase_ == Phase::StartRpc) {
        if(!input_.contains("start_rpc_session\r\n")) return;
        input_.clear(); phase_ = Phase::Ping;
        sendRpc(pingRequest(++commandId_, "omaflip-companion")); return;
    }
    for(;;) {
        PB::Main message;
        const auto frameStatus = takeDelimited(input_, message);
        if(frameStatus == FrameStatus::NeedMore) return;
        if(frameStatus != FrameStatus::Ok) {
            fail({{"code", "rpc_frame"}, {"reason", "Companion RPC frame was corrupt or too large."},
                {"suggestion", "Close Companion and retry."}}); return;
        }
        handleRpc(message);
        if(phase_ == Phase::Done) return;
    }
}

void CompanionSession::handleRpc(const PB::Main& message) {
    if(phase_ == Phase::Done) return;
    if(phase_ == Phase::Stopping) { closePort(); emit stopped(); return; }
    if(message.has_app_state_response() && message.app_state_response().state() == PB_App::APP_CLOSED) {
        status_ = "Fliparchy closed";
        closePort(); emit stopped(); return;
    }
    // Preserve the specific launch error for matching start responses. Other
    // terminal errors must close RPC even when their command ID is invalid.
    if(message.command_status() != PB::CommandStatus::OK &&
       !(phase_ == Phase::Launch && message.command_id() == commandId_)) {
        fail({{"code", "companion_rpc"},
            {"reason", "Companion RPC failed (" + commandStatusName(message.command_status()) + ")."},
            {"suggestion", "Close any running Flipper app and restart Companion."}}); return;
    }
    if(message.has_app_data_exchange_request()) {
        const auto data = QByteArray::fromStdString(message.app_data_exchange_request().data());
        sendRpc(commandResponse(message.command_id()));
        if(phase_ == Phase::Done) return;
        CompanionFrame frame; QString error;
        if(!decodeCompanionFrame(data, frame, error)) {
            sendFrame(CompanionMessage::Error, 0, companionText(error, 80), 2); return;
        }
        handleFrame(frame); return;
    }
    if(phase_ == Phase::Ping) {
        if(message.command_id() != commandId_ || !message.has_system_ping_response()) {
            fail({{"code", "rpc_ping"}, {"reason", "RPC ping failed while starting Companion."},
                {"suggestion", "Unlock the Flipper and retry."}}); return;
        }
        phase_ = Phase::Launch; status_ = "Launching Fliparchy";
        sendRpc(appStartRequest(++commandId_, "/ext/apps/Tools/fliparchy.fap", "RPC")); emit changed(); return;
    }
    if(phase_ == Phase::Launch && message.command_id() == commandId_) {
        if(message.command_status() != PB::CommandStatus::OK) {
            fail({{"code", "companion_launch"},
                {"reason", "Fliparchy could not start (" + commandStatusName(message.command_status()) + ")."},
                {"suggestion", "Install the FAP in Apps/Tools and close any running Flipper app."}}); return;
        }
        phase_ = Phase::WaitHello; status_ = "Waiting for Fliparchy"; timeout_.start(5000); emit changed(); return;
    }
}

void CompanionSession::handleFrame(const CompanionFrame& frame) {
    if(frame.type == CompanionMessage::Hello &&
       (phase_ == Phase::Launch || phase_ == Phase::WaitHello)) {
        timeout_.stop(); phase_ = Phase::Ready; status_ = "Connected";
        sendFrame(CompanionMessage::HelloAck, 0, companionText(QSysInfo::machineHostName(), 16));
        if(phase_ != Phase::Ready) return;
        sendState(); refreshTimer_.start(); emit ready();
        if(phase_ == Phase::Ready) emit changed();
        return;
    }
    if(phase_ != Phase::Ready) return;
    if(frame.type == CompanionMessage::ActionRequest && frame.payload.size() == 1) {
        runAction(static_cast<quint8>(frame.payload[0]), frame.requestId);
    } else if(frame.type == CompanionMessage::Ping) {
        sendFrame(CompanionMessage::Pong, frame.requestId, {}, 1);
    } else if(frame.type == CompanionMessage::Pong && frame.requestId == pendingPingId_) {
        pendingPingId_ = 0;
        ++pingsReceived_;
        emit changed();
    }
}

void CompanionSession::sendState() {
    if(phase_ != Phase::Ready || telemetryInFlight_) return;
    telemetryInFlight_ = true;
    telemetryRemaining_ = 6;
    telemetry_ = {};
    const auto generation = ++telemetryGeneration_;
    const std::array<QStringList, 6> commands{{
        {executable("hyprctl"), "activeworkspace", "-j"},
        {executable("wpctl"), "get-volume", "@DEFAULT_AUDIO_SINK@"},
        {executable("playerctl"), "metadata", "--format", "{{status}}|{{artist}} - {{title}}"},
        {executable("omarchy"), "toggle", "nightlight", "--status"},
        {executable("omarchy"), "toggle", "idle", "status"},
        {executable("omarchy"), "theme", "current"}
    }};
    telemetryTimer_.start(700); // One deadline, including process startup.
    for(int i = 0; i < 6; ++i) {
        auto* process = new QProcess(this);
        telemetryProcesses_[i] = process;
        process->setProcessChannelMode(QProcess::MergedChannels);
        connect(process, &QProcess::readyRead, this, [this, i, generation] { readTelemetry(i, generation); });
        connect(process, &QProcess::finished, this, [this, i, generation](int code, QProcess::ExitStatus status) {
            finishTelemetry(i, generation, code == 0 && status == QProcess::NormalExit);
        });
        connect(process, &QProcess::errorOccurred, this, [this, i, generation](QProcess::ProcessError error) {
            if(error == QProcess::FailedToStart) finishTelemetry(i, generation, false);
        });
        auto command = commands[i];
        const auto program = command.takeFirst();
        process->start(program, command);
    }
}

void CompanionSession::readTelemetry(int index, quint64 generation) {
    if(generation != telemetryGeneration_ || !telemetryInFlight_ || !telemetryProcesses_[index]) return;
    auto* process = telemetryProcesses_[index];
    auto& result = telemetry_[index];
    constexpr int maxOutput = 16384;
    // Keep at most the cap plus a sentinel byte; never parse oversize output.
    result.out += process->read(maxOutput + 1 - result.out.size());
    if(result.out.size() > maxOutput) finishTelemetry(index, generation, false);
}

void CompanionSession::finishTelemetry(int index, quint64 generation, bool ok) {
    if(generation != telemetryGeneration_ || !telemetryInFlight_ || !telemetryProcesses_[index]) return;
    auto* process = telemetryProcesses_[index];
    auto& result = telemetry_[index];
    result.out += process->read(qMax<qint64>(0, 16385 - result.out.size()));
    result.ok = ok && result.out.size() <= 16384;
    if(!result.ok) result.out.clear();
    else result.out = result.out.trimmed();
    telemetryProcesses_[index] = nullptr;
    retireProcess(process, this);
    if(--telemetryRemaining_ != 0) return;
    telemetryTimer_.stop(); telemetryInFlight_ = false;
    if(phase_ == Phase::Ready && generation == telemetryGeneration_) publishTelemetry();
}

void CompanionSession::cancelTelemetry() {
    ++telemetryGeneration_;
    telemetryTimer_.stop(); telemetryInFlight_ = false; telemetryRemaining_ = 0;
    for(auto*& process : telemetryProcesses_) {
        if(process) { retireProcess(process, this); process = nullptr; }
    }
    telemetry_ = {};
}

void CompanionSession::publishTelemetry() {
    if(phase_ != Phase::Ready) return;
    int workspace = 1;
    const auto& workspaceResult = telemetry_[0];
    if(workspaceResult.ok) workspace = QJsonDocument::fromJson(workspaceResult.out).object().value("id").toInt(1);
    int volume = 0; quint8 flags = 0;
    const auto& volumeResult = telemetry_[1];
    if(volumeResult.ok) {
        const auto text = QString::fromUtf8(volumeResult.out);
        const auto match = QRegularExpression("([0-9]+(?:\\.[0-9]+)?)").match(text);
        if(match.hasMatch()) volume = qBound(0, qRound(match.captured(1).toDouble() * 100), 100);
        if(text.contains("MUTED", Qt::CaseInsensitive)) flags |= 1;
    }
    QString media;
    const auto& mediaResult = telemetry_[2];
    if(mediaResult.ok) {
        media = QString::fromUtf8(mediaResult.out);
        if(media.startsWith("Playing|")) flags |= 2;
        media = media.section('|', 1);
    }
    if(jsonEnabled({telemetry_[3].ok, telemetry_[3].out})) flags |= 4;
    if(jsonEnabled({telemetry_[4].ok, telemetry_[4].out})) flags |= 8;
    const auto theme = QString::fromUtf8(telemetry_[5].out);
    QByteArray payload;
    payload.append(static_cast<char>(qBound(0, workspace, 255)));
    payload.append(static_cast<char>(volume)); payload.append(static_cast<char>(flags));
    appendText(payload, QSysInfo::machineHostName(), 16);
    appendText(payload, theme.isEmpty() ? QString("Omarchy") : theme, 20);
    appendText(payload, media.isEmpty() ? QString("No active player") : media, 32);
    sendFrame(CompanionMessage::StateSnapshot, 0, payload);
    if(phase_ == Phase::Ready) emit changed();
}

void CompanionSession::runAction(quint8 action, quint16 requestId) {
    if(phase_ != Phase::Ready) return;
    if(actionProcess_) {
        QByteArray payload; payload.append(static_cast<char>(action)); payload.append(char(1));
        payload.append(companionText("Another action is running", 64));
        sendFrame(CompanionMessage::ActionResult, requestId, payload, 3); return;
    }
    actionId_ = action; actionRequestId_ = requestId; actionStep_ = action == 10 ? 1 : 0;
    currentTheme_.clear();
    ++actionGeneration_;
    auto command = action == 10 ? QStringList{executable("omarchy"), "theme", "current"} : actionCommand(action);
    if(command.isEmpty()) {
        finishAction(false, "Unsupported action"); return;
    }
    actionTimer_.start(5000); // Includes theme discovery and the final command.
    startActionCommand(command);
}

void CompanionSession::startActionCommand(QStringList command) {
    const auto program = command.takeFirst();
    if(!QFileInfo::exists(program) && QStandardPaths::findExecutable(program).isEmpty()) {
        finishAction(false, "Required command is unavailable"); return;
    }
    status_ = "Running action"; actionOutput_.clear();
    auto* process = new QProcess(this); actionProcess_ = process;
    const auto generation = actionGeneration_;
    process->setProcessChannelMode(QProcess::MergedChannels);
    connect(process, &QProcess::readyRead, this, &CompanionSession::readActionOutput);
    connect(process, &QProcess::errorOccurred, this, [this, process, generation](QProcess::ProcessError error) {
        if(actionProcess_ == process && generation == actionGeneration_ && error == QProcess::FailedToStart)
            finishAction(false, "Required command could not start");
    });
    connect(process, &QProcess::finished, this, [this, process, generation](int exitCode, QProcess::ExitStatus exitStatus) {
        if(actionProcess_ != process || generation != actionGeneration_ || phase_ != Phase::Ready) return;
        readActionOutput();
        if(actionProcess_ != process) return;
        const auto output = QString::fromUtf8(actionOutput_).trimmed();
        actionProcess_ = nullptr; retireProcess(process, this);
        if(exitStatus != QProcess::NormalExit || exitCode != 0) {
            finishAction(false, output.isEmpty() ? QString("Action failed") : output); return;
        }
        if(actionStep_ == 1) {
            currentTheme_ = output; actionStep_ = 2;
            startActionCommand({executable("omarchy"), "theme", "list"}); return;
        }
        if(actionStep_ == 2) {
            const auto themes = output.split('\n', Qt::SkipEmptyParts);
            if(themes.isEmpty()) { finishAction(false, "No themes available"); return; }
            const auto index = themes.indexOf(currentTheme_);
            actionStep_ = 0;
            startActionCommand({executable("omarchy"), "theme", "set", themes[(index + 1) % themes.size()].trimmed()}); return;
        }
        finishAction(true, "Done");
    });
    process->start(program, command); emit changed();
}

void CompanionSession::readActionOutput() {
    if(!actionProcess_) return;
    actionOutput_ += actionProcess_->read(16385 - actionOutput_.size());
    if(actionOutput_.size() > 16384) finishAction(false, "Action output exceeded 16 KiB");
}

void CompanionSession::cancelAction() {
    ++actionGeneration_; actionTimer_.stop();
    if(actionProcess_) { auto* process = actionProcess_; actionProcess_ = nullptr; retireProcess(process, this); }
    actionOutput_.clear(); currentTheme_.clear(); actionStep_ = 0;
}

void CompanionSession::finishAction(bool success, const QString& detail) {
    cancelAction();
    if(phase_ != Phase::Ready) return;
    QByteArray payload; payload.append(static_cast<char>(actionId_)); payload.append(success ? char(0) : char(1));
    payload.append(companionText(detail, 72));
    sendFrame(CompanionMessage::ActionResult, actionRequestId_, payload, success ? 1 : 3);
    if(phase_ != Phase::Ready) return;
    lastAction_ = detail; status_ = "Connected"; emit changed();
    const auto generation = actionGeneration_;
    QTimer::singleShot(150, this, [this, generation] {
        if(generation == actionGeneration_) sendState();
    });
}

void CompanionSession::fail(const QJsonObject& error) { closePort(); emit failed(error); }
}
