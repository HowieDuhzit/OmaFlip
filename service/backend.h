#pragma once
#include "discovery.h"
#include "serial.h"
#include "remote.h"
#include "files.h"
#include "cli.h"
#include "apps.h"
#include "manage.h"
#include "dev.h"
#include "firmware.h"
#include <QJsonArray>
#include <QMap>
#include <memory>
namespace omaflip {
struct Device {
    UsbIdentity usb;
    State state = State::Disconnected;
    QJsonObject info, power, error, rpc;
    QString warning, sampledAt;
    SerialProbe* probe = nullptr;
    RemoteSession* remote = nullptr;
    FileSession* files = nullptr;
    CliSession* cli = nullptr;
    AppSession* apps = nullptr;
    ManageSession* manage = nullptr;
    DevSession* dev = nullptr;
    QStringList screenshots;
};
class Backend : public QObject {
    Q_OBJECT
public:
    explicit Backend(QObject* parent = nullptr);
    bool start(QString& error);
    void command(const QJsonObject& request);
    QJsonObject snapshot() const;
signals:
    void event(const QJsonObject& snapshot);
private:
    void reconcile();
    void probe(const QString& key);
    void startRemote(const QString& key);
    void stopRemote(const QString& key);
    void startFiles(const QString& key);
    void stopFiles(const QString& key);
    void startCli(const QString& key);
    void stopCli(const QString& key);
    void startApps(const QString& key);
    void stopApps(const QString& key);
    void startManage(const QString& key);
    void stopManage(const QString& key);
    void startDev(const QString& key);
    void stopDev(const QString& key);
    bool refuseHeldPort(const Device& device, const QString& action);
    QString holderName(const Device& device) const;
    void transition(Device& device, State next);
    void publish();
    void publishFrame(const QString& key, const QByteArray& png, int orientation);
    void chooseSelection();
    void announce(const QString& key, const Device& device, const QString& previous);
    Discovery discovery_;
    QMap<QString, Device> devices_;
    QString selected_;
    QString preferred_;
    QJsonObject error_;
    QJsonObject firmware_;
    QJsonObject packs_;
    FirmwareClient firmwareClient_;
    bool autoConnect_ = false;
    bool notifyConnect_ = true;
    bool notifyError_ = true;
    QMap<QString, QString> lastAnnounced_;
};
}
