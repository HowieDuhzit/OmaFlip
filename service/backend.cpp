#include "backend.h"
#include <QDateTime>
#include <QTimer>

namespace omaflip {
Backend::Backend(QObject* parent) : QObject(parent), discovery_(this) {
    connect(&discovery_, &Discovery::changed, this, &Backend::reconcile);
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
            if(it->probe) { it->probe->cancel(); it->probe->deleteLater(); }
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
    if(it == devices_.end() || it->probe || it->usb.port.isEmpty() || it->state == State::Bootloader) return;
    transition(*it, State::Connecting);
    it->error = {}; it->info = {}; it->power = {}; it->warning.clear(); it->sampledAt.clear();
    auto* session = new SerialProbe(this); it->probe = session;
    connect(session, &SerialProbe::finished, this, [this,key,session](const QJsonObject& info, const QJsonObject& power, const QString& warning) {
        auto it = devices_.find(key);
        if(it == devices_.end() || it->probe != session) return;
        it->info = info; it->power = power; it->warning = warning;
        it->sampledAt = QDateTime::currentDateTimeUtc().toString(Qt::ISODate);
        transition(*it, State::Connected); it->probe = nullptr;
        session->deleteLater(); publish();
    });
    connect(session, &SerialProbe::failed, this, [this,key,session](const QJsonObject& error) {
        auto it = devices_.find(key);
        if(it == devices_.end() || it->probe != session) return;
        it->error = error;
        transition(*it, error["code"] == "port_busy" ? State::Busy : State::Error);
        it->probe = nullptr; session->deleteLater(); publish();
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
            {"info", device.info}, {"power", device.power}, {"error", device.error},
            {"warning", device.warning}, {"sampledAt", device.sampledAt}, {"rpc", "Not tested (Milestone 2)"}});
    }
    return {{"protocol", 1}, {"type", "snapshot"}, {"devices", devices}, {"selected", selected_}, {"error", error_}};
}
void Backend::command(const QJsonObject& request) {
    error_ = {};
    const auto op = request["op"].toString();
    if(op == "snapshot") { publish(); return; }
    if(op == "configure" && request["autoConnect"].isBool()) {
        const bool wasEnabled = autoConnect_;
        autoConnect_ = request["autoConnect"].toBool();
        preferred_ = request["preferredDevice"].toString();
        chooseSelection();
        if(!wasEnabled && autoConnect_) for(const auto& key : devices_.keys()) probe(key);
        publish(); return;
    }
    const auto key = request["key"].toString();
    if((op == "select" || op == "refresh") && devices_.contains(key)) {
        if(op == "select") { selected_ = key; preferred_ = stableId(devices_[key].usb); }
        else probe(key);
    } else {
        error_ = {{"code", "invalid_request"}, {"reason", "Unknown operation or device key."},
                  {"suggestion", "Request a fresh snapshot and select an attached device."}};
    }
    publish();
}
}
