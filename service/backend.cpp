#include "backend.h"
#include "screen.h"
#include <QDateTime>
#include <QTimer>
#include <cstdio>

namespace omaflip {
Backend::Backend(QObject* parent) : QObject(parent), discovery_(this), firmwareClient_(this) {
    connect(&discovery_, &Discovery::changed, this, &Backend::reconcile);
    connect(&firmwareClient_, &FirmwareClient::checked, this, [this](const QJsonObject& package) {
        firmware_ = package;
        firmware_.insert("checked", true);
        firmware_.insert("downloading", false);
        error_ = {};
        publish();
    });
    connect(&firmwareClient_, &FirmwareClient::downloaded, this, [this](const QString& path, const QJsonObject& package) {
        firmware_ = package;
        firmware_.insert("checked", true);
        firmware_.insert("path", path);
        firmware_.insert("verified", true);
        firmware_.insert("downloading", false);
        error_ = {};
        publish();
    });
    connect(&firmwareClient_, &FirmwareClient::packsChecked, this, [this](const QJsonArray& catalog) {
        packs_.insert("checked", true);
        packs_.insert("catalog", catalog);
        packs_.insert("downloading", false);
        error_ = {};
        publish();
    });
    connect(&firmwareClient_, &FirmwareClient::packDownloaded, this, [this](const QString& path, const QJsonObject& pack) {
        const auto catalog = packs_.value("catalog");
        packs_ = pack;
        if(catalog.isArray()) packs_.insert("catalog", catalog);
        packs_.insert("checked", true);
        packs_.insert("path", path);
        packs_.insert("verified", true);
        packs_.insert("downloading", false);
        error_ = {};
        publish();
    });
    connect(&firmwareClient_, &FirmwareClient::failed, this, [this](const QJsonObject& error) {
        firmware_.insert("downloading", false);
        packs_.insert("downloading", false);
        error_ = error;
        publish();
    });
    connect(&firmwareClient_, &FirmwareClient::progress, this, [this](qint64 received, qint64 total) {
        auto& target = packs_.value("downloading").toBool() ? packs_ : firmware_;
        target.insert("downloading", true);
        target.insert("received", received);
        target.insert("total", total);
        publish();
    });
}
bool Backend::start(QString& error) {
    // Subscribe before enumeration so attachment during startup is never lost.
    if(!discovery_.start(error)) return false;
    reconcile(); return true;
}
void Backend::transition(Device& device, State next) {
    if(!allowedTransition(device.state, next)) qFatal("Invalid device state transition");
    device.state = next;
}
void Backend::publish() { emit event(snapshot()); }
void Backend::chooseSelection() {
    if(devices_.contains(selected_)) return;
    selected_.clear();
    QStringList matches;
    for(auto it = devices_.begin(); it != devices_.end(); ++it)
        if(stableId(it->usb) == preferred_) matches.append(it.key());
    if(matches.size() == 1) selected_ = matches.first();
    else if(devices_.size() == 1) selected_ = devices_.firstKey();
}
void Backend::reconcile() {
    const auto live = discovery_.scan();
    for(auto it = devices_.begin(); it != devices_.end();) {
        if(!live.contains(it.key()) || live.value(it.key()) != it->usb) {
            const auto previous = lastAnnounced_.take(it.key());
            if(notifyConnect_ && (previous == "Connected" || previous == "Busy"))
                desktopNotify("device.removed", "low", "Flipper disconnected", "The Flipper was unplugged.");
            if(it->probe) { it->probe->cancel(); it->probe->deleteLater(); }
            if(it->remote) { it->remote->stop(); it->remote->deleteLater(); it->remote = nullptr; }
            if(it->files) { it->files->stop(); it->files->deleteLater(); it->files = nullptr; }
            if(it->cli) { it->cli->stop(); it->cli->deleteLater(); it->cli = nullptr; }
            if(it->apps) { it->apps->stop(); it->apps->deleteLater(); it->apps = nullptr; }
            if(it->manage) { it->manage->stop(); it->manage->deleteLater(); it->manage = nullptr; }
            if(it->dev) { it->dev->stop(); it->dev->deleteLater(); it->dev = nullptr; }
            transition(*it, State::Disconnected);
            it = devices_.erase(it);
        } else ++it;
    }
    QStringList added;
    for(auto it = live.begin(); it != live.end(); ++it) {
        if(devices_.contains(it.key())) continue;
        Device device; device.usb = it.value();
        transition(device, State::Detecting);
        if(classify(device.usb) == UsbKind::DfuCandidate) transition(device, State::Bootloader);
        devices_.insert(it.key(), device);
        added.append(it.key());
    }
    // Never silently select an arbitrary device when several are attached.
    chooseSelection();
    publish();
    if(autoConnect_) for(const auto& key : added) if(!devices_[key].usb.port.isEmpty()) probe(key);
}
void Backend::probe(const QString& key) {
    auto it = devices_.find(key);
    if(it == devices_.end() || it->probe || it->remote || it->files || it->cli || it->apps || it->manage || it->dev || it->usb.port.isEmpty() || it->state == State::Bootloader) return;
    transition(*it, State::Connecting);
    it->error = {}; it->info = {}; it->power = {}; it->rpc = {}; it->warning.clear(); it->sampledAt.clear();
    auto* session = new SerialProbe(this); it->probe = session;
    connect(session, &SerialProbe::finished, this, [this,key,session](const QJsonObject& info, const QJsonObject& power, const QJsonObject& rpc, const QString& warning) {
        auto it = devices_.find(key);
        if(it == devices_.end() || it->probe != session) return;
        it->info = info; it->power = power; it->rpc = rpc; it->warning = warning;
        it->sampledAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        const auto previous = lastAnnounced_.value(key);
        transition(*it, State::Connected); it->probe = nullptr;
        announce(key, *it, previous);
        session->deleteLater(); publish();
    });
    connect(session, &SerialProbe::failed, this, [this,key,session](const QJsonObject& error) {
        auto it = devices_.find(key);
        if(it == devices_.end() || it->probe != session) return;
        it->error = error;
        const auto previous = lastAnnounced_.value(key);
        transition(*it, error["code"] == "port_busy" ? State::Busy : State::Error);
        it->probe = nullptr;
        announce(key, *it, previous);
        session->deleteLater(); publish();
    });
    publish(); session->start(it->usb.port);
}
QJsonObject Backend::snapshot() const {
    QJsonArray devices;
    for(auto it = devices_.begin(); it != devices_.end(); ++it) {
        const auto& device = it.value();
        devices.append(QJsonObject{{"key", it.key()}, {"id", stableId(device.usb)},
            {"state", stateName(device.state)}, {"port", device.usb.port}, {"usbSerial", device.usb.serial},
            {"usbProduct", device.usb.description}, {"usbVendorId", device.usb.vendor}, {"usbProductId", device.usb.product},
            {"identityConfirmed", classify(device.usb) == UsbKind::FlipperSerial},
            {"origin", originKind(device.info.value("firmware_origin_fork").toString())},
            {"info", device.info}, {"power", device.power}, {"error", device.error},
            {"warning", device.warning}, {"sampledAt", device.sampledAt},
            {"rpc", device.rpc.isEmpty() ? QJsonObject{{"available", false}, {"summary", "RPC not started"}} : device.rpc},
            {"remote", device.remote != nullptr},
            {"files", device.files ? device.files->snapshot() : QJsonObject{{"open", false}}},
            {"cli", device.cli ? device.cli->snapshot() : QJsonObject{{"open", false}}},
            {"apps", device.apps ? device.apps->snapshot() : QJsonObject{{"open", false}}},
            {"manage", device.manage ? device.manage->snapshot() : QJsonObject{{"open", false}}},
            {"dev", device.dev ? device.dev->snapshot() : QJsonObject{{"open", false}}},
            {"screenshots", QJsonArray::fromStringList(device.screenshots)}});
    }
    return {{"protocol", 1}, {"type", "snapshot"}, {"version", OMAFLIP_VERSION},
        {"devices", devices}, {"selected", selected_},
        {"error", error_}, {"firmware", firmware_}, {"packs", packs_}};
}

void Backend::announce(const QString& key, const Device& device, const QString& previous) {
    const auto now = stateName(device.state);
    lastAnnounced_[key] = now;
    const auto name = device.info.value("hardware_name").toString("Flipper");
    if(now == "Connected" && previous != "Busy" && previous != "Connected" && notifyConnect_)
        desktopNotify("device.added", "normal", "Flipper connected", name + " is Connected.");
    else if(now == "Error" && previous != "Error" && notifyError_) {
        const auto reason = device.error.value("reason").toString("The Flipper reported an error.");
        desktopNotify("device.error", "critical", "Flipper error", name + ": " + reason);
    }
}
void Backend::publishFrame(const QString& key, const QByteArray& png, int orientation) {
    emit event(QJsonObject{{"protocol", 1}, {"type", "frame"}, {"key", key},
        {"data", QString::fromLatin1(png)}, {"orientation", orientation}});
}
QString Backend::holderName(const Device& device) const {
    if(device.remote) return "Remote";
    if(device.files) return "Files";
    if(device.cli) return "CLI";
    if(device.apps) return "Apps";
    if(device.manage) return "Device";
    if(device.dev) return "Dev";
    return {};
}

bool Backend::refuseHeldPort(const Device& device, const QString& action) {
    const auto holder = holderName(device);
    if(holder.isEmpty()) return false;
    error_ = {{"code", "port_busy"}, {"reason", action + " cannot run while " + holder + " holds the serial port."},
        {"suggestion", "Close " + holder + " first."}};
    publish();
    return true;
}

void Backend::startRemote(const QString& key) {
    auto it = devices_.find(key);
    if(it == devices_.end() || it->usb.port.isEmpty() || it->state == State::Bootloader) {
        error_ = {{"code", "invalid_request"}, {"reason", "Remote needs an attached Flipper with a serial port."},
            {"suggestion", "Connect the device and wait until it is Connected."}};
        publish(); return;
    }
    if(const auto holder = holderName(*it); !holder.isEmpty() && holder != "Remote") {
        error_ = {{"code", "port_busy"}, {"reason", holder + " is using the serial port."},
            {"suggestion", "Close " + holder + " before opening Remote."}};
        publish(); return;
    }
    if(it->remote) { publish(); return; }
    if(it->probe) { it->probe->cancel(); it->probe->deleteLater(); it->probe = nullptr; }
    if(it->state == State::Connected) transition(*it, State::Busy);
    auto* session = new RemoteSession(this); it->remote = session;
    connect(session, &RemoteSession::ready, this, [this,key,session] {
        auto it = devices_.find(key);
        if(it == devices_.end() || it->remote != session) return;
        publish();
    });
    connect(session, &RemoteSession::frame, this, [this,key,session](const QByteArray& png, int orientation) {
        auto it = devices_.find(key);
        if(it == devices_.end() || it->remote != session) return;
        publishFrame(key, png, orientation);
    });
    connect(session, &RemoteSession::failed, this, [this,key,session](const QJsonObject& error) {
        auto it = devices_.find(key);
        if(it == devices_.end() || it->remote != session) return;
        it->error = error; it->remote = nullptr; session->deleteLater();
        transition(*it, State::Error); publish();
    });
    connect(session, &RemoteSession::stopped, this, [this,key,session] {
        auto it = devices_.find(key);
        if(it == devices_.end() || it->remote != session) return;
        it->remote = nullptr; session->deleteLater();
        if(it->state == State::Busy) transition(*it, State::Connected);
        publish();
    });
    publish(); session->start(it->usb.port);
}
void Backend::stopRemote(const QString& key) {
    auto it = devices_.find(key);
    if(it == devices_.end() || !it->remote) { publish(); return; }
    it->remote->stop();
}

void Backend::startFiles(const QString& key) {
    auto it = devices_.find(key);
    if(it == devices_.end() || it->usb.port.isEmpty() || it->state == State::Bootloader) {
        error_ = {{"code", "invalid_request"}, {"reason", "Files needs an attached Flipper with a serial port."},
            {"suggestion", "Connect the device and wait until it is Connected."}};
        publish(); return;
    }
    if(const auto holder = holderName(*it); !holder.isEmpty() && holder != "Files") {
        error_ = {{"code", "port_busy"}, {"reason", holder + " is using the serial port."},
            {"suggestion", "Close " + holder + " before opening Files."}};
        publish(); return;
    }
    if(it->files) { publish(); return; }
    if(it->probe) { it->probe->cancel(); it->probe->deleteLater(); it->probe = nullptr; }
    if(it->state == State::Connected) transition(*it, State::Busy);
    auto* session = new FileSession(this); it->files = session;
    connect(session, &FileSession::ready, this, [this,key,session] {
        auto it = devices_.find(key);
        if(it == devices_.end() || it->files != session) return;
        publish();
    });
    connect(session, &FileSession::changed, this, [this,key,session] {
        auto it = devices_.find(key);
        if(it == devices_.end() || it->files != session) return;
        publish();
    });
    connect(session, &FileSession::failed, this, [this,key,session](const QJsonObject& error) {
        auto it = devices_.find(key);
        if(it == devices_.end() || it->files != session) return;
        it->error = error; it->files = nullptr; session->deleteLater();
        transition(*it, State::Error); publish();
    });
    connect(session, &FileSession::stopped, this, [this,key,session] {
        auto it = devices_.find(key);
        if(it == devices_.end() || it->files != session) return;
        it->files = nullptr; session->deleteLater();
        if(it->state == State::Busy) transition(*it, State::Connected);
        publish();
    });
    publish(); session->start(it->usb.port);
}

void Backend::stopFiles(const QString& key) {
    auto it = devices_.find(key);
    if(it == devices_.end() || !it->files) { publish(); return; }
    it->files->stop();
}

void Backend::startCli(const QString& key) {
    auto it = devices_.find(key);
    if(it == devices_.end() || it->usb.port.isEmpty() || it->state == State::Bootloader) {
        error_ = {{"code", "invalid_request"}, {"reason", "CLI needs an attached Flipper with a serial port."},
            {"suggestion", "Connect the device and wait until it is Connected."}};
        publish(); return;
    }
    if(const auto holder = holderName(*it); !holder.isEmpty() && holder != "CLI") {
        error_ = {{"code", "port_busy"}, {"reason", holder + " is using the serial port."},
            {"suggestion", "Close " + holder + " before opening CLI."}};
        publish(); return;
    }
    if(it->cli) { publish(); return; }
    if(it->probe) { it->probe->cancel(); it->probe->deleteLater(); it->probe = nullptr; }
    if(it->state == State::Connected) transition(*it, State::Busy);
    auto* session = new CliSession(this); it->cli = session;
    connect(session, &CliSession::ready, this, [this,key,session] {
        auto it = devices_.find(key);
        if(it == devices_.end() || it->cli != session) return;
        publish();
    });
    connect(session, &CliSession::changed, this, [this,key,session] {
        auto it = devices_.find(key);
        if(it == devices_.end() || it->cli != session) return;
        publish();
    });
    connect(session, &CliSession::failed, this, [this,key,session](const QJsonObject& error) {
        auto it = devices_.find(key);
        if(it == devices_.end() || it->cli != session) return;
        it->error = error; it->cli = nullptr; session->deleteLater();
        transition(*it, State::Error); publish();
    });
    connect(session, &CliSession::stopped, this, [this,key,session] {
        auto it = devices_.find(key);
        if(it == devices_.end() || it->cli != session) return;
        it->cli = nullptr; session->deleteLater();
        if(it->state == State::Busy) transition(*it, State::Connected);
        publish();
    });
    publish(); session->start(it->usb.port);
}

void Backend::stopCli(const QString& key) {
    auto it = devices_.find(key);
    if(it == devices_.end() || !it->cli) { publish(); return; }
    it->cli->stop();
}

void Backend::startApps(const QString& key) {
    auto it = devices_.find(key);
    if(it == devices_.end() || it->usb.port.isEmpty() || it->state == State::Bootloader) {
        error_ = {{"code", "invalid_request"}, {"reason", "Apps needs an attached Flipper with a serial port."},
            {"suggestion", "Connect the device and wait until it is Connected."}};
        publish(); return;
    }
    if(const auto holder = holderName(*it); !holder.isEmpty() && holder != "Apps") {
        error_ = {{"code", "port_busy"}, {"reason", holder + " is using the serial port."},
            {"suggestion", "Close " + holder + " before opening Apps."}};
        publish(); return;
    }
    if(it->apps) { publish(); return; }
    if(it->probe) { it->probe->cancel(); it->probe->deleteLater(); it->probe = nullptr; }
    if(it->state == State::Connected) transition(*it, State::Busy);
    auto* session = new AppSession(this); it->apps = session;
    connect(session, &AppSession::ready, this, [this,key,session] {
        auto it = devices_.find(key);
        if(it == devices_.end() || it->apps != session) return;
        publish();
    });
    connect(session, &AppSession::changed, this, [this,key,session] {
        auto it = devices_.find(key);
        if(it == devices_.end() || it->apps != session) return;
        publish();
    });
    connect(session, &AppSession::failed, this, [this,key,session](const QJsonObject& error) {
        auto it = devices_.find(key);
        if(it == devices_.end() || it->apps != session) return;
        it->error = error; it->apps = nullptr; session->deleteLater();
        transition(*it, State::Error); publish();
    });
    connect(session, &AppSession::stopped, this, [this,key,session] {
        auto it = devices_.find(key);
        if(it == devices_.end() || it->apps != session) return;
        it->apps = nullptr; session->deleteLater();
        if(it->state == State::Busy) transition(*it, State::Connected);
        publish();
    });
    publish(); session->start(it->usb.port);
}

void Backend::stopApps(const QString& key) {
    auto it = devices_.find(key);
    if(it == devices_.end() || !it->apps) { publish(); return; }
    it->apps->stop();
}

void Backend::startManage(const QString& key) {
    auto it = devices_.find(key);
    if(it == devices_.end() || it->usb.port.isEmpty() || it->state == State::Bootloader) {
        error_ = {{"code", "invalid_request"}, {"reason", "Device management needs an attached Flipper with a serial port."},
            {"suggestion", "Connect the device and wait until it is Connected."}};
        publish(); return;
    }
    if(const auto holder = holderName(*it); !holder.isEmpty() && holder != "Device") {
        error_ = {{"code", "port_busy"}, {"reason", holder + " is using the serial port."},
            {"suggestion", "Close " + holder + " before opening Backup."}};
        publish(); return;
    }
    if(it->manage) { publish(); return; }
    if(it->probe) { it->probe->cancel(); it->probe->deleteLater(); it->probe = nullptr; }
    if(it->state == State::Connected) transition(*it, State::Busy);
    auto* session = new ManageSession(this); it->manage = session;
    session->setDevice(it->info, it->rpc);
    connect(session, &ManageSession::ready, this, [this,key,session] {
        auto it = devices_.find(key);
        if(it == devices_.end() || it->manage != session) return;
        publish();
    });
    connect(session, &ManageSession::changed, this, [this,key,session] {
        auto it = devices_.find(key);
        if(it == devices_.end() || it->manage != session) return;
        publish();
    });
    connect(session, &ManageSession::failed, this, [this,key,session](const QJsonObject& error) {
        auto it = devices_.find(key);
        if(it == devices_.end() || it->manage != session) return;
        it->error = error; it->manage = nullptr; session->deleteLater();
        transition(*it, State::Error); publish();
    });
    connect(session, &ManageSession::stopped, this, [this,key,session] {
        auto it = devices_.find(key);
        if(it == devices_.end() || it->manage != session) return;
        it->manage = nullptr; session->deleteLater();
        if(it->state == State::Busy) transition(*it, State::Connected);
        publish();
    });
    publish(); session->start(it->usb.port);
}

void Backend::stopManage(const QString& key) {
    auto it = devices_.find(key);
    if(it == devices_.end() || !it->manage) { publish(); return; }
    it->manage->stop();
}

void Backend::startDev(const QString& key) {
    auto it = devices_.find(key);
    if(it == devices_.end() || it->usb.port.isEmpty() || it->state == State::Bootloader) {
        error_ = {{"code", "invalid_request"}, {"reason", "Dev needs an attached Flipper with a serial port."},
            {"suggestion", "Connect the device and wait until it is Connected."}};
        publish(); return;
    }
    if(const auto holder = holderName(*it); !holder.isEmpty() && holder != "Dev") {
        error_ = {{"code", "port_busy"}, {"reason", holder + " is using the serial port."},
            {"suggestion", "Close " + holder + " before opening Dev."}};
        publish(); return;
    }
    if(it->dev) { publish(); return; }
    if(it->probe) { it->probe->cancel(); it->probe->deleteLater(); it->probe = nullptr; }
    if(it->state == State::Connected) transition(*it, State::Busy);
    auto* session = new DevSession(this); it->dev = session;
    session->setOrigin(it->info.value("firmware_origin_fork").toString(),
        it->info.value("firmware_version").toString());
    connect(session, &DevSession::ready, this, [this,key,session] {
        auto it = devices_.find(key);
        if(it == devices_.end() || it->dev != session) return;
        publish();
    });
    connect(session, &DevSession::changed, this, [this,key,session] {
        auto it = devices_.find(key);
        if(it == devices_.end() || it->dev != session) return;
        publish();
    });
    connect(session, &DevSession::failed, this, [this,key,session](const QJsonObject& error) {
        auto it = devices_.find(key);
        if(it == devices_.end() || it->dev != session) return;
        it->error = error; it->dev = nullptr; session->deleteLater();
        transition(*it, State::Error); publish();
    });
    connect(session, &DevSession::stopped, this, [this,key,session] {
        auto it = devices_.find(key);
        if(it == devices_.end() || it->dev != session) return;
        it->dev = nullptr; session->deleteLater();
        if(it->state == State::Busy) transition(*it, State::Connected);
        publish();
    });
    publish(); session->start(it->usb.port);
}

void Backend::stopDev(const QString& key) {
    auto it = devices_.find(key);
    if(it == devices_.end() || !it->dev) { publish(); return; }
    it->dev->stop();
}
void Backend::command(const QJsonObject& request) {
    error_ = {};
    const auto op = request["op"].toString();
    if(op == "snapshot") { publish(); return; }
    if(op == "configure" && request["autoConnect"].isBool()) {
        const bool wasEnabled = autoConnect_;
        autoConnect_ = request["autoConnect"].toBool();
        preferred_ = request["preferredDevice"].toString();
        if(request.contains("notifyConnect")) notifyConnect_ = request["notifyConnect"].toBool();
        if(request.contains("notifyError")) notifyError_ = request["notifyError"].toBool();
        chooseSelection();
        if(!wasEnabled && autoConnect_) for(const auto& key : devices_.keys()) probe(key);
        publish(); return;
    }
    const auto key = request["key"].toString();
    if((op == "select" || op == "refresh") && devices_.contains(key)) {
        if(op == "select") { selected_ = key; preferred_ = stableId(devices_[key].usb); publish(); return; }
        if(refuseHeldPort(devices_[key], "Refresh")) return;
        probe(key); return;
    }
    if(op == "remoteStart") { startRemote(key.isEmpty() ? selected_ : key); return; }
    if(op == "remoteStop") { stopRemote(key.isEmpty() ? selected_ : key); return; }
    if(op == "filesStart") { startFiles(key.isEmpty() ? selected_ : key); return; }
    if(op == "filesStop") { stopFiles(key.isEmpty() ? selected_ : key); return; }
    if(op == "cliStart") { startCli(key.isEmpty() ? selected_ : key); return; }
    if(op == "cliStop") { stopCli(key.isEmpty() ? selected_ : key); return; }
    if(op == "appsStart") { startApps(key.isEmpty() ? selected_ : key); return; }
    if(op == "appsStop") { stopApps(key.isEmpty() ? selected_ : key); return; }
    if(op == "manageStart") { startManage(key.isEmpty() ? selected_ : key); return; }
    if(op == "manageStop") { stopManage(key.isEmpty() ? selected_ : key); return; }
    if(op == "devStart") { startDev(key.isEmpty() ? selected_ : key); return; }
    if(op == "devStop") { stopDev(key.isEmpty() ? selected_ : key); return; }
    if(op == "devProject" || op == "devCreate" || op == "devBuild" || op == "devLint"
        || op == "devUpdateSdk" || op == "devInstallUfbt" || op == "devDeploy" || op == "devInspect") {
        const auto target = key.isEmpty() ? selected_ : key;
        if(!devices_.contains(target) || !devices_[target].dev) {
            error_ = {{"code", "invalid_request"}, {"reason", "Dev is not running."},
                {"suggestion", "Open Dev, wait for RPC, then try again."}};
            publish(); return;
        }
        auto* dev = devices_[target].dev;
        dev->setOrigin(devices_[target].info.value("firmware_origin_fork").toString(),
            devices_[target].info.value("firmware_version").toString());
        if(op == "devProject") dev->setProject(request["path"].toString());
        else if(op == "devCreate") dev->createApp(request["appId"].toString());
        else if(op == "devBuild") dev->build();
        else if(op == "devLint") dev->lint();
        else if(op == "devUpdateSdk") dev->updateSdk();
        else if(op == "devInstallUfbt") dev->installUfbt();
        else if(op == "devDeploy") dev->deploy(request["overwrite"].toBool());
        else dev->inspect(request["kind"].toString(), request["arg"].toString());
        return;
    }
    if(op == "backupCreate" || op == "backupRestore" || op == "backupRefresh"
        || op == "firmwareApply" || op == "packsInstall" || op == "packsRemove") {
        const auto target = key.isEmpty() ? selected_ : key;
        if(!devices_.contains(target) || !devices_[target].manage) {
            error_ = {{"code", "invalid_request"}, {"reason", "Device management is not running."},
                {"suggestion", "Open Backup, wait for the list, then try again."}};
            publish(); return;
        }
        auto* manage = devices_[target].manage;
        manage->setDevice(devices_[target].info, devices_[target].rpc);
        if(op == "backupCreate") manage->createBackup();
        else if(op == "backupRefresh") manage->refresh();
        else if(op == "backupRestore")
            manage->restore(request["archive"].toString(), request["confirmOrigin"].toBool(), request["confirmVersion"].toBool());
        else if(op == "packsInstall")
            manage->installPack(packs_.value("path").toString(), packs_);
        else if(op == "packsRemove")
            manage->removePack(request["name"].toString());
        else manage->applyFirmware(firmware_.value("path").toString(), firmware_, request["replaceOrigin"].toBool());
        return;
    }
    if(op == "firmwareCheck") {
        QString target = "f7";
        const auto deviceKey = key.isEmpty() ? selected_ : key;
        if(devices_.contains(deviceKey))
            target = hardwareTargetName(devices_[deviceKey].info.value("hardware_target").toString());
        if(target.isEmpty()) target = "f7";
        const auto provider = request["provider"].toString() == "momentum" ? QString("momentum") : QString("official");
        firmwareClient_.check(provider, request["channel"].toString("release"), target);
        return;
    }
    if(op == "firmwareDownload") {
        if(!firmware_.value("url").toString().isEmpty()) {
            firmware_.insert("downloading", true);
            packs_.insert("downloading", false);
            firmwareClient_.download(firmware_);
        } else {
            const auto label = providerLabel(firmware_.value("provider").toString());
            error_ = {{"code", "firmware_index"}, {"reason", "Check " + label + " firmware first."},
                {"suggestion", "Choose Check " + label + " firmware, then Download."}};
        }
        publish();
        return;
    }
    if(op == "packsCheck") {
        packs_.insert("downloading", false);
        firmwareClient_.checkPacks();
        return;
    }
    if(op == "packsDownload") {
        const auto id = request["id"].toString();
        QJsonObject pack;
        for(const auto& value : packs_.value("catalog").toArray()) {
            const auto item = value.toObject();
            if(item.value("id").toString() == id) { pack = item; break; }
        }
        if(pack.isEmpty()) {
            error_ = {{"code", "pack_index"}, {"reason", "Check asset packs first, then choose a pack."},
                {"suggestion", "Choose Check packs."}};
            publish();
            return;
        }
        packs_.insert("downloading", true);
        firmware_.insert("downloading", false);
        firmwareClient_.downloadPack(pack);
        return;
    }
    if(op == "appsRefresh" || op == "appsLaunch" || op == "appsExit" || op == "appsRemove"
        || op == "appsInstall" || op == "appsRead" || op == "appsWrite") {
        const auto target = key.isEmpty() ? selected_ : key;
        if(!devices_.contains(target) || !devices_[target].apps) {
            error_ = {{"code", "invalid_request"}, {"reason", "Apps is not running."},
                {"suggestion", "Open Apps, wait for the list, then try again."}};
            publish(); return;
        }
        auto* apps = devices_[target].apps;
        if(op == "appsRefresh") apps->refresh();
        else if(op == "appsLaunch") apps->launch(request["path"].toString());
        else if(op == "appsExit") apps->exitApp();
        else if(op == "appsRemove") apps->remove(request["path"].toString());
        else if(op == "appsInstall")
            apps->install(request["hostPath"].toString(), request["destDir"].toString(), request["overwrite"].toBool());
        else if(op == "appsRead") apps->readScript(request["path"].toString());
        else apps->writeScript(request["path"].toString(), request["text"].toString());
        return;
    }
    if(op == "cliSend" || op == "cliInterrupt" || op == "cliSave") {
        const auto target = key.isEmpty() ? selected_ : key;
        if(!devices_.contains(target) || !devices_[target].cli) {
            error_ = {{"code", "invalid_request"}, {"reason", "CLI is not running."},
                {"suggestion", "Open CLI, wait for the prompt, then try again."}};
            publish(); return;
        }
        auto* cli = devices_[target].cli;
        if(op == "cliSend") cli->sendLine(request["text"].toString());
        else if(op == "cliInterrupt") cli->interrupt();
        else {
            QString saveError;
            const auto path = cli->save(saveError);
            if(path.isEmpty()) {
                error_ = {{"code", "cli_save"}, {"reason", saveError},
                    {"suggestion", "Check that Downloads/OmaFlip is writable."}};
                publish();
            }
        }
        return;
    }
    if(op == "filesList" || op == "filesPreview" || op == "filesDownload" || op == "filesUpload"
        || op == "filesMkdir" || op == "filesRename" || op == "filesDelete") {
        const auto target = key.isEmpty() ? selected_ : key;
        if(!devices_.contains(target) || !devices_[target].files) {
            error_ = {{"code", "invalid_request"}, {"reason", "Files is not running."},
                {"suggestion", "Open Files, wait for the listing, then try again."}};
            publish(); return;
        }
        auto* files = devices_[target].files;
        if(op == "filesList") files->list(request["path"].toString());
        else if(op == "filesPreview") files->preview(request["path"].toString());
        else if(op == "filesDownload")
            files->download(request["path"].toString(), request["hostPath"].toString(), request["overwrite"].toBool());
        else if(op == "filesUpload")
            files->upload(request["path"].toString(), request["hostPath"].toString(), request["overwrite"].toBool());
        else if(op == "filesMkdir") files->mkdir(request["path"].toString());
        else if(op == "filesRename") files->rename(request["oldPath"].toString(), request["newPath"].toString());
        else files->remove(request["path"].toString(), request["recursive"].toBool());
        return;
    }
    if(op == "input") {
        const auto target = key.isEmpty() ? selected_ : key;
        const auto button = request["button"].toString();
        const auto kind = request["type"].toString("short");
        if(!devices_.contains(target) || !devices_[target].remote) {
            error_ = {{"code", "invalid_request"}, {"reason", "Remote is not running."},
                {"suggestion", "Open Remote, wait for the screen, then press a button."}};
            publish(); return;
        }
        fprintf(stderr, "OmaFlip input %s %s\n", qPrintable(button), qPrintable(kind));
        devices_[target].remote->sendInput(button, kind);
        return;
    }
    if(op == "screenshot" && devices_.contains(key.isEmpty() ? selected_ : key)) {
        const auto target = key.isEmpty() ? selected_ : key;
        auto& device = devices_[target];
        QString saveError;
        const auto path = device.remote ? saveScreenshot(device.remote->lastImage(), saveError) : QString();
        if(path.isEmpty()) error_ = {{"code", "screenshot"}, {"reason", saveError.isEmpty() ? "Start Remote before taking a screenshot." : saveError},
            {"suggestion", "Open Remote, wait for a frame, then save again."}};
        else {
            device.screenshots.prepend(path);
            while(device.screenshots.size() > 8) device.screenshots.removeLast();
        }
        publish(); return;
    }
    error_ = {{"code", "invalid_request"}, {"reason", "Unknown operation or device key."},
              {"suggestion", "Request a fresh snapshot and select an attached device."}};
    publish();
}
}
