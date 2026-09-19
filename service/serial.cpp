#include "serial.h"
#include <QSocketNotifier>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <cerrno>

namespace omaflip {
bool SerialProbe::rpcPhase() const {
    return phase_ == Phase::StartRpc || phase_ == Phase::RpcPing || phase_ == Phase::RpcVersion
        || phase_ == Phase::RpcStorageExt || phase_ == Phase::RpcStorageInt;
}
SerialProbe::SerialProbe(QObject* parent) : QObject(parent) {
    timeout_.setSingleShot(true);
    dtrTimer_.setSingleShot(true);
    connect(&timeout_, &QTimer::timeout, this, [this] {
        if(phase_ == Phase::PowerInfo) complete("Firmware did not answer 'info power'; power information is unavailable.");
        else if(rpcPhase()) complete(warning_.isEmpty() ? "RPC session did not complete; CLI device and power data are still available." : warning_);
        else fail({{"code", "cli_timeout"}, {"operation", "Read device information"}, {"path", port_},
            {"reason", "No complete CLI response within 5 seconds."},
            {"suggestion", "Unlock the Flipper, close other serial clients, and retry. The USB CLI may be disabled or unavailable in this firmware."}});
    });
    connect(&dtrTimer_, &QTimer::timeout, this, [this] {
        if(fd_ < 0) return;
        int flags = TIOCM_DTR | TIOCM_RTS;
        if(ioctl(fd_, TIOCMBIS, &flags) < 0 && errno != ENOTTY)
            fail(systemError("Enable serial session", port_, errno));
    });
}
SerialProbe::~SerialProbe() { cancel(); }
void SerialProbe::cancel() {
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
    saved_ = false; exclusive_ = false; phase_ = Phase::Done;
}
void SerialProbe::start(const QString& port) {
    cancel();
    port_ = port; input_.clear(); output_.clear(); pingPayload_ = "omaflip";
    info_ = {}; power_ = {}; rpc_ = {}; warning_.clear();
    commandId_ = 0; rpcMajor_ = 0; rpcMinor_ = 0;
    pingOk_ = false; extPresent_ = false; intPresent_ = false;
    extTotal_ = 0; extFree_ = 0; intTotal_ = 0; intFree_ = 0;
    probeMs_ = 0; pingMs_ = 0;
    probeClock_.start();
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
    connect(reader_, &QSocketNotifier::activated, this, &SerialProbe::readReady);
    connect(writer_, &QSocketNotifier::activated, this, &SerialProbe::drainWrite);
    int flags = TIOCM_DTR | TIOCM_RTS;
    if(ioctl(fd_, TIOCMBIC, &flags) < 0 && errno != ENOTTY) {
        fail(systemError("Reset serial session", port, errno)); return;
    }
    dtrTimer_.start(50);
    timeout_.start(5000);
}
void SerialProbe::send(const QByteArray& command) {
    output_ = command;
    timeout_.start(5000);
    writer_->setEnabled(true);
    drainWrite();
}
void SerialProbe::drainWrite() {
    while(!output_.isEmpty()) {
        const auto n = ::write(fd_, output_.constData(), static_cast<size_t>(output_.size()));
        if(n < 0) {
            if(errno == EINTR) continue;
            if(errno == EAGAIN) return;
            fail(systemError("Write CLI command", port_, errno)); return;
        }
        if(n == 0) return;
        output_.remove(0, n);
    }
    writer_->setEnabled(false);
}
void SerialProbe::readReady() {
    char bytes[4096];
    for(;;) {
        const auto count = ::read(fd_, bytes, sizeof(bytes));
        if(count < 0) {
            if(errno == EINTR) continue;
            if(errno == EAGAIN) break;
            fail(systemError("Read serial response", port_, errno)); return;
        }
        if(count == 0) { fail(systemError("Serial device disconnected", port_, ENODEV)); return; }
        input_.append(bytes, count);
        if(input_.size() > 65536) {
            fail({{"code", "response_too_large"}, {"operation", "Read serial response"},
                {"path", port_}, {"reason", "CLI response exceeds the 64 KiB limit."},
                {"suggestion", "Stop device log streaming or other serial clients, then retry."}}); return;
        }
    }
    if(phase_ == Phase::StartRpc) {
        if(!input_.contains("start_rpc_session\r\n") && !input_.endsWith("start_rpc_session\r\n")) return;
        input_.clear();
        phase_ = Phase::RpcPing;
        pingClock_.start();
        sendRpc(pingRequest(++commandId_, pingPayload_));
        return;
    }
    if(rpcPhase()) {
        for(;;) {
            PB::Main message;
            const auto status = takeDelimited(input_, message);
            if(status == FrameStatus::NeedMore) return;
            if(status == FrameStatus::TooLarge) {
                complete("RPC frame exceeded the 16 KiB bound."); return;
            }
            if(status == FrameStatus::Corrupt) {
                complete("RPC frame could not be decoded."); return;
            }
            handleRpc(message);
            if(phase_ == Phase::Done) return;
        }
    }
    const auto response = takeCliResponse(input_);
    if(!response) return;
    switch(phase_) {
    case Phase::Banner:
        phase_ = Phase::DeviceInfo; send("device_info\r"); break;
    case Phase::DeviceInfo:
        info_ = parseInfo(*response);
        if(!info_.contains("hardware_model") && !info_.contains("hardware_name")) {
            fail({{"code", "unsupported_cli"}, {"operation", "Read device information"},
                {"path", port_}, {"reason", "The CLI returned no recognized device information."},
                {"suggestion", "Check that this firmware supports the device_info command."}}); return;
        }
        phase_ = Phase::PowerInfo; send("info power\r"); break;
    case Phase::PowerInfo:
        power_ = parseInfo(*response);
        if(!power_.contains("charge_level"))
            warning_ = "This firmware does not expose power information through 'info power'.";
        input_.clear();
        phase_ = Phase::StartRpc;
        send("start_rpc_session\r");
        break;
    default: break;
    }
}
void SerialProbe::sendRpc(const PB::Main& message) {
    send(encodeDelimited(message));
}
void SerialProbe::handleRpc(const PB::Main& message) {
    if(message.command_id() != commandId_) {
        complete("RPC command id did not match the request."); return;
    }
    auto acceptStorage = [&](bool& present, quint64& total, quint64& free) {
        if(message.command_status() == PB::CommandStatus::OK && message.has_storage_info_response()) {
            present = true;
            total = message.storage_info_response().total_space();
            free = message.storage_info_response().free_space();
        }
    };
    switch(phase_) {
    case Phase::RpcPing:
        pingMs_ = pingClock_.isValid() ? pingClock_.elapsed() : 0;
        pingOk_ = message.command_status() == PB::CommandStatus::OK && message.has_system_ping_response()
            && QByteArray::fromStdString(message.system_ping_response().data()) == pingPayload_;
        if(!pingOk_) { complete("RPC ping failed."); return; }
        phase_ = Phase::RpcVersion;
        sendRpc(protobufVersionRequest(++commandId_));
        break;
    case Phase::RpcVersion:
        if(message.command_status() != PB::CommandStatus::OK || !message.has_system_protobuf_version_response()) {
            complete("RPC protobuf version request failed."); return;
        }
        rpcMajor_ = message.system_protobuf_version_response().major();
        rpcMinor_ = message.system_protobuf_version_response().minor();
        phase_ = Phase::RpcStorageExt;
        sendRpc(storageInfoRequest(++commandId_, "/ext"));
        break;
    case Phase::RpcStorageExt:
        acceptStorage(extPresent_, extTotal_, extFree_);
        phase_ = Phase::RpcStorageInt;
        sendRpc(storageInfoRequest(++commandId_, "/int"));
        break;
    case Phase::RpcStorageInt:
        acceptStorage(intPresent_, intTotal_, intFree_);
        rpc_ = rpcSummary(pingOk_, rpcMajor_, rpcMinor_, extPresent_, extTotal_, extFree_,
            intPresent_, intTotal_, intFree_, {});
        complete(warning_);
        break;
    default: break;
    }
}
void SerialProbe::complete(const QString& warning) {
    if(!warning.isEmpty() && warning_.isEmpty()) warning_ = warning;
    if(rpc_.isEmpty())
        rpc_ = rpcSummary(pingOk_, rpcMajor_, rpcMinor_, extPresent_, extTotal_, extFree_,
            intPresent_, intTotal_, intFree_, warning_.isEmpty() ? "RPC not started" : warning_);
    probeMs_ = probeClock_.isValid() ? probeClock_.elapsed() : 0;
    rpc_.insert("probeMs", probeMs_);
    if(pingMs_ > 0) rpc_.insert("pingMs", pingMs_);
    const auto warn = warning_;
    cancel(); emit finished(info_, power_, rpc_, warn);
}
void SerialProbe::fail(const QJsonObject& error) { cancel(); emit failed(error); }
}
