#include "model.h"
#include "serial.h"
#include <QtTest>
#include <QSocketNotifier>
#include <pty.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>

using namespace omaflip;
class CoreTests : public QObject {
    Q_OBJECT
private slots:
    void recognition() {
        UsbIdentity usb{"0483", "5740", "Flipper Devices Inc.", "Flipper Control Virtual ComPort", "TEST_ONLY", "/sys/test", "/dev/test"};
        QCOMPARE(classify(usb), UsbKind::FlipperSerial);
        usb.manufacturer = "Unrelated STM32";
        QCOMPARE(classify(usb), UsbKind::Other);
        usb.product = "df11";
        QCOMPARE(classify(usb), UsbKind::DfuCandidate);
        usb.vendor = "9999";
        QCOMPARE(classify(usb), UsbKind::Other);
    }
    void identityAcrossReconnect() {
        UsbIdentity first{"0483", "5740", "Flipper Devices Inc.", "Flipper Control Virtual ComPort", "TEST_ONLY", "/sys/one", "/dev/ttyACM0"};
        auto second = first;
        second.port = "/dev/ttyACM3"; second.syspath = "/sys/two";
        QCOMPARE(stableId(first), stableId(second));
        second.serial = "SECOND_TEST_ONLY";
        QVERIFY(stableId(first) != stableId(second));
        first.serial.clear(); second.serial.clear();
        QVERIFY(stableId(first) != stableId(second));
        second = first; second.product = "df11";
        QVERIFY(stableId(first) != stableId(second)); // No invented DFU correlation.
    }
    void states() {
        QVERIFY(allowedTransition(State::Disconnected, State::Detecting));
        QVERIFY(allowedTransition(State::Detecting, State::Connecting));
        QVERIFY(allowedTransition(State::Connecting, State::Connected));
        QVERIFY(allowedTransition(State::Connecting, State::Busy));
        QVERIFY(allowedTransition(State::Busy, State::Connecting));
        QVERIFY(allowedTransition(State::Connected, State::Disconnected));
        QVERIFY(allowedTransition(State::Error, State::Connecting));
        QVERIFY(!allowedTransition(State::Disconnected, State::Connected));
        QVERIFY(!allowedTransition(State::Bootloader, State::Connected));
        for(int i = 0; i <= static_cast<int>(State::Error); ++i) {
            QVERIFY(!stateName(static_cast<State>(i)).isEmpty());
            QVERIFY(allowedTransition(static_cast<State>(i), State::Disconnected));
        }
    }
    void parser() {
        const QByteArray info = "device_info\r\n\x1b[32mhardware_name                 : TEST_ONLY\x1b[0m\r\nfirmware.version : 1.0:test\r\ninvalid output\r\n>: ";
        const auto parsed = parseInfo(info);
        QCOMPARE(parsed["hardware_name"].toString(), "TEST_ONLY");
        QCOMPARE(parsed["firmware_version"].toString(), "1.0:test");
        QCOMPARE(parsed.size(), 2);
        QVERIFY(parseInfo("Unknown command\r\n>: ").isEmpty());
    }
    void fragmentedPrompt() {
        QByteArray buffer("\r\nhardware_name: TEST_ONLY\r\n>");
        QVERIFY(!takeCliResponse(buffer));
        buffer += ':';
        QVERIFY(!takeCliResponse(buffer));
        buffer += ' ';
        QVERIFY(takeCliResponse(buffer));
        QVERIFY(buffer.isEmpty());
        buffer = "some_value: >: ";
        QVERIFY(!takeCliResponse(buffer));
        buffer = "\r\n\x1b[0m>: \x1b[?25h";
        QVERIFY(takeCliResponse(buffer));
    }
    void errors() {
        QCOMPARE(systemError("Open", "/dev/test", EACCES)["code"].toString(), "permission_denied");
        QCOMPARE(systemError("Open", "/dev/test", EBUSY)["code"].toString(), "port_busy");
        QVERIFY(!systemError("Open", "/dev/test", ENODEV)["suggestion"].toString().isEmpty());
    }
    void missingPort() {
        SerialProbe probe;
        QSignalSpy errors(&probe, &SerialProbe::failed);
        probe.start("/dev/omaflip-nonexistent-test-port");
        QCOMPARE(errors.size(), 1);
        QCOMPARE(errors[0][0].toJsonObject()["code"].toString(), "io");
    }
    void serialTransactionAndReconnect() {
        int master = -1, slave = -1;
        char name[128];
        QVERIFY(openpty(&master, &slave, name, nullptr, nullptr) == 0);
        fcntl(master, F_SETFL, O_NONBLOCK);
        close(slave);
        QByteArray commands;
        QSocketNotifier monitor(master, QSocketNotifier::Read);
        connect(&monitor, &QSocketNotifier::activated, this, [&] {
            char buf[1024];
            auto n = read(master, buf, sizeof(buf));
            if(n <= 0) return;
            commands.append(buf, n);
            if(commands.endsWith("device_info\r")) {
                const QByteArray response("device_info\r\nhardware_model : Flipper Zero\r\nhardware_name : TEST_ONLY\r\nfirmware_version : TEST_ONLY\r\n>: ");
                QCOMPARE(write(master, response.constData(), response.size()), response.size());
            } else if(commands.endsWith("info power\r")) {
                const QByteArray response("info power\r\ncharge.level : 0\r\ncharge.state : charging\r\n>: ");
                QCOMPARE(write(master, response.constData(), response.size()), response.size());
            }
        });
        SerialProbe probe;
        QSignalSpy finished(&probe, &SerialProbe::finished);
        QSignalSpy failed(&probe, &SerialProbe::failed);
        for(int attempt = 0; attempt < 3; ++attempt) {
            commands.clear();
            probe.start(QString::fromLocal8Bit(name));
            QTimer::singleShot(80, this, [master] { const QByteArray banner("Test fixture\r\n\r\n>: "); (void)write(master, banner.constData(), banner.size()); });
            QTRY_COMPARE_WITH_TIMEOUT(finished.size(), attempt + 1, 1500);
            QCOMPARE(failed.size(), 0);
            QCOMPARE(finished.last()[0].toJsonObject()["hardware_name"].toString(), "TEST_ONLY");
            QCOMPARE(finished.last()[1].toJsonObject()["charge_level"].toString(), "0");
            QCOMPARE(commands, QByteArray("device_info\rinfo power\r"));
            // Success must release TIOCEXCL, even if the PTY master remains open.
            int reopened = open(name, O_RDWR | O_NOCTTY | O_NONBLOCK);
            QVERIFY(reopened >= 0);
            close(reopened);
        }
        monitor.setEnabled(false);
        close(master);
    }
    void disconnectDuringProbe() {
        int master, slave; char name[128];
        QVERIFY(openpty(&master, &slave, name, nullptr, nullptr) == 0);
        close(slave);
        SerialProbe probe;
        QSignalSpy errors(&probe, &SerialProbe::failed);
        probe.start(QString::fromLocal8Bit(name));
        close(master);
        QTRY_COMPARE_WITH_TIMEOUT(errors.size(), 1, 1000);
    }
    void unsupportedCommand() {
        int master, slave; char name[128];
        QVERIFY(openpty(&master, &slave, name, nullptr, nullptr) == 0);
        close(slave);
        SerialProbe probe;
        QSignalSpy errors(&probe, &SerialProbe::failed);
        QSignalSpy finished(&probe, &SerialProbe::finished);
        probe.start(QString::fromLocal8Bit(name));
        const QByteArray banner("\r\n>: ");
        (void)write(master, banner.constData(), banner.size());
        QTest::qWait(80);
        const QByteArray reply("device_info\r\nUnknown command\r\n>: ");
        (void)write(master, reply.constData(), reply.size());
        QTRY_COMPARE_WITH_TIMEOUT(errors.size(), 1, 1000);
        QCOMPARE(errors[0][0].toJsonObject()["code"].toString(), "unsupported_cli");
        QCOMPARE(finished.size(), 0);
        close(master);
    }
    void timeoutAndCancel() {
        int master, slave; char name[128];
        QVERIFY(openpty(&master, &slave, name, nullptr, nullptr) == 0);
        close(slave);
        SerialProbe probe;
        QSignalSpy errors(&probe, &SerialProbe::failed);
        probe.start(QString::fromLocal8Bit(name));
        QTRY_COMPARE_WITH_TIMEOUT(errors.size(), 1, 6000);
        QCOMPARE(errors[0][0].toJsonObject()["code"].toString(), "cli_timeout");
        probe.start(QString::fromLocal8Bit(name));
        probe.cancel();
        int reopened = open(name, O_RDWR | O_NOCTTY | O_NONBLOCK);
        QVERIFY(reopened >= 0);
        close(reopened);
        close(master);
    }
    void responseLimit() {
        int master, slave; char name[128];
        QVERIFY(openpty(&master, &slave, name, nullptr, nullptr) == 0);
        fcntl(master, F_SETFL, O_NONBLOCK);
        close(slave);
        SerialProbe probe;
        QSignalSpy errors(&probe, &SerialProbe::failed);
        probe.start(QString::fromLocal8Bit(name));
        const QByteArray chunk(4096, 'A');
        for(int i = 0; i < 100 && errors.isEmpty(); ++i) {
            (void)write(master, chunk.constData(), chunk.size());
            QTest::qWait(2);
        }
        QCOMPARE(errors.size(), 1);
        QCOMPARE(errors[0][0].toJsonObject()["code"].toString(), "response_too_large");
        close(master);
    }
};
QTEST_GUILESS_MAIN(CoreTests)
#include "test_core.moc"
