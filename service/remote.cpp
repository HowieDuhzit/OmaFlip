#include "remote.h"
#include "model.h"
#include <QSocketNotifier>
#include <fcntl.h>
#include <unistd.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <cerrno>

namespace omaflip {
namespace {
PB_Gui::InputKey parseKey(const QString& button) {
    const auto name = button.toLower();
    if(name == "up") return PB_Gui::UP;
    if(name == "down") return PB_Gui::DOWN;
    if(name == "left") return PB_Gui::LEFT;
    if(name == "right") return PB_Gui::RIGHT;
    if(name == "back") return PB_Gui::BACK;
    return PB_Gui::OK;
}
PB_Gui::InputType parseType(const QString& type) {
    const auto name = type.toLower();
    if(name == "press") return PB_Gui::PRESS;
    if(name == "release") return PB_Gui::RELEASE;
    if(name == "long") return PB_Gui::LONG;
    if(name == "repeat") return PB_Gui::REPEAT;
    return PB_Gui::SHORT;
}
}

QString remoteKeyFromVisual(const QString& visual, int orientation) {
    const auto name = visual.toLower();
    int dir = name == "up" ? 0 : name == "right" ? 1 : name == "down" ? 2 : name == "left" ? 3 : -1;
    if(dir < 0) return "ok";
    static const char* keys[] = {"up", "right", "down", "left"};
    const int turn[] = {0, 2, 1, 3};
    return QString::fromLatin1(keys[(dir + turn[orientation & 3]) % 4]);
}

RemoteSession::RemoteSession(QObject* parent) : QObject(parent) {
    timeout_.setSingleShot(true);
    dtrTimer_.setSingleShot(true);
    connect(&timeout_, &QTimer::timeout, this, [this] {
        if(phase_ == Phase::Streaming || phase_ == Phase::Done) return;
        if(phase_ == Phase::Stopping) { closePort(); emit stopped(); return; }
        fail({{"code", "cli_timeout"}, {"operation", "Start remote session"}, {"path", port_},
            {"reason", "The Flipper did not enter an RPC screen stream in time."},
            {"suggestion", "Unlock the Flipper, close other serial clients, and retry."}});
    });
    connect(&dtrTimer_, &QTimer::timeout, this, [this] {
        if(fd_ < 0) return;
        int flags = TIOCM_DTR | TIOCM_RTS;
        if(ioctl(fd_, TIOCMBIS, &flags) < 0 && errno != ENOTTY)
            fail(systemError("Enable serial session", port_, errno));
    });
}
RemoteSession::~RemoteSession() { closePort(); }

void RemoteSession::closePort() {
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

void RemoteSession::start(const QString& port) {
    closePort();
    port_ = port; input_.clear(); output_.clear(); commandId_ = 0; lastImage_ = {}; orientation_ = 0;
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
    connect(reader_, &QSocketNotifier::activated, this, &RemoteSession::readReady);
    connect(writer_, &QSocketNotifier::activated, this, &RemoteSession::drainWrite);
    int flags = TIOCM_DTR | TIOCM_RTS;
    if(ioctl(fd_, TIOCMBIC, &flags) < 0 && errno != ENOTTY) {
        fail(systemError("Reset serial session", port, errno)); return;
    }
    dtrTimer_.start(50);
    timeout_.start(5000);
}

void RemoteSession::stop() {
    if(fd_ < 0 || phase_ == Phase::Done) { closePort(); emit stopped(); return; }
    if(phase_ == Phase::Streaming) {
        phase_ = Phase::Stopping;
        sendRpc(stopStreamRequest(++commandId_));
        sendRpc(stopSessionRequest(++commandId_));
        timeout_.start(1500);
        return;
    }
    closePort();
    emit stopped();
}

void RemoteSession::sendInput(const QString& button, const QString& type) {
    if(phase_ != Phase::Streaming || fd_ < 0) return;
    auto name = button.toLower();
    if(name.startsWith("visual-")) name = remoteKeyFromVisual(name.mid(7), orientation_);
    const auto key = parseKey(name);
    const auto kind = type.toLower();
    if(kind == "press" || kind == "release" || kind == "long" || kind == "repeat") {
        sendRpc(inputRequest(++commandId_, key, parseType(kind)));
        return;
    }
    // Firmware publishes RPC events directly. Menu views listen for SHORT, and SHORT
    // is ignored unless a PRESS first assigned a non-zero sequence counter.
    sendRpc(inputRequest(++commandId_, key, PB_Gui::PRESS));
    QTimer::singleShot(25, this, [this, key] {
        if(phase_ != Phase::Streaming || fd_ < 0) return;
        sendRpc(inputRequest(++commandId_, key, PB_Gui::SHORT));
        sendRpc(inputRequest(++commandId_, key, PB_Gui::RELEASE));
    });
}

void RemoteSession::send(const QByteArray& bytes) {
    output_.append(bytes);
    if(phase_ != Phase::Streaming && phase_ != Phase::Stopping) timeout_.start(5000);
    writer_->setEnabled(true);
    drainWrite();
}
void RemoteSession::sendRpc(const PB::Main& message) { send(encodeDelimited(message)); }
void RemoteSession::drainWrite() {
    while(!output_.isEmpty()) {
        const auto n = ::write(fd_, output_.constData(), static_cast<size_t>(output_.size()));
        if(n < 0) {
            if(errno == EINTR) continue;
            if(errno == EAGAIN) return;
            fail(systemError("Write remote command", port_, errno)); return;
        }
        if(n == 0) return;
        output_.remove(0, n);
    }
    writer_->setEnabled(false);
}

void RemoteSession::readReady() {
    char bytes[4096];
    for(;;) {
        const auto count = ::read(fd_, bytes, sizeof(bytes));
        if(count < 0) {
            if(errno == EINTR) continue;
            if(errno == EAGAIN) break;
            fail(systemError("Read remote session", port_, errno)); return;
        }
        if(count == 0) { fail(systemError("Serial device disconnected", port_, ENODEV)); return; }
        input_.append(bytes, count);
        if(input_.size() > 262144) {
            fail({{"code", "response_too_large"}, {"operation", "Read remote session"},
                {"path", port_}, {"reason", "Remote RPC input exceeded the 256 KiB bound."},
                {"suggestion", "Stop the remote view and retry."}}); return;
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
            fail({{"code", "rpc_frame"}, {"operation", "Read remote session"}, {"path", port_},
                {"reason", "RPC frame was corrupt or too large."},
                {"suggestion", "Stop the remote view and retry."}}); return;
        }
        handleRpc(message);
        if(phase_ == Phase::Done) return;
    }
}

void RemoteSession::handleRpc(const PB::Main& message) {
    if(message.has_gui_screen_frame() && (phase_ == Phase::Streaming || phase_ == Phase::StartStream)) {
        const auto& screen = message.gui_screen_frame();
        orientation_ = static_cast<int>(screen.orientation());
        const auto raw = QByteArray::fromStdString(screen.data());
        lastImage_ = decodeScreen(raw, orientation_);
        emit this->frame(raw.toBase64(), orientation_);
        return;
    }
    if(phase_ == Phase::Ping) {
        if(message.command_id() != commandId_ || !message.has_system_ping_response()) {
            fail({{"code", "rpc_ping"}, {"operation", "Start remote session"}, {"path", port_},
                {"reason", "RPC ping failed while starting the screen stream."},
                {"suggestion", "Unlock the Flipper and retry Remote."}}); return;
        }
        phase_ = Phase::StartStream;
        sendRpc(startStreamRequest(++commandId_));
        return;
    }
    if(phase_ == Phase::StartStream) {
        if(message.command_id() == commandId_ && message.command_status() != PB::CommandStatus::OK) {
            fail({{"code", "rpc_stream"}, {"operation", "Start screen stream"}, {"path", port_},
                {"reason", "The firmware refused gui_start_screen_stream (" + commandStatusName(message.command_status()) + ")."},
                {"suggestion", "Close other RPC clients and retry."}}); return;
        }
        if(message.command_id() == commandId_ || message.has_gui_screen_frame()) {
            phase_ = Phase::Streaming;
            timeout_.stop();
            emit ready();
        }
        return;
    }
    if(phase_ == Phase::Stopping) {
        closePort();
        emit stopped();
    }
}

void RemoteSession::fail(const QJsonObject& error) {
    closePort();
    emit failed(error);
}
}
