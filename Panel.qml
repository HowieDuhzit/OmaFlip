import QtQuick
import Quickshell
import qs.Commons
import qs.Ui as Ui
import "qml" as Oma

Ui.Panel {
    id: root
    moduleName: "io.github.howieduhzit.omaflip"
    manageIpc: false
    property var anchorItem: null
    property var hostWidget: null
    readonly property var service: hostWidget ? hostWidget.service : null
    readonly property var device: service ? service.selectedDevice : null
    readonly property var info: device ? device.info : ({})
    readonly property var power: device ? device.power : ({})
    property bool details: false
    property bool showDiagnostics: false
    property int cursor: 0
    readonly property string status: !service ? "Service unavailable" : (!service.ready ? "Service unavailable" :
        (device ? (device.identityConfirmed ? device.state : "STM32 DFU · identity unconfirmed") :
        (service.devices.length ? "Select a device" : "No Flipper Zero detected")))
    readonly property string errorText: {
        if (!service) return "Enable the OmaFlip service alongside the bar widget."
        if (service.serviceError) return service.serviceError
        if (device && device.error.reason) return device.error.operation + "\n" + device.error.path + "\n" + device.error.reason + "\n" + device.error.suggestion
        return ""
    }
    readonly property var actions: {
        let rows = []
        if (service) for (const item of service.devices) rows.push({label: (item.key === service.selectedKey ? "● " : "○ ") + (item.info.hardware_name || (item.identityConfirmed ? "Flipper " : "STM32 DFU ") + (item.usbSerial || item.key)), op: "select", key: item.key, id: item.id})
        if (device && device.identityConfirmed) {
            rows.push({label: "Refresh / Retry", op: "refresh"})
            rows.push({label: details ? "Hide device details" : "Device details", op: "details"})
        }
        if (errorText) rows.push({label: "Copy error", op: "copy"})
        rows.push({label: showDiagnostics ? "Hide diagnostics" : "Open diagnostics", op: "diagnostics"})
        rows.push({label: "Restart service", op: "restart"})
        rows.push({label: "Auto-connect: " + (hostWidget && hostWidget.setting("autoConnect", true) ? "On" : "Off"), op: "auto"})
        rows.push({label: "Bar label: " + (hostWidget && hostWidget.setting("compact", false) ? "Icon" : "Device name"), op: "compact"})
        if (!service || !service.ready) rows.push({label: "Build / repair backend", op: "build"})
        rows.push({label: "Setup Device Access", op: "setup"})
        rows.push({label: "Open documentation", op: "docs"})
        return rows
    }
    function activate(index) {
        const action = actions[index]
        if (!action) return
        if (action.op === "select" && service) {
            service.selectDevice(action.key)
            if (hostWidget) hostWidget.persist("preferredDevice", action.id)
        }
        else if (action.op === "refresh" && service) service.refresh()
        else if (action.op === "restart" && service) service.restart()
        else if (action.op === "details") details = !details
        else if (action.op === "diagnostics") showDiagnostics = !showDiagnostics
        else if (action.op === "copy") Quickshell.clipboardText = errorText
        else if (action.op === "docs") Qt.openUrlExternally(Qt.resolvedUrl("README.md"))
        else if (action.op === "auto" && hostWidget) hostWidget.persist("autoConnect", !hostWidget.setting("autoConnect", true))
        else if (action.op === "compact" && hostWidget) hostWidget.persist("compact", !hostWidget.setting("compact", false))
        else if (action.op === "build") Quickshell.execDetached(["omarchy-launch-terminal", "--hold", "bash", decodeURIComponent(Qt.resolvedUrl("scripts/build").toString().replace(/^file:\/\//, ""))])
        else if (action.op === "setup") Quickshell.execDetached(["omarchy-launch-terminal", "bash", decodeURIComponent(Qt.resolvedUrl("scripts/setup-access").toString().replace(/^file:\/\//, ""))])
        cursor = Math.min(cursor, actions.length - 1)
    }
    function open() {
        cursor = 0
        root.controller.show()
    }

    Ui.KeyboardPanel {
        id: popup
        anchorItem: root.anchorItem
        owner: root.hostWidget || root
        bar: root.bar
        open: root.opened
        focusTarget: keys
        contentWidth: popup.fittedContentWidth(Style.space(root.details ? 480 : 360))
        contentHeight: popup.fittedContentHeight(content.implicitHeight)

        Ui.PanelKeyCatcher {
            id: keys
            anchors.fill: parent
            onCloseRequested: root.close()
            onMoveRequested: (dx, dy) => root.moveCursor(dy || dx)
            onTabRequested: direction => root.moveCursor(direction)
            onActivateRequested: root.activate(root.cursor)
            Flickable {
                id: scroll
                anchors.fill: parent
                contentHeight: content.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                Column {
                    id: content
                    width: scroll.width
                    spacing: Style.space(10)
                    Text {
                        text: "󰓻  " + (root.info.hardware_name || "OmaFlip")
                        textFormat: Text.PlainText
                        color: root.barForeground
                        font.family: Style.font.family
                        font.pixelSize: Style.font.subtitle
                        font.bold: true
                    }
                    Text {
                        width: parent.width
                        text: root.status
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        color: root.barForeground
                        font.family: Style.font.family
                        font.pixelSize: Style.font.body
                    }
                    Text {
                        width: parent.width
                        visible: !root.device
                        text: root.service && root.service.ready && root.service.devices.length === 0
                            ? "Connect your Flipper Zero using a USB data cable."
                            : "Choose a device above, or inspect the service diagnostics below."
                        textFormat: Text.PlainText
                        wrapMode: Text.Wrap
                        color: Qt.alpha(root.barForeground, 0.65)
                        font.family: Style.font.family
                        font.pixelSize: Style.font.body
                    }
                    Column {
                        width: parent.width
                        spacing: Style.space(7)
                        visible: root.device !== null
                        Oma.InfoRow { width: parent.width; label: "Firmware"; value: root.info.firmware_version; foreground: root.barForeground }
                        Oma.InfoRow { width: parent.width; label: "Origin"; value: root.info.firmware_origin_fork; foreground: root.barForeground }
                        Oma.InfoRow { width: parent.width; label: "Battery"; value: root.power.charge_level !== undefined ? root.power.charge_level + "% · " + (root.power.charge_state || "Unavailable") : null; foreground: root.barForeground }
                    }
                    Column {
                        width: parent.width
                        spacing: Style.space(7)
                        visible: root.details && root.device !== null
                        Oma.InfoRow { width: parent.width; label: "USB serial"; value: root.device ? root.device.usbSerial : null; foreground: root.barForeground }
                        Oma.InfoRow { width: parent.width; label: "Hardware"; value: root.info.hardware_ver; foreground: root.barForeground }
                        Oma.InfoRow { width: parent.width; label: "Region"; value: root.info.hardware_region; foreground: root.barForeground }
                        Oma.InfoRow { width: parent.width; label: "Battery health"; value: root.power.battery_health !== undefined ? root.power.battery_health + "%" : null; foreground: root.barForeground }
                        Oma.InfoRow { width: parent.width; label: "Port"; value: root.device ? root.device.port : null; foreground: root.barForeground }
                        Oma.InfoRow { width: parent.width; label: "RPC"; value: root.device ? root.device.rpc : null; foreground: root.barForeground }
                        Oma.InfoRow { width: parent.width; label: "Last read (UTC)"; value: root.device ? root.device.sampledAt : null; foreground: root.barForeground }
                    }
                    Text {
                        width: parent.width
                        visible: text.length > 0
                        text: root.errorText || (root.device ? root.device.warning : "")
                        textFormat: Text.PlainText
                        wrapMode: Text.WrapAnywhere
                        color: root.barForeground
                        font.family: Style.font.family
                        font.pixelSize: Style.font.body
                    }
                    Text {
                        width: parent.width
                        visible: root.showDiagnostics
                        text: root.service ? (root.service.diagnostics || "No backend errors recorded.") + "\n" + JSON.stringify(root.device || {}, null, 2) : "Service not loaded."
                        textFormat: Text.PlainText
                        wrapMode: Text.WrapAnywhere
                        color: Qt.alpha(root.barForeground, 0.7)
                        font.family: Style.font.family
                        font.pixelSize: Style.font.caption
                    }
                    Rectangle { width: parent.width; height: 1; color: Qt.alpha(root.barForeground, 0.15) }
                    Column {
                        id: actionList
                        width: parent.width
                        Repeater {
                            id: actionRepeater
                            model: root.actions
                            Oma.Action {
                                required property var modelData
                                required property int index
                                width: actionList.width
                                text: modelData.label
                                selected: root.cursor === index
                                foreground: root.barForeground
                                onTriggered: { root.cursor = index; root.activate(index) }
                            }
                        }
                    }
                }
            }
        }
    }
    function moveCursor(direction) {
        cursor = (cursor + direction + actions.length) % actions.length
        const item = actionRepeater.itemAt(cursor)
        if (!item) return
        const point = item.mapToItem(content, 0, 0)
        if (point.y < scroll.contentY) scroll.contentY = point.y
        else if (point.y + item.height > scroll.contentY + scroll.height)
            scroll.contentY = point.y + item.height - scroll.height
    }
}
