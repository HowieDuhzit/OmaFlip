#include "backend.h"
#include <QCoreApplication>
#include <QJsonDocument>
#include <QLockFile>
#include <QSocketNotifier>
#include <QStandardPaths>
#include <QDir>
#include <QTimer>
#include <cstdio>
#include <fcntl.h>
#include <unistd.h>
#include <csignal>
#include <sys/signalfd.h>

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("omaflip");
    const auto args = app.arguments();
    if(args.contains("--version")) {
        fprintf(stdout, "OmaFlip %s (IPC 1)\n", OMAFLIP_VERSION);
        return 0;
    }
    if(args.size() != 2 || (args[1] != "--stdio" && args[1] != "--scan")) {
        fprintf(stderr, "Usage: omaflip --stdio | --scan | --version\n"); return 2;
    }
    if(args[1] == "--scan") {
        omaflip::Discovery discovery;
        QJsonArray devices;
        for(const auto& usb : discovery.scan()) devices.append(QJsonObject{{"id", omaflip::stableId(usb)},
            {"port", usb.port}, {"product", usb.description}, {"identityConfirmed", omaflip::classify(usb) == omaflip::UsbKind::FlipperSerial}});
        puts(QJsonDocument(devices).toJson(QJsonDocument::Compact).constData()); return 0;
    }
    const QString runtime = QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation);
    if(runtime.isEmpty()) { fprintf(stderr,"No private user runtime directory available.\n"); return 1; }
    QLockFile lock(runtime + "/omaflip.lock");
    // Short wait permits a previous shell-owned process to exit during hot reload.
    if(!lock.tryLock(1000)) { fprintf(stderr,"Another OmaFlip backend owns the session. Stop it before restarting.\n"); return 1; }
    std::signal(SIGPIPE, SIG_IGN);
    sigset_t shutdownSignals;
    sigemptyset(&shutdownSignals);
    sigaddset(&shutdownSignals, SIGTERM);
    sigaddset(&shutdownSignals, SIGINT);
    if(sigprocmask(SIG_BLOCK, &shutdownSignals, nullptr) != 0) return 1;
    const int signalFd = signalfd(-1, &shutdownSignals, SFD_NONBLOCK | SFD_CLOEXEC);
    if(signalFd < 0) return 1;
    QSocketNotifier shutdownNotifier(signalFd, QSocketNotifier::Read);
    QObject::connect(&shutdownNotifier, &QSocketNotifier::activated, &app, [&] {
        signalfd_siginfo signalInfo{};
        if(read(signalFd, &signalInfo, sizeof(signalInfo)) == sizeof(signalInfo)) app.quit();
    });
    const int flags = fcntl(STDIN_FILENO, F_GETFL);
    if(flags < 0 || fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) < 0) return 1;
    omaflip::Backend backend;
    QObject::connect(&backend, &omaflip::Backend::event, &app, [&](const QJsonObject& object) {
        const auto data = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
        if(fwrite(data.constData(), 1, static_cast<size_t>(data.size()), stdout) != static_cast<size_t>(data.size()) || fflush(stdout) != 0) app.quit();
    });
    QByteArray input;
    QSocketNotifier stdinNotifier(STDIN_FILENO, QSocketNotifier::Read);
    QObject::connect(&stdinNotifier, &QSocketNotifier::activated, &app, [&] {
        char bytes[4096];
        const auto size = read(STDIN_FILENO, bytes, sizeof(bytes));
        if(size == 0) { app.quit(); return; } // Owner gone: release all ports and exit.
        if(size < 0) { if(errno != EAGAIN && errno != EINTR) app.exit(1); return; }
        input.append(bytes, size);
        if(input.size() > 65536) { fprintf(stderr,"IPC request exceeds 64 KiB.\n"); app.exit(2); return; }
        qsizetype end;
        while((end = input.indexOf('\n')) >= 0) {
            auto line = input.first(end); input.remove(0, end + 1);
            QJsonParseError error;
            auto doc = QJsonDocument::fromJson(line, &error);
            if(error.error != QJsonParseError::NoError || !doc.isObject()) {
                fprintf(stderr,"Invalid JSON object on IPC input.\n"); continue;
            }
            backend.command(doc.object());
        }
    });
    QString error;
    if(!backend.start(error)) { fprintf(stderr,"%s\n", qPrintable(error)); return 1; }
    const int result = app.exec();
    shutdownNotifier.setEnabled(false);
    close(signalFd);
    return result;
}
