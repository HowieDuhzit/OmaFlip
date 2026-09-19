#include "discovery.h"
#include <QSocketNotifier>
#include <libudev.h>
#include <cstring>
#include <initializer_list>

namespace omaflip {
namespace {
QString prop(udev_device* dev, const char* name) {
    const char* value = udev_device_get_property_value(dev, name);
    return value ? QString::fromUtf8(value) : QString();
}
QString firstProp(udev_device* dev, std::initializer_list<const char*> names) {
    for(const char* name : names) {
        const auto value = prop(dev, name);
        if(!value.isEmpty()) return value;
    }
    return {};
}
UsbIdentity identity(udev_device* usb) {
    // Prefer udev properties. USB sysfs attribute reads issue control transfers and
    // can block the backend indefinitely (seen on hub manufacturer_show).
    QString vendor = firstProp(usb, {"ID_VENDOR_ID", "ID_USB_VENDOR_ID"});
    QString product = firstProp(usb, {"ID_MODEL_ID", "ID_USB_MODEL_ID"});
    if(vendor.isEmpty()) {
        const char* raw = udev_device_get_sysattr_value(usb, "idVendor");
        if(raw) vendor = QString::fromUtf8(raw);
    }
    if(product.isEmpty() && vendor.toLower() == "0483") {
        const char* raw = udev_device_get_sysattr_value(usb, "idProduct");
        if(raw) product = QString::fromUtf8(raw);
    }
    QString manufacturer = firstProp(usb, {"ID_USB_VENDOR", "ID_VENDOR"});
    manufacturer.replace('_', ' ');
    QString description = firstProp(usb, {"ID_USB_MODEL", "ID_MODEL"});
    description.replace('_', ' ');
    QString serial = firstProp(usb, {"ID_USB_SERIAL_SHORT", "ID_SERIAL_SHORT"});
    if(vendor.toLower() == "0483" && manufacturer.isEmpty()) {
        const char* raw = udev_device_get_sysattr_value(usb, "manufacturer");
        if(raw) manufacturer = QString::fromUtf8(raw);
    }
    if(vendor.toLower() == "0483" && description.isEmpty()) {
        const char* raw = udev_device_get_sysattr_value(usb, "product");
        if(raw) description = QString::fromUtf8(raw);
    }
    if(vendor.toLower() == "0483" && serial.isEmpty()) {
        const char* raw = udev_device_get_sysattr_value(usb, "serial");
        if(raw) serial = QString::fromUtf8(raw);
    }
    return {vendor, product, manufacturer, description, serial,
        QString::fromUtf8(udev_device_get_syspath(usb)), {}};
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
    auto add = [&](const UsbIdentity& item) {
        if(classify(item) == UsbKind::Other) return;
        if(!result.contains(item.syspath) || !item.port.isEmpty()) result[item.syspath] = item;
    };
    {
        auto* enumeration = udev_enumerate_new(context_);
        udev_enumerate_add_match_subsystem(enumeration, "usb");
        udev_enumerate_add_match_sysattr(enumeration, "idVendor", "0483");
        udev_enumerate_scan_devices(enumeration);
        udev_list_entry* entry;
        udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(enumeration)) {
            auto* dev = udev_device_new_from_syspath(context_, udev_list_entry_get_name(entry));
            if(dev) { add(identity(dev)); udev_device_unref(dev); }
        }
        udev_enumerate_unref(enumeration);
    }
    {
        auto* enumeration = udev_enumerate_new(context_);
        udev_enumerate_add_match_subsystem(enumeration, "tty");
        udev_enumerate_scan_devices(enumeration);
        udev_list_entry* entry;
        udev_list_entry_foreach(entry, udev_enumerate_get_list_entry(enumeration)) {
            auto* dev = udev_device_new_from_syspath(context_, udev_list_entry_get_name(entry));
            if(!dev) continue;
            auto* usb = udev_device_get_parent_with_subsystem_devtype(dev, "usb", "usb_device");
            if(usb) {
                auto item = identity(usb);
                if(classify(item) != UsbKind::Other) {
                    item.port = QString::fromUtf8(udev_device_get_devnode(dev));
                    udev_list_entry* link;
                    udev_list_entry_foreach(link, udev_device_get_devlinks_list_entry(dev)) {
                        QString path = QString::fromUtf8(udev_list_entry_get_name(link));
                        if(path.startsWith("/dev/serial/by-id/")) { item.port = path; break; }
                    }
                    add(item);
                }
            }
            udev_device_unref(dev);
        }
        udev_enumerate_unref(enumeration);
    }
    return result;
}
}
