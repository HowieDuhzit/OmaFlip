import QtQuick
import qs.Ui as Ui

Ui.BarWidget {
    id: root
    moduleName: "io.github.howieduhzit.omaflip"
    readonly property var service: bar?.shell?.serviceFor(moduleName) ?? null
    readonly property var device: service ? service.selectedDevice : null
    readonly property bool opened: panelLoader.item ? panelLoader.item.opened : false
    readonly property bool popoutSwitchClosing: panelLoader.item ? panelLoader.item.popoutSwitchClosing : false
    readonly property string deviceName: device && device.info.hardware_name ? device.info.hardware_name : "Flipper"

    function open() { if (panelLoader.item) panelLoader.item.open() }
    function close() { if (panelLoader.item) panelLoader.item.close() }
    function toggle() { if (panelLoader.item) panelLoader.item.toggle() }
    function closeForPopoutSwitch() { if (panelLoader.item) panelLoader.item.closeForPopoutSwitch() }
    function injectPanel() {
        if (!panelLoader.item) return
        panelLoader.item.bar = root.bar
        panelLoader.item.anchorItem = button
        panelLoader.item.hostWidget = root
    }
    function configure() {
        if (service) service.configure(setting("autoConnect", true), setting("preferredDevice", ""),
            setting("notifyConnect", true), setting("notifyError", true))
    }
    function persist(name, value) {
        const entry = Object.assign({}, settings, {id: moduleName})
        entry[name] = value
        settings = entry
        if (bar?.shell) bar.shell.updateEntryInline(moduleName, entry)
    }
    onBarChanged: injectPanel()
    onServiceChanged: configure()
    onSettingsChanged: configure()
    implicitWidth: button.implicitWidth
    implicitHeight: button.implicitHeight

    Loader {
        id: panelLoader
        active: true
        visible: false
        source: Qt.resolvedUrl("Panel.qml")
        onLoaded: { root.injectPanel(); Qt.callLater(root.injectPanel) }
    }
    Ui.WidgetButton {
        id: button
        anchors.fill: parent
        bar: root.bar
        text: root.vertical || root.setting("compact", false) ? "󰓻" : "󰓻  " + root.deviceName
        tooltipText: "OmaFlip · " + (root.service ? root.service.state : "Service unavailable")
            + (root.device && root.device.power.charge_level !== undefined ? " · " + root.device.power.charge_level + "%" : "")
        Accessible.role: Accessible.Button
        Accessible.name: "OmaFlip " + root.deviceName + " " + (root.service ? root.service.state : "unavailable")
        Accessible.onPressAction: root.toggle()
        onPressed: buttonCode => { if (buttonCode === Qt.LeftButton) root.toggle() }
    }
}
