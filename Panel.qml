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
    readonly property var rpc: device && device.rpc && typeof device.rpc === "object" ? device.rpc : ({})
    readonly property string rpcText: rpc.summary || ""
    readonly property string storageText: rpc.storage || ""
    readonly property string regionText: info.hardware_region_provisioned || ""
    property bool details: false
    property bool showDiagnostics: false
    property bool remoteOpen: false
    property bool filesOpen: false
    property bool cliOpen: false
    property bool appsOpen: false
    property bool manageOpen: false
    property bool devOpen: false
    property int cursor: 0
    readonly property bool sessionOpen: remoteOpen || filesOpen || cliOpen || appsOpen || manageOpen || devOpen
    readonly property string status: !service ? "Service unavailable" : (!service.ready ? "Service unavailable" :
        (device ? (device.remote ? "Remote" : (device.files && device.files.open ? "Files" : (device.cli && device.cli.open ? "CLI" : (device.apps && device.apps.open ? "Apps" : (device.manage && device.manage.open ? "Backup" : (device.dev && device.dev.open ? "Dev" : (device.identityConfirmed ? device.state : "STM32 DFU · identity unconfirmed"))))))) :
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
            rows.push({label: remoteOpen ? "Close remote" : "Remote", op: "remote"})
            rows.push({label: filesOpen ? "Close files" : "Files", op: "files"})
            rows.push({label: cliOpen ? "Close CLI" : "CLI", op: "cli"})
            rows.push({label: appsOpen ? "Close apps" : "Apps", op: "apps"})
            rows.push({label: manageOpen ? "Close backup" : "Backup", op: "manage"})
            rows.push({label: devOpen ? "Close dev" : "Dev", op: "dev"})
            if (!sessionOpen) {
                rows.push({label: "Refresh / Retry", op: "refresh"})
                rows.push({label: details ? "Hide device details" : "Device details", op: "details"})
            } else if (remoteOpen) {
                rows.push({label: "Screenshot", op: "screenshot"})
                rows.push({label: "Copy screenshot", op: "copyShot"})
            }
        }
        if (errorText) rows.push({label: "Copy error", op: "copy"})
        rows.push({label: showDiagnostics ? "Hide diagnostics" : "Open diagnostics", op: "diagnostics"})
        rows.push({label: "Restart service", op: "restart"})
        rows.push({label: "Auto-connect: " + (hostWidget && hostWidget.setting("autoConnect", true) ? "On" : "Off"), op: "auto"})
        rows.push({label: "Bar label: " + (hostWidget && hostWidget.setting("compact", false) ? "Icon" : "Device name"), op: "compact"})
        rows.push({label: "Notify connect: " + (hostWidget && hostWidget.setting("notifyConnect", true) ? "On" : "Off"), op: "notifyConnect"})
        rows.push({label: "Notify errors: " + (hostWidget && hostWidget.setting("notifyError", true) ? "On" : "Off"), op: "notifyError"})
        if (!service || !service.ready) rows.push({label: "Build / repair backend", op: "build"})
        rows.push({label: "Setup Device Access", op: "setup"})
        rows.push({label: "Open documentation", op: "docs"})
        return rows
    }
    function closeExclusive(except) {
        if (except !== "remote" && remoteOpen) { remoteOpen = false; if (service) service.remoteStop() }
        if (except !== "files" && filesOpen) { filesOpen = false; if (service) service.filesStop() }
        if (except !== "cli" && cliOpen) { cliOpen = false; if (service) service.cliStop() }
        if (except !== "apps" && appsOpen) { appsOpen = false; if (service) service.appsStop() }
        if (except !== "manage" && manageOpen) { manageOpen = false; if (service) service.manageStop() }
        if (except !== "dev" && devOpen) { devOpen = false; if (service) service.devStop() }
    }
    function toggleSession(name) {
        closeExclusive(name)
        if (name === "remote") {
            remoteOpen = !remoteOpen
            if (remoteOpen && service) service.remoteStart()
            else if (service) service.remoteStop()
        } else if (name === "files") {
            filesOpen = !filesOpen
            if (filesOpen && service) service.filesStart()
            else if (service) service.filesStop()
        } else if (name === "cli") {
            cliOpen = !cliOpen
            if (cliOpen && service) service.cliStart()
            else if (service) service.cliStop()
        } else if (name === "apps") {
            appsOpen = !appsOpen
            if (appsOpen && service) service.appsStart()
            else if (service) service.appsStop()
        } else if (name === "manage") {
            manageOpen = !manageOpen
            if (manageOpen && service) service.manageStart()
            else if (service) service.manageStop()
        } else if (name === "dev") {
            devOpen = !devOpen
            if (devOpen && service) service.devStart()
            else if (service) service.devStop()
        }
    }
    function activate(index) {
        const action = actions[index]
        if (!action) return
        if (action.op === "select" && service) {
            service.selectDevice(action.key)
            if (hostWidget) hostWidget.persist("preferredDevice", action.id)
        }
        else if (action.op === "refresh" && service) service.refresh()
        else if (action.op === "remote" || action.op === "files" || action.op === "cli"
            || action.op === "apps" || action.op === "manage" || action.op === "dev") {
            root.toggleSession(action.op)
        }
        else if (action.op === "screenshot" && service) service.screenshot()
        else if (action.op === "copyShot") {
            const shots = root.device && root.device.screenshots
            if (shots && shots.length) {
                Quickshell.clipboardText = shots[0]
                Quickshell.execDetached(["bash", "-c", "wl-copy -t image/png < \"$1\"", "omaflip-copy", shots[0]])
            }
        }
        else if (action.op === "restart" && service) service.restart()
        else if (action.op === "details") details = !details
        else if (action.op === "diagnostics") showDiagnostics = !showDiagnostics
        else if (action.op === "copy") Quickshell.clipboardText = errorText
        else if (action.op === "docs") Qt.openUrlExternally(Qt.resolvedUrl("README.md"))
        else if (action.op === "auto" && hostWidget) hostWidget.persist("autoConnect", !hostWidget.setting("autoConnect", true))
        else if (action.op === "compact" && hostWidget) hostWidget.persist("compact", !hostWidget.setting("compact", false))
        else if (action.op === "notifyConnect" && hostWidget) hostWidget.persist("notifyConnect", !hostWidget.setting("notifyConnect", true))
        else if (action.op === "notifyError" && hostWidget) hostWidget.persist("notifyError", !hostWidget.setting("notifyError", true))
        else if (action.op === "build") Quickshell.execDetached(["omarchy-launch-terminal", "--hold", "bash", decodeURIComponent(Qt.resolvedUrl("scripts/build").toString().replace(/^file:\/\//, ""))])
        else if (action.op === "setup") Quickshell.execDetached(["omarchy-launch-terminal", "bash", decodeURIComponent(Qt.resolvedUrl("scripts/setup-access").toString().replace(/^file:\/\//, ""))])
        cursor = Math.min(cursor, actions.length - 1)
    }
    function open() {
        cursor = 0
        root.controller.show()
    }
    onOpenedChanged: if (!opened) {
        if (remoteOpen) { remoteOpen = false; if (service) service.remoteStop() }
        if (filesOpen) { filesOpen = false; if (service) service.filesStop() }
        if (cliOpen) { cliOpen = false; if (service) service.cliStop() }
        if (appsOpen) { appsOpen = false; if (service) service.appsStop() }
        if (manageOpen) { manageOpen = false; if (service) service.manageStop() }
        if (devOpen) { devOpen = false; if (service) service.devStop() }
    }

    Ui.KeyboardPanel {
        id: popup
        anchorItem: root.anchorItem
        owner: root.hostWidget || root
        bar: root.bar
        open: root.opened
        focusTarget: keys
        contentWidth: popup.fittedContentWidth(Style.space(root.sessionOpen ? 560 : (root.details ? 480 : 360)))
        contentHeight: popup.fittedContentHeight(content.implicitHeight)

        Ui.PanelKeyCatcher {
            id: keys
            anchors.fill: parent
            onCloseRequested: {
                if (root.remoteOpen) { root.remoteOpen = false; if (root.service) root.service.remoteStop(); return }
                if (root.filesOpen) {
                    if (fileView && fileView.confirmKind) { fileView.confirmKind = ""; fileView.confirmHosts = []; return }
                    if (fileView && fileView.nameMode) { fileView.nameMode = ""; return }
                    root.filesOpen = false; if (root.service) root.service.filesStop(); return
                }
                if (root.cliOpen) {
                    if (cliView && cliView.confirmText) { cliView.confirmText = ""; return }
                    if (cliView && root.device && root.device.cli && root.device.cli.streaming) {
                        if (root.service) root.service.cliInterrupt(); return
                    }
                    root.cliOpen = false; if (root.service) root.service.cliStop(); return
                }
                if (root.appsOpen) {
                    if (appsView && appsView.confirmKind) { appsView.confirmKind = ""; return }
                    root.appsOpen = false; if (root.service) root.service.appsStop(); return
                }
                if (root.manageOpen) {
                    if (manageView && manageView.confirmKind) { manageView.confirmKind = ""; return }
                    root.manageOpen = false; if (root.service) root.service.manageStop(); return
                }
                if (root.devOpen) {
                    if (devView && devView.confirmKind) { devView.confirmKind = ""; return }
                    root.devOpen = false; if (root.service) root.service.devStop(); return
                }
                root.close()
            }
            onMoveRequested: (dx, dy) => {
                if (root.remoteOpen && root.service) {
                    if (dy < 0) root.service.input("up")
                    else if (dy > 0) root.service.input("down")
                    else if (dx < 0) root.service.input("left")
                    else if (dx > 0) root.service.input("right")
                    return
                }
                if (root.filesOpen) {
                    const view = fileView
                    if (view) {
                        if (dy < 0 || dx < 0) view.cursor = Math.max(0, view.cursor - 1)
                        else if (dy > 0 || dx > 0) view.cursor = Math.min(Math.max(0, view.entries.length - 1), view.cursor + 1)
                    }
                    return
                }
                if (root.cliOpen && cliView) {
                    if (dy < 0) cliView.historyPrev()
                    else if (dy > 0) cliView.historyNext()
                    return
                }
                if (root.appsOpen && appsView) {
                    if (dy < 0 || dx < 0) appsView.cursor = Math.max(0, appsView.cursor - 1)
                    else if (dy > 0 || dx > 0) appsView.cursor = Math.min(Math.max(0, appsView.apps.length - 1), appsView.cursor + 1)
                    return
                }
                if (root.manageOpen && manageView) {
                    if (manageView.section === "packs") {
                        if (manageView.catalog.length) {
                            if (dy < 0 || dx < 0) manageView.catalogCursor = Math.max(0, manageView.catalogCursor - 1)
                            else if (dy > 0 || dx > 0) manageView.catalogCursor = Math.min(Math.max(0, manageView.catalog.length - 1), manageView.catalogCursor + 1)
                        } else {
                            if (dy < 0 || dx < 0) manageView.packCursor = Math.max(0, manageView.packCursor - 1)
                            else if (dy > 0 || dx > 0) manageView.packCursor = Math.min(Math.max(0, manageView.installed.length - 1), manageView.packCursor + 1)
                        }
                    } else {
                        if (dy < 0 || dx < 0) manageView.cursor = Math.max(0, manageView.cursor - 1)
                        else if (dy > 0 || dx > 0) manageView.cursor = Math.min(Math.max(0, manageView.backups.length - 1), manageView.cursor + 1)
                    }
                    return
                }
                root.moveCursor(dy || dx)
            }
            onTabRequested: direction => { if (!root.sessionOpen) root.moveCursor(direction) }
            onActivateRequested: {
                if (root.remoteOpen && root.service) root.service.input("ok")
                else if (root.filesOpen && fileView) {
                    if (fileView.confirmKind) fileView.runConfirm()
                    else if (fileView.nameMode) fileView.submitName()
                    else fileView.openItem(fileView.cursor)
                }
                else if (root.cliOpen && cliView) cliView.send()
                else if (root.appsOpen && appsView) {
                    if (appsView.confirmKind) appsView.runConfirm()
                    else appsView.launch()
                }
                else if (root.manageOpen && manageView) {
                    if (manageView.confirmKind) manageView.runConfirm()
                }
                else if (root.devOpen && devView) {
                    if (devView.confirmKind) devView.runConfirm()
                }
                else root.activate(root.cursor)
            }
            Flickable {
                id: scroll
                anchors.fill: parent
                contentHeight: content.implicitHeight
                clip: true
                interactive: !root.sessionOpen
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
                    Oma.RemoteView {
                        width: parent.width
                        visible: root.remoteOpen && root.device
                        service: root.service
                        foreground: root.barForeground
                    }
                    Oma.FilesView {
                        id: fileView
                        width: parent.width
                        visible: root.filesOpen && root.device
                        service: root.service
                        foreground: root.barForeground
                    }
                    Oma.CliView {
                        id: cliView
                        width: parent.width
                        visible: root.cliOpen && root.device
                        service: root.service
                        foreground: root.barForeground
                    }
                    Oma.AppsView {
                        id: appsView
                        width: parent.width
                        visible: root.appsOpen && root.device
                        service: root.service
                        foreground: root.barForeground
                    }
                    Oma.ManageView {
                        id: manageView
                        width: parent.width
                        visible: root.manageOpen && root.device
                        service: root.service
                        foreground: root.barForeground
                    }
                    Oma.DevView {
                        id: devView
                        width: parent.width
                        visible: root.devOpen && root.device
                        service: root.service
                        hostWidget: root.hostWidget
                        foreground: root.barForeground
                    }
                    Column {
                        width: parent.width
                        spacing: Style.space(7)
                        visible: root.device !== null && !root.sessionOpen
                        Oma.InfoRow { width: parent.width; label: "Firmware"; value: root.info.firmware_version; foreground: root.barForeground }
                        Oma.InfoRow { width: parent.width; label: "Origin"; value: root.info.firmware_origin_fork; foreground: root.barForeground }
                        Oma.InfoRow { width: parent.width; label: "Battery"; value: root.power.charge_level !== undefined ? root.power.charge_level + "% · " + (root.power.charge_state || "Unavailable") : null; foreground: root.barForeground }
                        Oma.InfoRow { width: parent.width; label: "Storage"; value: root.storageText || null; foreground: root.barForeground }
                    }
                    Column {
                        width: parent.width
                        spacing: Style.space(7)
                        visible: root.details && root.device !== null && !root.sessionOpen
                        Oma.InfoRow { width: parent.width; label: "USB serial"; value: root.device ? root.device.usbSerial : null; foreground: root.barForeground }
                        Oma.InfoRow { width: parent.width; label: "Hardware"; value: root.info.hardware_ver; foreground: root.barForeground }
                        Oma.InfoRow { width: parent.width; label: "Region"; value: root.regionText || null; foreground: root.barForeground }
                        Oma.InfoRow { width: parent.width; label: "Battery health"; value: root.power.battery_health !== undefined ? root.power.battery_health + "%" : null; foreground: root.barForeground }
                        Oma.InfoRow { width: parent.width; label: "Port"; value: root.device ? root.device.port : null; foreground: root.barForeground }
                        Oma.InfoRow { width: parent.width; label: "RPC"; value: root.rpcText || null; foreground: root.barForeground }
                        Oma.InfoRow { width: parent.width; label: "Probe"; value: root.rpc.probeMs !== undefined ? root.rpc.probeMs + " ms" : null; foreground: root.barForeground }
                        Oma.InfoRow { width: parent.width; label: "Ping"; value: root.rpc.pingMs !== undefined ? root.rpc.pingMs + " ms" : null; foreground: root.barForeground }
                        Oma.InfoRow { width: parent.width; label: "OmaFlip"; value: root.service && root.service.backendVersion ? root.service.backendVersion : null; foreground: root.barForeground }
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
