#include "model.h"
#include "serial.h"
#include "rpc.h"
#include "screen.h"
#include "files.h"
#include "cli.h"
#include "apps.h"
#include "firmware.h"
#include "dev.h"
#include "remote.h"
#include <QTemporaryFile>
#include <QTemporaryDir>
#include <QFile>
#include <QJsonArray>
#include <QtTest>
#include <QImage>
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
        usb.description = "CUSTOM_DEVICE_NAME";
        QCOMPARE(classify(usb), UsbKind::FlipperSerial);
        usb.manufacturer = "Unrelated STM32";
        QCOMPARE(classify(usb), UsbKind::Other);
        usb.product = "df11";
        QCOMPARE(classify(usb), UsbKind::DfuCandidate);
        usb.vendor = "9999";
        QCOMPARE(classify(usb), UsbKind::Other);
    }
    void rpcFraming() {
        auto ping = pingRequest(7, "omaflip");
        const auto wire = encodeDelimited(ping);
        QVERIFY(!wire.isEmpty());
        QByteArray buffer = wire;
        PB::Main decoded;
        QCOMPARE(takeDelimited(buffer, decoded), FrameStatus::Ok);
        QVERIFY(buffer.isEmpty());
        QCOMPARE(decoded.command_id(), 7u);
        QVERIFY(decoded.has_system_ping_request());
        QCOMPARE(QByteArray::fromStdString(decoded.system_ping_request().data()), QByteArray("omaflip"));

        QByteArray split = wire;
        QByteArray first = split.left(2);
        QByteArray rest = split.mid(2);
        PB::Main partial;
        QCOMPARE(takeDelimited(first, partial), FrameStatus::NeedMore);
        first += rest;
        QCOMPARE(takeDelimited(first, partial), FrameStatus::Ok);

        QByteArray huge(kMaxRpcFrame + 8, 'x');
        huge[0] = static_cast<char>(0x80 | 0x01);
        huge[1] = static_cast<char>(0x80);
        huge[2] = static_cast<char>(0x04); // varint > 16 KiB
        PB::Main ignored;
        QCOMPARE(takeDelimited(huge, ignored), FrameStatus::TooLarge);

        QByteArray corrupt;
        corrupt.append(char(0x02));
        corrupt.append(char(0xff));
        corrupt.append(char(0xff));
        QCOMPARE(takeDelimited(corrupt, ignored), FrameStatus::Corrupt);
    }
    void screenDecodeAndPng() {
        QByteArray frame(kScreenBytes, '\0');
        const int x = 10, y = 20;
        frame[(y / 8) * kScreenWidth + x] = static_cast<char>(1 << (y & 7));
        const auto image = decodeScreen(frame, 0);
        QCOMPARE(image.width(), kScreenWidth);
        QCOMPARE(image.height(), kScreenHeight);
        QVERIFY(qGray(image.pixel(x, y)) > 128);
        QVERIFY(qGray(image.pixel(0, 0)) < 128);
        const auto png = encodePng(image);
        QVERIFY(png.startsWith("\x89PNG"));
        QImage roundtrip;
        QVERIFY(roundtrip.loadFromData(png, "PNG"));
        QVERIFY(qGray(roundtrip.pixel(x, y)) > 128);
        auto stream = startStreamRequest(3);
        QVERIFY(stream.has_gui_start_screen_stream_request());
        auto input = inputRequest(4, PB_Gui::OK, PB_Gui::SHORT);
        QVERIFY(input.has_gui_send_input_event_request());
        QCOMPARE(input.gui_send_input_event_request().key(), PB_Gui::OK);
        QCOMPARE(input.gui_send_input_event_request().type(), PB_Gui::SHORT);
        const auto wire = encodeDelimited(input);
        QVERIFY(wire.size() > 4);
        PB::Main decoded;
        QByteArray copy = wire;
        QCOMPARE(takeDelimited(copy, decoded), FrameStatus::Ok);
        QVERIFY(decoded.has_gui_send_input_event_request());
        QCOMPARE(decoded.gui_send_input_event_request().key(), PB_Gui::OK);
        QCOMPARE(decoded.gui_send_input_event_request().type(), PB_Gui::SHORT);
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
        QByteArray rpcIncoming;
        bool rpcStarted = false;
        QSocketNotifier monitor(master, QSocketNotifier::Read);
        connect(&monitor, &QSocketNotifier::activated, this, [&] {
            char buf[1024];
            auto n = read(master, buf, sizeof(buf));
            if(n <= 0) return;
            if(rpcStarted) {
                rpcIncoming.append(buf, n);
                PB::Main request;
                while(takeDelimited(rpcIncoming, request) == FrameStatus::Ok) {
                    PB::Main reply;
                    reply.set_command_id(request.command_id());
                    reply.set_command_status(PB::CommandStatus::OK);
                    if(request.has_system_ping_request())
                        reply.mutable_system_ping_response()->set_data(request.system_ping_request().data());
                    else if(request.has_system_protobuf_version_request()) {
                        reply.mutable_system_protobuf_version_response()->set_major(0);
                        reply.mutable_system_protobuf_version_response()->set_minor(25);
                    } else if(request.has_storage_info_request()) {
                        reply.mutable_storage_info_response()->set_total_space(64ull * 1024 * 1024);
                        reply.mutable_storage_info_response()->set_free_space(32ull * 1024 * 1024);
                    }
                    const auto out = encodeDelimited(reply);
                    QCOMPARE(write(master, out.constData(), out.size()), out.size());
                }
                return;
            }
            commands.append(buf, n);
            if(commands.endsWith("device_info\r")) {
                const QByteArray response("device_info\r\nhardware_model : Flipper Zero\r\nhardware_name : TEST_ONLY\r\nfirmware_version : TEST_ONLY\r\n>: ");
                QCOMPARE(write(master, response.constData(), response.size()), response.size());
            } else if(commands.endsWith("info power\r")) {
                const QByteArray response("info power\r\ncharge.level : 0\r\ncharge.state : charging\r\n>: ");
                QCOMPARE(write(master, response.constData(), response.size()), response.size());
            } else if(commands.endsWith("start_rpc_session\r")) {
                rpcStarted = true;
                const QByteArray echo("start_rpc_session\r\n");
                QCOMPARE(write(master, echo.constData(), echo.size()), echo.size());
            }
        });
        SerialProbe probe;
        QSignalSpy finished(&probe, &SerialProbe::finished);
        QSignalSpy failed(&probe, &SerialProbe::failed);
        for(int attempt = 0; attempt < 3; ++attempt) {
            commands.clear();
            rpcIncoming.clear();
            rpcStarted = false;
            probe.start(QString::fromLocal8Bit(name));
            QTimer::singleShot(80, this, [master] { const QByteArray banner("Test fixture\r\n\r\n>: "); (void)write(master, banner.constData(), banner.size()); });
            QTRY_COMPARE_WITH_TIMEOUT(finished.size(), attempt + 1, 3000);
            QCOMPARE(failed.size(), 0);
            QCOMPARE(finished.last()[0].toJsonObject()["hardware_name"].toString(), "TEST_ONLY");
            QCOMPARE(finished.last()[1].toJsonObject()["charge_level"].toString(), "0");
            QCOMPARE(finished.last()[2].toJsonObject()["available"].toBool(), true);
            QCOMPARE(finished.last()[2].toJsonObject()["protobuf"].toString(), "0.25");
            QVERIFY(commands.startsWith("device_info\rinfo power\rstart_rpc_session\r"));
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
    void filesPathRules() {
        QCOMPARE(normalizeDevicePath("/ext/apps"), QString("/ext/apps"));
        QCOMPARE(normalizeDevicePath("/ext/apps/../nfc"), QString("/ext/nfc"));
        QCOMPARE(normalizeDevicePath("/ext/./dolphin"), QString("/ext/dolphin"));
        QCOMPARE(normalizeDevicePath("/"), QString("/"));
        QVERIFY(normalizeDevicePath("/tmp/evil").isEmpty());
        QVERIFY(normalizeDevicePath("ext/apps").isEmpty());
        QVERIFY(normalizeDevicePath("/ext/../../etc").isEmpty());
        QVERIFY(normalizeDevicePath("/ext/" + QString(300, 'a')).isEmpty());
        QVERIFY(normalizeDevicePath(QString("/ext/na\nme")).isEmpty());
        QVERIFY(joinDevicePath("/ext", "../x").isEmpty());
        QVERIFY(joinDevicePath("/ext", "a/b").isEmpty());
        QCOMPARE(joinDevicePath("/ext", "foo.txt"), QString("/ext/foo.txt"));
        QCOMPARE(joinDevicePath("/", "ext"), QString("/ext"));
        QCOMPARE(parentDevicePath("/ext/apps"), QString("/ext"));
        QCOMPARE(parentDevicePath("/ext"), QString("/"));
        QVERIFY(parentDevicePath("/").isEmpty());
        QCOMPARE(deviceBasename("/ext/apps/foo.txt"), QString("foo.txt"));
        QCOMPARE(sanitizeHostName("/tmp/../evil.txt"), QString("evil.txt"));
        QVERIFY(sanitizeHostName("..").isEmpty());
        QVERIFY(looksLikeText("hello\nworld"));
        QVERIFY(!looksLikeText(QByteArray("a\0b", 3)));
        QCOMPARE(previewKindFor("tv.ir", "name: test"), QString("text"));
        QCOMPARE(previewKindFor("game.fap", QByteArray(16, '\x01')), QString("binary"));
    }
    void storageWriteFraming() {
        const auto chunks = storageWriteChunks(9, "/ext/a.bin", QByteArray(1200, 'A'), 512);
        QCOMPARE(chunks.size(), 3);
        QVERIFY(chunks[0].has_next());
        QVERIFY(chunks[1].has_next());
        QVERIFY(!chunks[2].has_next());
        QCOMPARE(chunks[0].command_id(), 9u);
        QCOMPARE(chunks[2].command_id(), 9u);
        QCOMPARE(static_cast<int>(chunks[0].storage_write_request().file().data().size()), 512);
        QCOMPARE(static_cast<int>(chunks[2].storage_write_request().file().data().size()), 176);
        const auto empty = storageWriteChunks(1, "/ext/empty", {});
        QCOMPARE(empty.size(), 1);
        QVERIFY(!empty[0].has_next());
        QVERIFY(empty[0].has_storage_write_request());
        auto list = storageListRequest(3, "/ext");
        QVERIFY(list.has_storage_list_request());
        QCOMPARE(QString::fromStdString(list.storage_list_request().path()), QString("/ext"));
        auto del = storageDeleteRequest(4, "/ext/x", true);
        QVERIFY(del.storage_delete_request().recursive());
    }
    void filesSessionListAndOverwrite() {
        int master = -1, slave = -1;
        char name[128];
        QVERIFY(openpty(&master, &slave, name, nullptr, nullptr) == 0);
        fcntl(master, F_SETFL, O_NONBLOCK);
        close(slave);
        QByteArray rpcIncoming;
        bool rpcStarted = false;
        int listCount = 0, statCount = 0, writeCount = 0;
        QSocketNotifier monitor(master, QSocketNotifier::Read);
        connect(&monitor, &QSocketNotifier::activated, this, [&] {
            char buf[1024];
            auto n = read(master, buf, sizeof(buf));
            if(n <= 0) return;
            if(!rpcStarted) {
                const QByteArray got(buf, n);
                if(got.contains("start_rpc_session\r")) {
                    rpcStarted = true;
                    const QByteArray echo("start_rpc_session\r\n");
                    QCOMPARE(write(master, echo.constData(), echo.size()), echo.size());
                }
                return;
            }
            rpcIncoming.append(buf, n);
            PB::Main request;
            while(takeDelimited(rpcIncoming, request) == FrameStatus::Ok) {
                PB::Main reply;
                reply.set_command_id(request.command_id());
                reply.set_command_status(PB::CommandStatus::OK);
                if(request.has_system_ping_request())
                    reply.mutable_system_ping_response()->set_data(request.system_ping_request().data());
                else if(request.has_storage_list_request()) {
                    ++listCount;
                    auto* file = reply.mutable_storage_list_response()->add_file();
                    file->set_name("apps");
                    file->set_type(PB_Storage::File_FileType_DIR);
                    file = reply.mutable_storage_list_response()->add_file();
                    file->set_name("Manifest");
                    file->set_type(PB_Storage::File_FileType_FILE);
                    file->set_size(100);
                } else if(request.has_storage_stat_request()) {
                    ++statCount;
                    auto* file = reply.mutable_storage_stat_response()->mutable_file();
                    file->set_name("Manifest");
                    file->set_type(PB_Storage::File_FileType_FILE);
                    file->set_size(100);
                } else if(request.has_storage_write_request()) {
                    ++writeCount;
                }
                const auto out = encodeDelimited(reply);
                QCOMPARE(write(master, out.constData(), out.size()), out.size());
            }
        });
        FileSession session;
        QSignalSpy ready(&session, &FileSession::ready);
        QSignalSpy failed(&session, &FileSession::failed);
        session.start(QString::fromLocal8Bit(name));
        QTimer::singleShot(80, this, [master] {
            const QByteArray banner("Test fixture\r\n\r\n>: ");
            (void)write(master, banner.constData(), banner.size());
        });
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(listCount, 1, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!session.busy(), 3000);
        QCOMPARE(failed.size(), 0);
        const auto listing = session.snapshot();
        QCOMPARE(listing["path"].toString(), QString("/ext"));
        QCOMPARE(listing["entries"].toArray().size(), 2);
        QCOMPARE(listing["entries"].toArray()[0].toObject()["name"].toString(), QString("apps"));
        QCOMPARE(listing["entries"].toArray()[0].toObject()["type"].toString(), QString("dir"));

        QTemporaryFile host;
        QVERIFY(host.open());
        QCOMPARE(host.write("hello"), 5);
        host.flush();
        session.upload("/ext/Manifest", host.fileName(), false);
        QTRY_COMPARE_WITH_TIMEOUT(statCount, 1, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!session.busy(), 3000);
        QCOMPARE(writeCount, 0);
        QCOMPARE(session.snapshot()["errorCode"].toString(), QString("exists"));

        session.stop();
        monitor.setEnabled(false);
        close(master);
    }
    void cliHelpAndSanitize() {
        QCOMPARE(sanitizeCliLine("  uptime\r\n"), QString("uptime"));
        QVERIFY(sanitizeCliLine("").isEmpty());
        QVERIFY(isBlockedCliCommand("start_rpc_session"));
        QVERIFY(isBlockedCliCommand("START_RPC_SESSION"));
        QVERIFY(!isBlockedCliCommand("uptime"));
        QVERIFY(isStreamingCommand("log"));
        QVERIFY(isStreamingCommand("log debug"));
        QVERIFY(!isStreamingCommand("log ?"));
        QVERIFY(!isStreamingCommand("log help"));
        QVERIFY(isStreamingCommand("echo"));
        QVERIFY(isStreamingCommand("top"));
        QVERIFY(!isStreamingCommand("top 0"));
        QVERIFY(!isStreamingCommand("uptime"));
        const auto commands = parseHelpCommands(
            "help\r\nAvailable commands:\r\ninfo, !\tDevice information\r\nlog\tSystem log viewer\r\n"
            "start_rpc_session\tSwitch to RPC\r\nuptime Time since reboot\r\n>: \r\n");
        QCOMPARE(commands.size(), 5);
        QCOMPARE(commands[0].toObject()["name"].toString(), QString("info"));
        QCOMPARE(commands[1].toObject()["name"].toString(), QString("!"));
        QCOMPARE(commands[2].toObject()["name"].toString(), QString("log"));
        QCOMPARE(commands[2].toObject()["summary"].toString(), QString("System log viewer"));
        QVERIFY(commands[3].toObject()["name"].toString() == QString("start_rpc_session"));
        QCOMPARE(commands[4].toObject()["name"].toString(), QString("uptime"));
    }
    void cliSessionHelpAndBlock() {
        int master = -1, slave = -1;
        char name[128];
        QVERIFY(openpty(&master, &slave, name, nullptr, nullptr) == 0);
        fcntl(master, F_SETFL, O_NONBLOCK);
        close(slave);
        QByteArray incoming;
        bool sawRpc = false;
        int helpCount = 0, uptimeCount = 0, etxCount = 0;
        QSocketNotifier monitor(master, QSocketNotifier::Read);
        connect(&monitor, &QSocketNotifier::activated, this, [&] {
            char buf[1024];
            auto n = read(master, buf, sizeof(buf));
            if(n <= 0) return;
            incoming.append(buf, n);
            if(incoming.contains('\x03')) {
                ++etxCount;
                incoming.replace('\x03', "");
                const QByteArray prompt("\r\n>: ");
                QCOMPARE(write(master, prompt.constData(), prompt.size()), prompt.size());
            }
            if(incoming.contains("start_rpc_session")) sawRpc = true;
            if(incoming.endsWith("help\r")) {
                ++helpCount;
                incoming.clear();
                const QByteArray reply("help\r\ninfo\tDevice\r\nlog\tLogs\r\nuptime\r\n>: ");
                QCOMPARE(write(master, reply.constData(), reply.size()), reply.size());
            } else if(incoming.endsWith("uptime\r")) {
                ++uptimeCount;
                incoming.clear();
                const QByteArray reply("uptime\r\nUptime: 1h0m0s\r\n>: ");
                QCOMPARE(write(master, reply.constData(), reply.size()), reply.size());
            }
        });
        CliSession session;
        QSignalSpy ready(&session, &CliSession::ready);
        QSignalSpy failed(&session, &CliSession::failed);
        session.start(QString::fromLocal8Bit(name));
        QTimer::singleShot(80, this, [master] {
            const QByteArray banner("Welcome\r\n>: ");
            (void)write(master, banner.constData(), banner.size());
        });
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 3000);
        QCOMPARE(failed.size(), 0);
        QCOMPARE(helpCount, 1);
        QCOMPARE(session.snapshot()["commands"].toArray().size(), 3);
        session.sendLine("uptime");
        QTRY_COMPARE_WITH_TIMEOUT(uptimeCount, 1, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!session.busy(), 3000);
        QVERIFY(session.snapshot()["output"].toString().contains("Uptime:"));
        session.sendLine("start_rpc_session");
        QCOMPARE(sawRpc, false);
        QVERIFY(session.snapshot()["error"].toString().contains("Remote or Files"));
        session.sendLine("log");
        QTRY_VERIFY_WITH_TIMEOUT(session.snapshot()["streaming"].toBool(), 1000);
        session.interrupt();
        QTRY_COMPARE_WITH_TIMEOUT(etxCount, 1, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!session.snapshot()["streaming"].toBool(), 3000);
        session.stop();
        monitor.setEnabled(false);
        close(master);
    }
    void appsPathRules() {
        QCOMPARE(appKindForName("snake.fap"), QString("fap"));
        QCOMPARE(appKindForName("hello.js"), QString("js"));
        QVERIFY(appKindForName("notes.txt").isEmpty());
        QVERIFY(isAppStoragePath("/ext/apps"));
        QVERIFY(isAppStoragePath("/ext/apps/Games/snake.fap"));
        QVERIFY(!isAppStoragePath("/ext/badusb/x.txt"));
        QCOMPARE(defaultInstallDir("hello.js"), QString("/ext/apps/Scripts"));
        QCOMPARE(defaultInstallDir("game.fap"), QString("/ext/apps/Misc"));
        auto start = appStartRequest(3, "JS Runner", "/ext/apps/Scripts/a.js");
        QVERIFY(start.has_app_start_request());
        QCOMPARE(QString::fromStdString(start.app_start_request().name()), QString("JS Runner"));
        QCOMPARE(QString::fromStdString(start.app_start_request().args()), QString("/ext/apps/Scripts/a.js"));
        QVERIFY(appExitRequest(4).has_app_exit_request());
    }
    void appsSessionInventoryAndLaunch() {
        int master = -1, slave = -1;
        char name[128];
        QVERIFY(openpty(&master, &slave, name, nullptr, nullptr) == 0);
        fcntl(master, F_SETFL, O_NONBLOCK);
        close(slave);
        QByteArray rpcIncoming;
        bool rpcStarted = false;
        int listCount = 0, startCount = 0;
        QString lastStart;
        QSocketNotifier monitor(master, QSocketNotifier::Read);
        connect(&monitor, &QSocketNotifier::activated, this, [&] {
            char buf[1024];
            auto n = read(master, buf, sizeof(buf));
            if(n <= 0) return;
            if(!rpcStarted) {
                const QByteArray got(buf, n);
                if(got.contains("start_rpc_session\r")) {
                    rpcStarted = true;
                    const QByteArray echo("start_rpc_session\r\n");
                    QCOMPARE(write(master, echo.constData(), echo.size()), echo.size());
                }
                return;
            }
            rpcIncoming.append(buf, n);
            PB::Main request;
            while(takeDelimited(rpcIncoming, request) == FrameStatus::Ok) {
                PB::Main reply;
                reply.set_command_id(request.command_id());
                reply.set_command_status(PB::CommandStatus::OK);
                if(request.has_system_ping_request())
                    reply.mutable_system_ping_response()->set_data(request.system_ping_request().data());
                else if(request.has_storage_list_request()) {
                    ++listCount;
                    const auto path = QString::fromStdString(request.storage_list_request().path());
                    if(path == "/ext/apps") {
                        auto* dir = reply.mutable_storage_list_response()->add_file();
                        dir->set_name("Games");
                        dir->set_type(PB_Storage::File_FileType_DIR);
                    } else if(path == "/ext/apps/Games") {
                        auto* file = reply.mutable_storage_list_response()->add_file();
                        file->set_name("snake.fap");
                        file->set_type(PB_Storage::File_FileType_FILE);
                        file->set_size(2048);
                    }
                } else if(request.has_app_start_request()) {
                    ++startCount;
                    lastStart = QString::fromStdString(request.app_start_request().name());
                }
                const auto out = encodeDelimited(reply);
                QCOMPARE(write(master, out.constData(), out.size()), out.size());
            }
        });
        AppSession session;
        QSignalSpy ready(&session, &AppSession::ready);
        QSignalSpy failed(&session, &AppSession::failed);
        session.start(QString::fromLocal8Bit(name));
        QTimer::singleShot(80, this, [master] {
            const QByteArray banner("Welcome\r\n>: ");
            (void)write(master, banner.constData(), banner.size());
        });
        QTRY_COMPARE_WITH_TIMEOUT(ready.size(), 1, 3000);
        QTRY_COMPARE_WITH_TIMEOUT(listCount, 2, 3000);
        QTRY_VERIFY_WITH_TIMEOUT(!session.busy(), 3000);
        QCOMPARE(failed.size(), 0);
        QCOMPARE(session.snapshot()["apps"].toArray().size(), 1);
        QCOMPARE(session.snapshot()["apps"].toArray()[0].toObject()["name"].toString(), QString("snake.fap"));
        session.launch("/ext/apps/Games/snake.fap");
        QTRY_COMPARE_WITH_TIMEOUT(startCount, 1, 3000);
        QCOMPARE(lastStart, QString("/ext/apps/Games/snake.fap"));
        session.stop();
        monitor.setEnabled(false);
        close(master);
    }
    void backupMetadataCompat() {
        const QJsonObject info{{"firmware_version", "mntm-0.1"}, {"firmware_origin_fork", "Momentum"},
            {"hardware_target", "7"}, {"hardware_name", "Flippie"}};
        const auto meta = backupMetadata(info, {{"protobuf", "0.25"}}, "omaflip-1.tar");
        QCOMPARE(meta["omaflip_backup"].toInt(), 1);
        QCOMPARE(meta["hardware_target"].toString(), QString("f7"));
        QCOMPARE(backupCompat(meta, info), QString("ok"));
        QJsonObject other = info; other["firmware_version"] = "mntm-0.2";
        QCOMPARE(backupCompat(meta, other), QString("version_mismatch"));
        other = info; other["firmware_origin_fork"] = "Official";
        QCOMPARE(backupCompat(meta, other), QString("origin_mismatch"));
        other = info; other["hardware_target"] = "18";
        QCOMPARE(backupCompat(meta, other), QString("target_mismatch"));
        QVERIFY(storageBackupCreateRequest(9, "/ext/omaflip_backup/a.tar").has_storage_backup_create_request());
        QVERIFY(systemUpdateRequest(10, "/ext/update/update.fuf").has_system_update_request());
    }
    void officialFirmwareIndex() {
        const QByteArray json = "{\"channels\":[{\"id\":\"release\",\"title\":\"Stable\",\"versions\":[{\"version\":\"1.4.3\",\"changelog\":\"fix\","
            "\"files\":[{\"url\":\"https://update.flipperzero.one/builds/firmware/1.4.3/flipper-z-f7-update-1.4.3.tgz\",\"target\":\"f7\",\"type\":\"update_tgz\",\"sha256\":\"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa\"}]}]}]}";
        QString error;
        const auto package = parseOfficialIndex(json, "release", "f7", error);
        QVERIFY(error.isEmpty());
        QCOMPARE(package["version"].toString(), QString("1.4.3"));
        QCOMPARE(package["provider"].toString(), QString("official"));
        QVERIFY(urlAllowed(package["url"].toString()));
        QVERIFY(!urlAllowed("http://evil.example/x.tgz"));
        QVERIFY(!urlAllowed("https://example.com/x.tgz"));
        const QByteArray data("omaflip");
        QVERIFY(!verifySha256(data, "aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa"));
        QCOMPARE(hardwareTargetName("7"), QString("f7"));
        QCOMPARE(updateManifestPath(package), QString("/ext/update/f7-update-1.4.3/update.fuf"));
    }
    void momentumOriginAndIndex() {
        QCOMPARE(originKind("Momentum"), QString("Momentum"));
        QCOMPARE(originKind("Official"), QString("Official"));
        QCOMPARE(originKind("Unleashed"), QString("Other"));
        QCOMPARE(originKind(""), QString("Unknown"));
        QVERIFY(channelAllowed("momentum", "release"));
        QVERIFY(channelAllowed("momentum", "development"));
        QVERIFY(!channelAllowed("momentum", "pr503:956/prs"));
        const QByteArray json = "{\"channels\":["
            "{\"id\":\"release\",\"title\":\"Stable Release Channel\",\"versions\":[{\"version\":\"mntm-012\",\"changelog\":\"apps\","
            "\"files\":[{\"url\":\"https://up.momentum-fw.dev/builds/firmware/mntm-012/flipper-z-f7-update-mntm-012.tgz\",\"target\":\"f7\",\"type\":\"update_tgz\",\"sha256\":\"bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb\"}]}]},"
            "{\"id\":\"development\",\"title\":\"Development Channel\",\"versions\":[{\"version\":\"d3f89dfe\",\"changelog\":\"dev\","
            "\"files\":[{\"url\":\"https://up.momentum-fw.dev/builds/firmware/dev/flipper-z-f7-update-mntm-dev-d3f89dfe.tgz\",\"target\":\"f7\",\"type\":\"update_tgz\",\"sha256\":\"cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc\"}]}]}]}";
        QString error;
        const auto release = parseFirmwareIndex(json, "momentum", "release", "f7", error);
        QVERIFY(error.isEmpty());
        QCOMPARE(release["provider"].toString(), QString("momentum"));
        QCOMPARE(release["version"].toString(), QString("mntm-012"));
        QVERIFY(urlAllowedFor(release["url"].toString(), "momentum"));
        QVERIFY(!urlAllowed(release["url"].toString()));
        QCOMPARE(updateManifestPath(release), QString("/ext/update/f7-update-mntm-012/update.fuf"));
        error.clear();
        const auto development = parseFirmwareIndex(json, "momentum", "development", "f7", error);
        QVERIFY(error.isEmpty());
        QCOMPARE(updateManifestPath(development), QString("/ext/update/f7-update-mntm-dev-d3f89dfe/update.fuf"));
        error.clear();
        QVERIFY(parseFirmwareIndex(json, "momentum", "pr503:956/prs", "f7", error).isEmpty());
        QVERIFY(error.contains("release and development"));
        error.clear();
        QVERIFY(parseFirmwareIndex(json, "official", "release", "f7", error).isEmpty());
        QVERIFY(error.contains("update.flipperzero.one") || error.contains("official"));
    }
    void momentumAssetIndex() {
        const QByteArray json = "{\"packs\":["
            "{\"id\":\"blank-screen\",\"name\":\"Blank Screen\",\"author\":\"pr3\",\"description\":\"blank\","
            "\"files\":[{\"url\":\"https://up.momentum-fw.dev/builds/asset-packs/blank-screen/download/blank-screen.tar.gz\",\"type\":\"pack_targz\",\"sha256\":\"dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd\"},"
            "{\"url\":\"https://up.momentum-fw.dev/builds/asset-packs/blank-screen/download/blank-screen.zip\",\"type\":\"pack_zip\",\"sha256\":\"eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee\"}],"
            "\"stats\":{\"anims\":2,\"icons\":0,\"folders\":[\"pr3 - Blank Screen\"]}},"
            "{\"id\":\"evil\",\"name\":\"Evil\",\"author\":\"x\",\"description\":\"no\","
            "\"files\":[{\"url\":\"https://example.com/evil.tar.gz\",\"type\":\"pack_targz\",\"sha256\":\"ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff\"}],"
            "\"stats\":{\"anims\":0,\"icons\":0,\"folders\":[\"Evil\"]}}]}";
        QString error;
        const auto catalog = parseAssetIndex(json, error);
        QVERIFY(error.isEmpty());
        QCOMPARE(catalog.size(), 1);
        QCOMPARE(catalog[0].toObject()["id"].toString(), QString("blank-screen"));
        QCOMPARE(catalog[0].toObject()["folders"].toArray()[0].toString(), QString("pr3 - Blank Screen"));
        QVERIFY(urlAllowedFor(catalog[0].toObject()["url"].toString(), "pack"));
        QVERIFY(!urlAllowedFor("https://example.com/x.tar.gz", "pack"));
    }
    void ufbtProjectHelpers() {
        QCOMPARE(sanitizeAppId("Hello_App"), QString("hello_app"));
        QVERIFY(sanitizeAppId("1bad").isEmpty());
        QVERIFY(sanitizeAppId("bad-id").isEmpty());
        QVERIFY(sanitizeAppId("APPID=hello;rm").isEmpty());
        const auto fam = parseApplicationFam("App(\n    appid=\"hello_app\",\n    name=\"Hello\",\n    fap_category=\"GPIO\",\n)\n");
        QCOMPARE(fam["appid"].toString(), QString("hello_app"));
        QCOMPARE(fam["fap_category"].toString(), QString("GPIO"));
        const auto momentum = ufbtUpdateArgs("Momentum", "mntm-dev");
        QCOMPARE(momentum[0], QString("update"));
        QVERIFY(momentum.contains("--channel=dev"));
        QVERIFY(momentum.contains("--index-url=https://up.momentum-fw.dev/firmware/directory.json"));
        const auto official = ufbtUpdateArgs("Official", "1.4.3");
        QVERIFY(official.contains("--channel=release"));
        QVERIFY(!official.join(' ').contains("momentum-fw.dev"));
        QVERIFY(inspectKindAllowed("ping"));
        QVERIFY(inspectKindAllowed("property"));
        QVERIFY(!inspectKindAllowed("flash"));
        QVERIFY(!inspectKindAllowed("gpio"));
        QVERIFY(!inspectKindAllowed("reboot"));
        QVERIFY(defaultDevProject().endsWith("/OmaFlip/projects"));
        QTemporaryDir extra;
        QVERIFY(extra.isValid());
        QFile fake(extra.path() + "/ufbt");
        QVERIFY(fake.open(QIODevice::WriteOnly));
        fake.write("#!/bin/sh\n");
        fake.close();
        QVERIFY(fake.setPermissions(QFileDevice::ExeOwner | QFileDevice::ReadOwner | QFileDevice::WriteOwner));
        const auto found = findUfbt({extra.path()});
        QVERIFY(found.endsWith("/ufbt"));
        QCOMPARE(ufbtCommand({extra.path()}).first(), found);
    }
    void remoteVisualKeys() {
        QCOMPARE(remoteKeyFromVisual("up", 0), QString("up"));
        QCOMPARE(remoteKeyFromVisual("left", 0), QString("left"));
        QCOMPARE(remoteKeyFromVisual("up", 1), QString("down"));
        QCOMPARE(remoteKeyFromVisual("left", 1), QString("right"));
        QCOMPARE(remoteKeyFromVisual("up", 2), QString("right"));
        QCOMPARE(remoteKeyFromVisual("down", 2), QString("left"));
        QCOMPARE(remoteKeyFromVisual("up", 3), QString("left"));
        QCOMPARE(remoteKeyFromVisual("right", 3), QString("up"));
    }
    void desktopNotifyArgsAllowlist() {
        const auto args = desktopNotifyArgs("device.added", "normal", "Flipper connected", "Flippie is Connected.");
        QVERIFY(args.contains("--app-name=OmaFlip"));
        QVERIFY(args.contains("--category=device.added"));
        QVERIFY(args.contains("--urgency=normal"));
        QCOMPARE(desktopNotifyArgs("http.request", "normal", "no", "no").size(), 0);
        QVERIFY(desktopNotifyCategoryAllowed("device.removed"));
        QVERIFY(desktopNotifyCategoryAllowed("device.error"));
        QVERIFY(!desktopNotifyCategoryAllowed("transfer"));
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
