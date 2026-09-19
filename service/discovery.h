#pragma once
#include "model.h"
#include <QObject>
#include <QMap>
struct udev;
struct udev_monitor;
class QSocketNotifier;
namespace omaflip {
class Discovery : public QObject {
    Q_OBJECT
public:
    explicit Discovery(QObject* parent = nullptr);
    ~Discovery() override;
    bool start(QString& error);
    QMap<QString, UsbIdentity> scan() const;
signals:
    void changed();
private:
    udev* context_ = nullptr;
    udev_monitor* monitor_ = nullptr;
    QSocketNotifier* notifier_ = nullptr;
};
}
