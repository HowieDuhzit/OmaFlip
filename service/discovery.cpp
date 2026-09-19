#include "discovery.h"
#include <QSocketNotifier>
#include <libudev.h>
#include <cstring>

namespace omaflip {
namespace {
QString attr(udev_device* dev, const char* name) {
    return QString::fromUtf8(udev_device_get_sysattr_value(dev, name));
}
UsbIdentity identity(udev_device* dev) {
    return {attr(dev,"idVendor"), attr(dev,"idProduct"), attr(dev,"manufacturer"),
        attr(dev,"product"), attr(dev,"serial"), QString::fromUtf8(udev_device_get_syspath(dev)), {}};
}
}
Discovery::Discovery(QObject* parent) : QObject(parent), context_(udev_new()) {}
Discovery::~Discovery() {
    delete notifier_;
    if(monitor_) udev_monitor_unref(monitor_);
    if(context_) udev_unref(context_);
}
bool Discovery::start(QString& error) {
    if(!context_) { error = "Could not initialize libudev."; return false; }
    monitor_ = udev_monitor_new_from_netlink(context_, "udev");
    if(!monitor_ || udev_monitor_filter_add_match_subsystem_devtype(monitor_, "tty", nullptr) < 0 ||
       udev_monitor_filter_add_match_subsystem_devtype(monitor_, "usb", "usb_device") < 0 ||
       udev_monitor_enable_receiving(monitor_) < 0) {
        error = "Could not subscribe to udev USB/serial events."; return false;
    }
    notifier_ = new QSocketNotifier(udev_monitor_get_fd(monitor_), QSocketNotifier::Read, this);
    connect(notifier_, &QSocketNotifier::activated, this, [this] {
        bool any = false;
        while(auto* event = udev_monitor_receive_device(monitor_)) {
            any = true;
            udev_device_unref(event);
        }
        if(any) emit changed();
    });
    return true;
}
QMap<QString, UsbIdentity> Discovery::scan() const {
    QMap<QString, UsbIdentity> result;
    if(!context_) return result;
    for(const char* subsystem : {"usb", "tty"}) {
        auto* enumeration = udev_enumerate_new(context_);
        udev_enumerate_add_match_subsystem(enumeration, subsystem);
        udev_enumerate_scan_devices(enumeration);
        udev_list_entry* entry;
        udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(enumeration)) {
            auto* dev = udev_device_new_from_syspath(context_, udev_list_entry_get_name(entry));
            if(!dev) continue;
            auto* usb = std::strcmp(subsystem,"usb") == 0 ? dev : udev_device_get_parent_with_subsystem_devtype(dev,"usb","usb_device");
            if(usb) {
                auto item = identity(usb);
                const auto kind = classify(item);
                if(kind != UsbKind::Other) {
                    if(std::strcmp(subsystem,"tty") == 0) {
                        item.port = QString::fromUtf8(udev_device_get_devnode(dev));
                        udev_list_entry* link;
                        udev_list_entry_foreach(link, udev_device_get_devlinks_list_entry(dev)) {
                            QString path = QString::fromUtf8(udev_list_entry_get_name(link));
                            if(path.startsWith("/dev/serial/by-id/")) { item.port = path; break; }
                        }
                    }
                    // Key by physical USB node; stable IDs are exposed separately.
                    // Duplicate/missing serial descriptors cannot collapse two devices.
                    if(!result.contains(item.syspath) || !item.port.isEmpty()) result[item.syspath] = item;
                }
            }
            udev_device_unref(dev);
        }
        udev_enumerate_unref(enumeration);
    }
    return result;
}
}
