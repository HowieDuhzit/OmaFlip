#include "cli.h"
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <cerrno>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <unistd.h>

namespace omaflip {
namespace {
QString firstToken(const QString& line) {
    const auto trimmed = line.trimmed();
    const auto space = trimmed.indexOf(QRegularExpression("\\s"));
    return space < 0 ? trimmed : trimmed.left(space);
}
}

QJsonArray parseHelpCommands(const QByteArray& bytes) {
    QJsonArray out;
    QStringList seen;
    static const QRegularExpression nameRe("^[A-Za-z!?][A-Za-z0-9_!?-]*");
    const auto text = cleanTerminal(bytes);
    for(const auto& raw : text.split('\n')) {
        auto line = raw.trimmed();
        if(line.isEmpty() || line == ">:" || line.startsWith(">:")) continue;
        if(line == "help" || line == "?") continue;
        if(line.startsWith("Available", Qt::CaseInsensitive) || line.startsWith("Command", Qt::CaseInsensitive))
            continue;
        QStringList names;
        QString rest = line;
        for(;;) {
            const auto match = nameRe.match(rest);
            if(!match.hasMatch() || match.capturedStart() != 0) break;
            names.append(match.captured());
            rest = rest.mid(match.capturedLength()).trimmed();
            if(rest.startsWith(',')) rest = rest.mid(1).trimmed();
            else break;
        }
        if(names.isEmpty()) continue;
        if(rest.startsWith(':') || rest.startsWith('-') || rest.startsWith('\t'))
            rest = rest.mid(1).trimmed();
        for(const auto& name : names) {
            if(seen.contains(name)) continue;
            seen.append(name);
            out.append(QJsonObject{{"name", name}, {"summary", rest}});
        }
    }
    return out;
}

QString sanitizeCliLine(const QString& line) {
    QString text = line;
    text.replace('\r', ' ');
    text.replace('\n', ' ');
    text.remove('\0');
    text.remove(QChar(0x03));
    text = text.trimmed();
    if(text.size() > kMaxCliCommand) text = text.left(kMaxCliCommand);
    return text;
}

bool isStreamingCommand(const QString& line) {
    const auto parts = line.trimmed().split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if(parts.isEmpty()) return false;
    const auto cmd = parts[0].toLower();
    if(cmd == "log") {
        if(parts.size() >= 2) {
            const auto arg = parts[1].toLower();
            if(arg == "?" || arg == "help") return false;
        }
        return true;
    }
    if(cmd == "echo") return true;
    if(cmd == "top") return parts.size() < 2 || parts[1] != "0";
    return false;
}

bool isBlockedCliCommand(const QString& line) {
    return firstToken(line).compare("start_rpc_session", Qt::CaseInsensitive) == 0;
}

CliSession::CliSession(QObject* parent) : QObject(parent) {
    timeout_.setSingleShot(true);
    dtrTimer_.setSingleShot(true);
    liveTimer_.setSingleShot(true);
    connect(&timeout_, &QTimer::timeout, this, [this] {
        if(phase_ == Phase::Ready && streaming_) return;
        if(phase_ == Phase::Stopping) { closePort(); emit stopped(); return; }
        if(phase_ == Phase::Ready && waiting_) {
            streaming_ = true;
            waiting_ = false;
            error_ = "The command is still running. Choose Stop to send Ctrl+C.";
            emit changed();
            return;
        }
        fail({{"code", "cli_timeout"}, {"operation", "Start CLI session"}, {"path", port_},
            {"reason", "The Flipper did not enter a CLI session in time."},
            {"suggestion", "Unlock the Flipper, close other serial clients, and retry."}});
    });
    connect(&dtrTimer_, &QTimer::timeout, this, [this] {
        if(fd_ < 0) return;
        int flags = TIOCM_DTR | TIOCM_RTS;
        if(ioctl(fd_, TIOCMBIS, &flags) < 0 && errno != ENOTTY)
            fail(systemError("Enable serial session", port_, errno));
    });
    connect(&liveTimer_, &QTimer::timeout, this, [this] { emit changed(); });
}
CliSession::~CliSession() { closePort(); }

bool CliSession::busy() const {
    return waiting_ || streaming_ || phase_ == Phase::Banner || phase_ == Phase::Help;
}

QJsonObject CliSession::snapshot() const {
    return {{"open", phase_ != Phase::Done}, {"ready", phase_ == Phase::Ready},
        {"busy", busy()}, {"streaming", streaming_}, {"error", error_},
        {"output", output_ + cleanTerminal(input_)}, {"commands", commands_},
        {"history", QJsonArray::fromStringList(history_)}, {"saved", savedPath_}};
}

void CliSession::closePort() {
    timeout_.stop(); dtrTimer_.stop(); liveTimer_.stop();
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
    waiting_ = false; streaming_ = false;
}

void CliSession::start(const QString& port) {
    closePort();
    port_ = port; input_.clear(); outputBytes_.clear(); output_.clear(); error_.clear();
    commands_ = {}; history_.clear(); savedPath_.clear(); waiting_ = false; streaming_ = false;
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
    connect(reader_, &QSocketNotifier::activated, this, &CliSession::readReady);
    connect(writer_, &QSocketNotifier::activated, this, &CliSession::drainWrite);
    int flags = TIOCM_DTR | TIOCM_RTS;
    if(ioctl(fd_, TIOCMBIC, &flags) < 0 && errno != ENOTTY) {
        fail(systemError("Reset serial session", port, errno)); return;
    }
    dtrTimer_.start(50);
    timeout_.start(5000);
    emit changed();
}

void CliSession::stop() {
    if(fd_ < 0 || phase_ == Phase::Done) { closePort(); emit stopped(); return; }
    if(streaming_ || waiting_) {
        phase_ = Phase::Stopping;
        send(QByteArray(1, '\x03'));
        timeout_.start(1500);
        return;
    }
    closePort();
    emit stopped();
}

void CliSession::send(const QByteArray& bytes) {
    outputBytes_.append(bytes);
    writer_->setEnabled(true);
    drainWrite();
}
void CliSession::drainWrite() {
    while(!outputBytes_.isEmpty()) {
        const auto n = ::write(fd_, outputBytes_.constData(), static_cast<size_t>(outputBytes_.size()));
        if(n < 0) {
            if(errno == EINTR) continue;
            if(errno == EAGAIN) return;
            fail(systemError("Write CLI command", port_, errno)); return;
        }
        if(n == 0) return;
        outputBytes_.remove(0, n);
    }
    if(writer_) writer_->setEnabled(false);
}

void CliSession::appendOutput(const QString& text) {
    output_ += text;
    if(output_.size() > kMaxCliOutput) output_ = output_.right(kMaxCliOutput);
}

void CliSession::publishLive() {
    if(!liveTimer_.isActive()) liveTimer_.start(80);
}

void CliSession::fail(const QJsonObject& error) {
    closePort();
    emit failed(error);
}

void CliSession::completeCommand() {
    waiting_ = false;
    streaming_ = false;
    timeout_.stop();
    error_.clear();
    emit changed();
}

void CliSession::ingest() {
    if(auto done = takeCliResponse(input_)) {
        appendOutput(cleanTerminal(*done));
        if(phase_ == Phase::Banner) {
            phase_ = Phase::Help;
            send("help\r");
            timeout_.start(5000);
            emit changed();
            return;
        }
        if(phase_ == Phase::Help) {
            commands_ = parseHelpCommands(*done);
            phase_ = Phase::Ready;
            timeout_.stop();
            emit ready();
            emit changed();
            return;
        }
        if(phase_ == Phase::Stopping) { closePort(); emit stopped(); return; }
        completeCommand();
        return;
    }
    if(phase_ == Phase::Ready) publishLive();
}

void CliSession::readReady() {
    char bytes[4096];
    for(;;) {
        const auto count = ::read(fd_, bytes, sizeof(bytes));
        if(count < 0) {
            if(errno == EINTR) continue;
            if(errno == EAGAIN) break;
            fail(systemError("Read CLI session", port_, errno)); return;
        }
        if(count == 0) { fail(systemError("Serial device disconnected", port_, ENODEV)); return; }
        input_.append(bytes, count);
        if(input_.size() > 256 * 1024) {
            fail({{"code", "response_too_large"}, {"operation", "Read CLI session"},
                {"path", port_}, {"reason", "CLI input exceeded the 256 KiB bound."},
                {"suggestion", "Stop the CLI view and retry."}}); return;
        }
    }
    ingest();
}

void CliSession::sendLine(const QString& line) {
    const auto text = sanitizeCliLine(line);
    if(phase_ != Phase::Ready) {
        error_ = "The CLI session is not ready yet.";
        emit changed(); return;
    }
    if(waiting_ || streaming_) {
        error_ = "A command is still running. Choose Stop first.";
        emit changed(); return;
    }
    if(text.isEmpty()) return;
    if(isBlockedCliCommand(text)) {
        error_ = "start_rpc_session would leave the CLI. Use Remote or Files instead.";
        emit changed(); return;
    }
    if(history_.isEmpty() || history_.last() != text) {
        history_.append(text);
        while(history_.size() > kMaxCliHistory) history_.removeFirst();
    }
    error_.clear();
    waiting_ = true;
    streaming_ = isStreamingCommand(text);
    send(text.toUtf8() + '\r');
    if(streaming_) timeout_.stop();
    else timeout_.start(8000);
    emit changed();
}

void CliSession::interrupt() {
    if(phase_ != Phase::Ready || fd_ < 0) return;
    send(QByteArray(1, '\x03'));
    streaming_ = false;
    waiting_ = true;
    timeout_.start(3000);
    emit changed();
}

QString CliSession::save(QString& error) {
    const auto downloads = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    if(downloads.isEmpty()) { error = "No Downloads directory is available."; return {}; }
    QDir dir(downloads + "/OmaFlip");
    if(!dir.exists() && !dir.mkpath(".")) { error = "Could not create the OmaFlip download folder."; return {}; }
    const auto path = dir.filePath("cli-" + QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss") + ".txt");
    QFile file(path);
    if(!file.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        error = "Could not write the CLI log."; return {};
    }
    const auto text = output_ + cleanTerminal(input_);
    if(file.write(text.toUtf8()) < 0) { error = "Could not write the CLI log."; return {}; }
    savedPath_ = path;
    emit changed();
    return path;
}
}
