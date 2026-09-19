#pragma once
#include "discovery.h"
#include "serial.h"
#include <QJsonArray>
#include <QMap>
#include <memory>
namespace omaflip {
struct Device {
    UsbIdentity usb;
    State state = State::Disconnected;
    QJsonObject info, power, error;
    QString warning, sampledAt;
    SerialProbe* probe = nullptr;
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
    void transition(Device& device, State next);
    void publish();
    void chooseSelection();
    Discovery discovery_;
    QMap<QString, Device> devices_;
    QString selected_;
    QString preferred_;
    QJsonObject error_;
    bool autoConnect_ = false;
};
}
